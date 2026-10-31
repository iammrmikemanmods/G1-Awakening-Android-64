#include "g1/CampaignController.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <limits>
#include <sys/stat.h>

namespace g1 {
namespace {
void require(bool value,const char* message) {
    if(!value)throw FormatError(message);
}
std::int16_t signed16(std::uint16_t value) noexcept {
    return static_cast<std::int16_t>(value<0x8000U ? std::int32_t(value) : std::int32_t(value)-0x10000);
}
std::uint64_t next_request_token() {
    static std::atomic<std::uint64_t> next{1};
    auto value=next.load(std::memory_order_relaxed);
    for(;;) {
        require(value!=std::numeric_limits<std::uint64_t>::max(),"campaign request token space exhausted");
        if(next.compare_exchange_weak(value,value+1U,std::memory_order_relaxed))return value;
    }
}
CampaignProfile load_profile(const std::string& path,Bytes& legacy) {
    require(!path.empty() && path.find('\0')==std::string::npos,
            "native profile destination is invalid");
    struct stat info{};
    if(stat(path.c_str(),&info)!=0) {
        const auto error=errno;
        if(error==ENOENT)return {};
        throw FormatError(std::string("cannot inspect native campaign profile: ")+std::strerror(error));
    }
    require(S_ISREG(info.st_mode),"native campaign profile is not a regular file");
    auto bytes=read_file(path,4096);
    auto profile=decode_campaign_profile(bytes);
    if(bytes.at(8)==1U)legacy=std::move(bytes);
    return profile;
}
bool same_random(const GameRandom::State& a,const GameRandom::State& b) noexcept {
    return a.words==b.words && a.index==b.index && a.draws==b.draws;
}
bool same_identity(const WorldUnit& a,const WorldUnit& b) noexcept {
    return a.type==b.type && a.faction==b.faction && a.owner==b.owner && a.active==b.active &&
           a.form==b.form && a.x==b.x && a.y==b.y && a.spent_movement==b.spent_movement;
}
} // namespace

CampaignController::CampaignController(std::string destination)
    : destination_(std::move(destination)) {
    profile_=load_profile(destination_,legacy_profile_bytes_);
}

SaveWriteResult CampaignController::preserve_legacy_profile() {
    if(legacy_profile_bytes_.empty())return {true,{}};
    const auto backup=destination_+".v1-backup";
    struct stat info{};
    if(lstat(backup.c_str(),&info)==0) {
        if(!S_ISREG(info.st_mode))return {false,"native profile backup is not a regular file"};
        try {
            if(read_file(backup,4096)==legacy_profile_bytes_)return {true,{}};
        } catch(const std::exception& error) { return {false,error.what()}; }
        return {false,"existing native profile backup differs; original profile preserved"};
    }
    if(errno!=ENOENT)return {false,std::string("cannot inspect native profile backup: ")+std::strerror(errno)};
    return write_native_save_atomic(backup,legacy_profile_bytes_,false);
}

SaveWriteResult CampaignController::consume_support_tutorial() {
    require(state_.gate==CampaignControllerGate::idle && state_.token==0U,
            "support tutorial cannot change a pending campaign handoff");
    if(!profile_.support_tutorial_pending)return {true,{}};
    auto next=profile_;
    next.support_tutorial_pending=false;
    const auto backup=preserve_legacy_profile();
    if(!backup.succeeded)return backup;
    const auto written=write_native_save_atomic(destination_,encode_campaign_profile(next));
    if(written.succeeded){profile_=std::move(next);legacy_profile_bytes_.clear();}
    return written;
}

void CampaignController::require_gate(std::uint64_t token,CampaignControllerGate gate) const {
    require(token!=0U && token==state_.token && state_.gate==gate,
            "campaign response does not match the pending request");
}

SaveWriteResult CampaignController::begin(const GameSession& session) {
    require(state_.gate==CampaignControllerGate::idle && state_.token==0U,
            "campaign controller already owns a handoff cycle");
    const auto& mission=session.mission();
    const auto& world=session.world();
    require(mission.gate==MissionGate::stopped && mission.stop==MissionStop::campaign_handoff_required &&
            mission.opcode==10U && mission.interaction_mode==6U && !session.moving() &&
            !session.battle() && !session.world_presentation(),
            "campaign save requires the actual stopped opcode10 gate with prior work completed");
    require(mission.command_index<world.commands.size(),"campaign command cursor is outside its script");
    const auto& command=world.commands[mission.command_index];
    require(command.opcode==10U && command.instruction_index==mission.command_index &&
            command.argument_words_u16.size()==1U,"campaign handoff command is malformed");
    const auto campaign=session.campaign_context();
    require(world.mission_resource_id==mission_resource_for_campaign(campaign.group,campaign.mission_index) &&
            world.player_factions==campaign.player_factions,
            "campaign save context does not match the actual session");
    const auto next=signed16(command.argument_words_u16[0]);
    const auto record=session.mission_record();
    auto update=prepare_campaign_profile_update(profile_,campaign,next,record);
    auto bytes=encode_campaign_profile(update.profile);

    // Commit the request only after all decoded inputs and serialized output
    // have validated. Neither validation nor preparation touches the file.
    state_.token=next_request_token();
    state_.command_index=mission.command_index;
    source_campaign_=campaign;source_record_=record;next_index_=next;
    source_random_=session.random();
    if(session.terrain_tables())source_terrain_tables_=*session.terrain_tables();
    prepared_update_=std::move(update);prepared_bytes_=std::move(bytes);
    return attempt_save();
}

SaveWriteResult CampaignController::attempt_save() {
    require(prepared_update_.has_value() && source_random_.has_value() && !prepared_bytes_.empty(),
            "campaign save has no prepared transaction");
    require(state_.save_attempts!=std::numeric_limits<std::uint32_t>::max(),
            "campaign save retry counter exhausted");
    ++state_.save_attempts;
    auto written=preserve_legacy_profile();
    if(written.succeeded)written=write_native_save_atomic(destination_,prepared_bytes_);
    state_.profile_persisted=written.succeeded;
    state_.save_error=written.error;
    if(!written.succeeded) {
        state_.gate=CampaignControllerGate::save_failed;
        state_.results.reset();state_.handoff.reset();
        return written;
    }
    profile_=prepared_update_->profile;
    legacy_profile_bytes_.clear();
    if(campaign_save_disposition(source_campaign_.group,true)==CampaignSaveDisposition::present_results) {
        CampaignResultsPresentation results;
        results.campaign=source_campaign_;results.current_record=source_record_;
        results.previous_record=prepared_update_->previous_record;
        results.best_record=profile_.records.at(source_campaign_.group).at(
            static_cast<std::size_t>(source_campaign_.mission_index));
        results.finished_campaign=prepared_update_->finished_campaign;
        state_.results=results;
        state_.gate=CampaignControllerGate::results;
    } else {
        // Original group2 directly calls57c98 after a successful save.
        // Requiring a new results screen here would invent a gameplay gate.
        request_handoff();
    }
    return written;
}

SaveWriteResult CampaignController::retry_save(std::uint64_t token) {
    require_gate(token,CampaignControllerGate::save_failed);
    return attempt_save();
}

void CampaignController::report_results_presented(std::uint64_t token,std::int32_t height) {
    require_gate(token,CampaignControllerGate::results);
    require(state_.profile_persisted && state_.results.has_value(),"results lack a persisted campaign profile");
    (void)advance_mission_results(0,height,0,0); // Checked original Q16.16 period.
    state_.results->background_image_height=height;
    state_.results->drawn=true;
}

ResultsUpdate CampaignController::update_results(std::uint64_t token,std::uint32_t edges,
                                                 std::uint32_t delta_ms) {
    require_gate(token,CampaignControllerGate::results);
    require(state_.profile_persisted && state_.results && state_.results->drawn,
            "results input requires this request's actual completed host drawing");
    const auto update=advance_mission_results(state_.results->background_offset_q16,
        state_.results->background_image_height,edges,delta_ms);
    state_.results->background_offset_q16=update.background_offset_q16;
    if(update.request_handoff)request_handoff();
    return update;
}

void CampaignController::request_handoff() {
    require(state_.profile_persisted && prepared_update_.has_value(),
            "campaign handoff requires successful real persistence");
    const auto request=campaign_handoff(source_campaign_,next_index_,prepared_update_->finished_campaign);
    state_.handoff=request;
    switch(request.kind) {
    case CampaignHandoffKind::next_mission:state_.gate=CampaignControllerGate::next_mission;break;
    case CampaignHandoffKind::campaign_menu:state_.gate=CampaignControllerGate::campaign_menu;break;
    case CampaignHandoffKind::campaign_ending:state_.gate=CampaignControllerGate::campaign_ending;break;
    }
}

PreparedCampaign CampaignController::prepare_next_campaign(std::uint64_t token,const Archive& archive,
                                                           bool config_immunity) {
    require_gate(token,CampaignControllerGate::next_mission);
    require(state_.handoff && state_.handoff->next_index && state_.handoff->next_mission_resource &&
            source_random_.has_value(),"next campaign request is incomplete");
    if(prepared_next_) {
        require(config_immunity==prepared_config_immunity_,"prepared campaign configuration cannot change mid-request");
        return *prepared_next_;
    }
    auto context=source_campaign_;
    context.mission_index=*state_.handoff->next_index;
    context.active_unit_counts={{0,0}};context.type_12_active=false;
    auto bootstrap=bootstrap_campaign(archive,context,*source_random_,config_immunity,
        source_terrain_tables_ ? &*source_terrain_tables_ : nullptr);
    require(bootstrap.world.mission_resource_id==*state_.handoff->next_mission_resource &&
            (bootstrap.world.stop==BootstrapStop::execution_required ||
             bootstrap.world.stop==BootstrapStop::presentation_required),
            "next campaign bootstrap did not reach a supported unconsumed execution boundary");
    prepared_next_=PreparedCampaign{context,std::move(bootstrap)};
    prepared_config_immunity_=config_immunity;
    return *prepared_next_;
}

void CampaignController::report_next_session_installed(std::uint64_t token,const GameSession& installed) {
    require_gate(token,CampaignControllerGate::next_mission);
    require(prepared_next_.has_value(),"next session was not prepared by this campaign request");
    const auto& expected=*prepared_next_;
    const auto& world=installed.world();
    const auto actual=installed.campaign_context();
    require(actual.group==expected.context.group && actual.mission_index==expected.context.mission_index &&
            actual.player_factions==expected.context.player_factions &&
            actual.player_controllers==expected.context.player_controllers &&
            world.mission_resource_id==expected.bootstrap.world.mission_resource_id &&
            world.grid_resource_id==expected.bootstrap.world.grid_resource_id &&
            world.style==expected.bootstrap.world.style &&
            installed.mission().command_index==expected.bootstrap.world.next_command_index &&
            installed.mission().opcode==expected.bootstrap.world.commands.at(expected.bootstrap.world.next_command_index).opcode,
            "installed session does not match the prepared campaign boundary");
    require(same_random(installed.random().state(),expected.bootstrap.random_after),
            "installed campaign did not preserve the prepared shared random stream");
    require(world.units.size()==expected.bootstrap.world.units.size(),"installed campaign roster size differs");
    for(std::size_t i=0;i<world.units.size();++i)
        require(same_identity(world.units[i],expected.bootstrap.world.units[i]),"installed campaign roster identity differs");
    std::array<std::uint8_t,2> counts{};
    for(const auto& effect:expected.bootstrap.world.initialization_effects)
        if(effect.kind==WorldInitializationEffectKind::debit_spawn_funds) {
            require(effect.owner<2U,"prepared activation count has no owner");
            counts[effect.owner]=static_cast<std::uint8_t>(counts[effect.owner]+effect.active_count_increment);
        }
    require(actual.active_unit_counts==counts,"installed campaign activation counters differ");
    // Do not call report_world_presented, report_camera or any other session
    // acknowledgement here. The actual host still owns those independent gates.
    state_.gate=CampaignControllerGate::handed_off;
}

void CampaignController::report_menu_presented(std::uint64_t token) {
    require_gate(token,CampaignControllerGate::campaign_menu);
    state_.gate=CampaignControllerGate::handed_off;
}

void CampaignController::report_ending_presented(std::uint64_t token) {
    require_gate(token,CampaignControllerGate::campaign_ending);
    state_.gate=CampaignControllerGate::handed_off;
}

} // namespace g1
