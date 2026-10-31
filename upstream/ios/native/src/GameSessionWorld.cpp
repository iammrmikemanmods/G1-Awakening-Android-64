#include "g1/GameSession.hpp"

#include <algorithm>

namespace g1 {
namespace {
GameRandom restored_random(const GameRandom::State& state) {
    GameRandom result(0);result.restore(state);return result;
}
std::int8_t signed_byte(std::uint8_t value) noexcept {
    return static_cast<std::int8_t>(value<128U ? int(value) : int(value)-256);
}
bool needs_presentation(const WorldCommandPlan& plan) {
    return std::holds_alternative<WorldMapInstall>(plan.data) ||
           std::holds_alternative<WorldSpawnCommand>(plan.data) ||
           std::holds_alternative<WorldSitesCommand>(plan.data) ||
           std::holds_alternative<WorldPointsCommand>(plan.data) ||
           std::holds_alternative<WorldMarkerCommand>(plan.data) ||
           (std::holds_alternative<WorldPhaseCommand>(plan.data) &&
            std::get<WorldPhaseCommand>(plan.data).title_text_id.has_value());
}
}

GameSession::GameSession(const Archive& archive,CampaignBootstrap bootstrap,CampaignContext campaign)
    : GameSession(std::move(bootstrap.world),restored_random(bootstrap.random_after),
                  std::move(campaign),std::move(bootstrap.unit_activations)) {
    archive_=&archive;
    terrain_=std::move(bootstrap.terrain);
    if(!bootstrap.applied_commands.empty())
        world_presentation_=SessionWorldPresentation{next_world_token_++,{},std::move(bootstrap.applied_commands)};
}

void GameSession::report_world_presented(std::uint64_t token) {
    if(!world_presentation_ || world_presentation_->token!=token)
        throw FormatError("stale native world presentation response");
    if(world_presentation_->command_index)
        mission_.report_world_command_applied(*world_presentation_->command_index);
    world_presentation_.reset();
}

void GameSession::apply_world_command(const WorldCommandPlan& plan) {
    if(moving_ || battle_)throw FormatError("world command cannot replace a live action");
    clear_ai_action();clear_selection();focus_.reset();
    if(const auto phase=std::get_if<WorldPhaseCommand>(&plan.data)) {
        if(phase->phase_flag_225)world_.flag_225=*phase->phase_flag_225;
        if(phase->title_text_index)world_.title_text_index=*phase->title_text_index;
    }else if(const auto map=std::get_if<WorldMapInstall>(&plan.data)) {
        world_.grid_resource_id=map->grid_resource_id;world_.lookup_resource_id=map->lookup_resource_id;
        world_.style=map->style;world_.grid=map->grid;world_.effective_lookup=map->effective_lookup;
        if(terrain_) {
            terrain_=create_terrain_runtime(*archive_,world_,terrain_->tables,random_);
            ++terrain_generation_;
        }
        world_.sites.clear();world_.site_points.clear();world_.cost99_regions.clear();
        world_.marker_254={{-1,-1}};world_.marker_25c={{-1,-1}};
        world_.camera=WorldCameraRequest{0,0,true};
        captures_=initial_capture_network({});
        active_counts_={{0,0}};upgrade_slots_={{-1,-1}};death_log_.clear();
        original_transform_gate_=false;
        // Original43ac8(false) deactivates without clearing HP, forms, action,
        // spent movement, coordinates, timers or pending animation byte2e.
        for(std::size_t i=0;i<units_.size();++i) {
            units_[i].active=false;combat_[i].active=false;world_.units[i].active=false;
            ++spawn_generation_[i];++transform_generation_[i];
            // Native renderer requests from the old map are invalidated. An
            // unconsumed original2e request is still retained for reactivation.
            transform_waiting_[i]=animation_pending_[i]!=0;
        }
        turn_.current_player=0;
        begin_current_player_turn(captures_,{{signed_byte(world_.player_factions[0]),
                                             signed_byte(world_.player_factions[1])}},turn_);
        ++world_generation_;
    }else if(const auto spawn=std::get_if<WorldSpawnCommand>(&plan.data)) {
        const auto index=find_or_create_unit(spawn->identity.type,spawn->identity.owner);
        const auto activated=activate_unit(world_.unit_definitions.at(spawn->identity.type),spawn->identity,
            spawn->destination,spawn->terrain0,spawn->terrain2,units_[index],combat_[index],spawn->modifiers);
        commit_activation(index,activated);
        if(spawn->random_choice)random_.restore(spawn->random_choice->after);
        initialize_upgrade_slots();
    }else if(const auto sites=std::get_if<WorldSitesCommand>(&plan.data)) {
        const auto income=captures_.counters_68;
        auto network=initial_capture_network(sites->sites);network.counters_68=income;
        world_.sites=sites->sites;captures_=std::move(network);
    }else if(const auto availability=std::get_if<WorldAvailabilityCommand>(&plan.data)) {
        world_.available_types=availability->available;
    }else if(const auto flag=std::get_if<WorldCombatFlagCommand>(&plan.data)) {
        world_.flag_227=flag->value;
    }else if(const auto points=std::get_if<WorldPointsCommand>(&plan.data)) {
        for(const auto& point:points->points) {
            world_.sites.at(point.site_index).state=1;
            captures_.sites.at(point.site_index).state=1;
        }
        world_.site_points=points->points;
    }else if(const auto marker=std::get_if<WorldMarkerCommand>(&plan.data)) {
        auto& stored=marker->slot==WorldMarkerSlot::field_254 ? world_.marker_254 : world_.marker_25c;
        stored={{marker->x,marker->y}};
    }else if(const auto immunity=std::get_if<WorldImmunityCommand>(&plan.data)) {
        const auto type=immunity->type;
        const auto faction=world_.unit_definitions.at(type).faction();
        std::uint8_t owner=0;
        if(world_.player_factions[0]!=faction) {
            if(world_.player_factions[1]!=faction)throw FormatError("immunity unit faction has no player");
            owner=1;
        }
        const auto index=find_or_create_unit(type,owner);
        combat_[index].damage_immune=immunity->value;
        world_.units[index].spawn.damage_immune=immunity->value;
    }else if(!std::holds_alternative<WorldOriginalNoEffect>(plan.data)) {
        throw FormatError("unknown native world-operation variant");
    }
    world_.last_phase_opcode=static_cast<std::uint16_t>(plan.phase_after.value_or(99));
}

void GameSession::process_world_command(SessionTick& tick) {
    if(!archive_)throw FormatError("live mission world commands require the original Archive data");
    const auto& command=mission_.pending_world_command();
    auto plan=prepare_world_command(*archive_,world_,command,campaign_context(),&random_,config_damage_immunity_);
    apply_world_command(plan);tick.world_changed=true;
    if(needs_presentation(plan))
        world_presentation_=SessionWorldPresentation{next_world_token_++,command.instruction_index,{std::move(plan)}};
    else mission_.report_world_command_applied(command.instruction_index);
}

void GameSession::tick_terrain(std::uint32_t delta_ms,SessionTick& result) {
    if(!terrain_ || mission_.state().phase_flag_225 ||
       mission_.state().stop!=MissionStop::none)return;
    bool changed=false;
    if(terrain_->state) {
        const auto step=advance_terrain_decorations(*terrain_->layout,*terrain_->state,delta_ms,random_);
        changed=step.phase_advanced;
    } else if(terrain_->sprite_state) {
        const auto& layout=*terrain_->sprite_layout;
        const auto& bank=*terrain_->sprite_animations;
        auto& state=*terrain_->sprite_state;
        const auto frame=[&](std::size_t i) {
            const auto& sprite=state.sprites[i];
            return bank[layout.selectors[i]-73U][sprite.animation_index][sprite.playback.step_index].frame_index;
        };
        std::vector<std::uint8_t> before;before.reserve(state.sprites.size());
        for(std::size_t i=0;i<state.sprites.size();++i)before.push_back(frame(i));
        const auto step=advance_terrain_sprite_decorations(layout,bank,state,delta_ms,random_);
        changed=!step.relocated_slots.empty();
        for(std::size_t i=0;i<state.sprites.size();++i)
            changed=changed || (state.sprites[i].visible && before[i]!=frame(i));
    }
    if(changed){++terrain_generation_;result.terrain_changed=true;}
}
} // namespace g1
