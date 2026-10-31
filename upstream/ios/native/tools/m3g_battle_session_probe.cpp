#include "g1/GameSession.hpp"
#include "g1/M3GBattlePresentation.hpp"
#include "g1/WorldCommands.hpp"

#include <cstring>
#include <iostream>

namespace {
void require(bool condition,const char* message) { if(!condition)throw g1::FormatError(message); }
std::size_t unit(const g1::GameSession& session,std::uint8_t type) {
    for(std::size_t i=0;i<session.world().units.size();++i)
        if(session.world().units[i].type==type)return i;
    throw g1::FormatError("required source mission unit is absent");
}
// Explicit test feedback only. This CPU integration probe is NOT evidence of
// UIKit dialogue, tactical GPU focus or transform presentation on a device.
void synthetic_setup_feedback(g1::GameSession& session) {
    if(session.world_presentation())session.report_world_presented(session.world_presentation()->token);
    if(session.focus_request())session.report_focus_presented(session.focus_request()->token);
    if(session.mission().gate==g1::MissionGate::camera)session.report_camera(session.mission().command_index,true);
    for(std::size_t i=0;i<session.units().size();++i) {
        if(session.transform_animation_pending(i))session.clear_transform_animation_request(i);
        if(session.unit_transform_waiting(i))session.report_transform_finished(i,session.unit_transform_generation(i));
    }
}
std::uint64_t frame_hash(const g1::M3GBattleFrame& frame) {
    std::uint64_t hash=14695981039346656037ULL;
    const auto word=[&](std::uint32_t value) {
        for(unsigned byte=0;byte<4;++byte){hash^=(value>>(byte*8U))&255U;hash*=1099511628211ULL;}
    };
    const auto scalar=[&](float value) {std::uint32_t bits;std::memcpy(&bits,&value,4);word(bits);};
    for(const auto& pass:frame.submissions) {
        word(static_cast<std::uint32_t>(pass.asset_index));
        for(float value:pass.frame.metal_view_projection)scalar(value);
        word(static_cast<std::uint32_t>(pass.frame.draws.size()));
        for(const auto& draw:pass.frame.draws) {
            word(static_cast<std::uint32_t>(draw.kind));word(static_cast<std::uint32_t>(draw.node_object));
            scalar(draw.effective_alpha);for(float value:draw.metal_matrix)scalar(value);
            if(draw.kind==g1::M3GDrawableKind::sprite)
                for(const auto& vertex:draw.sprite_vertices)
                    for(float value:vertex.position)scalar(value);
        }
    }
    return hash;
}
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"Usage: m3g_battle_session_probe ARCHIVE TERRAIN_TABLES");
        const g1::Archive archive(g1::read_file(argv[1]));
        const auto tables=g1::decode_terrain_tables(g1::read_file(argv[2],480));
        const g1::CampaignContext context;
        const g1::GameRandom random(0x4741U);
        g1::GameSession session(archive,g1::bootstrap_campaign(archive,context,random,false,&tables),context);
        unsigned stage=0;std::size_t ticks=0;
        for(;ticks<500 && !session.battle();++ticks) {
            synthetic_setup_feedback(session);
            const bool admitted=session.mission().command_index==18 && session.mission().interaction_mode==0 &&
                !session.moving() && !session.focus_request() && !session.world_presentation() && session.current_player()==0;
            if(admitted) {
                const auto actor=unit(session,11);
                if(stage==0) {
                    require(session.select(actor),"first unit selection rejected");
                    require(session.transform_selected().has_value(),"first transform rejected");++stage;
                } else if(stage==1 && !session.unit_transform_waiting(actor)) {
                    (void)session.move_selected_to({3,5});require(session.moving(),"first actual move rejected");++stage;
                } else if(stage==2) {
                    require(session.end_turn().has_value(),"actual end turn rejected");++stage;
                } else if(stage==3 && session.turn().human_turn_counter_274>=2) {
                    require(session.select(actor),"second unit selection rejected");
                    (void)session.move_selected_to({4,7});require(session.moving(),"second actual move rejected");++stage;
                } else if(stage==4) {
                    require(session.transform_selected().has_value(),"second transform rejected");++stage;
                } else if(stage==5 && !session.unit_transform_waiting(actor)) {
                    require(session.attack_selected(unit(session,19)),"actual attack rejected");++stage;
                }
            }
            if(session.battle())break;
            g1::MissionTickInput input;
            if(session.mission().gate==g1::MissionGate::dialogue)
                input={16,g1::DialogueLayout{1,16,14,3,40,true}};
            (void)session.tick(30,input);
        }
        require(session.battle().has_value(),"source mission did not produce a real pending battle");
        const auto battle=*session.battle();const auto rng=session.random().state();
        require(session.combat_units().at(battle.attacker).health==95 &&
                session.combat_units().at(battle.defender).health==50,"real seeded exchange differs from recorded native pilot");
        g1::M3GBattlePresentation presentation(archive,{battle.token,session.world().style,
            battle.attacker_before,battle.defender_before,battle.exchange});
        std::vector<const g1::M3GRenderAsset*> assets;
        for(const auto& asset:presentation.assets())assets.push_back(&asset.render);
        std::size_t updates=0,frames=0,interpolated=0,read_only_checks=0;
        (void)presentation.take_audio_requests();
        const auto prepare=[&](float blend) {
            ++frames;return presentation.prepare_frame({1792,828},assets,blend,1792.0F/828.0F);
        };
        (void)prepare(1);
        while(!presentation.status().complete && updates<2000) {
            presentation.advance(30);++updates;(void)presentation.take_audio_requests();
            const auto current=presentation.status();
            if(updates%10==0 || current.complete) {
                const auto a=frame_hash(prepare(0)),b=frame_hash(prepare(0.5F)),c=frame_hash(prepare(1));
                require(b==frame_hash(prepare(0.5F)),"read-only render sampling is nondeterministic");
                require(presentation.status().revision==current.revision && presentation.status().phase==current.phase &&
                    presentation.status().phase_time_ms==current.phase_time_ms &&
                    presentation.status().display_health==current.display_health &&
                    presentation.take_audio_requests().empty(),"render interpolation advanced controller or audio");
                ++read_only_checks;if(a!=b && b!=c && a!=c)++interpolated;
            }
            require(session.battle() && session.battle()->token==battle.token,"adapter acknowledged gameplay without a host report");
        }
        require(presentation.status().complete && interpolated>0,"battle did not complete with distinct interpolated poses");
        const auto final=presentation.status();presentation.advance(30);
        require(presentation.status().revision==final.revision && presentation.take_audio_requests().empty(),
                "completed controller restarted or emitted audio");
        require(session.random().state().words==rng.words && session.random().state().index==rng.index &&
                session.random().state().draws==rng.draws,"presentation changed the game's random stream");
        // This is explicitly a synthetic CPU host acknowledgement AFTER the
        // complete prepared final frame. Only the Apple host uses displayed GPU
        // completion; this call cannot establish that hardware gate.
        session.report_battle_finished(battle.token);
        require(!session.battle(),"matching host completion did not release native battle token");
        std::cout<<"{\"source\":\"real GameSession exchange; synthetic startup/host feedback; no GPU\","
            <<"\"setup_ticks\":"<<ticks<<",\"actual_attacker_type\":"<<unsigned(battle.attacker_before.type)
            <<",\"actual_defender_type\":"<<unsigned(battle.defender_before.type)
            <<",\"attacker_health_after\":95,\"defender_health_after\":50,\"battle_updates\":"<<updates
            <<",\"prepared_frames\":"<<frames<<",\"read_only_sample_checks\":"<<read_only_checks
            <<",\"distinct_interpolation_cases\":"<<interpolated
            <<",\"rng_unchanged\":true,\"token_retained_until_host_report\":true,\"complete\":true}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
