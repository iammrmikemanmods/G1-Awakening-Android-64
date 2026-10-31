#include "g1/SessionSave.hpp"

#include <iostream>

// Original data, real native state writes, explicitly synthetic presentation
// feedback. Stops at the first live gameplay objective in each script; never
// changes a unit, ownership or objective to manufacture mission completion.
int main(int argc,char** argv) {
    if(argc!=2 && argc!=3){std::cerr<<"usage: session_campaign_probe iphone.big [terrain-tables.bin]\n";return 2;}
    try {
        const g1::Archive archive(g1::read_file(argv[1]));
        const auto tables=argc==3 ? std::optional<g1::TerrainTables>(g1::decode_terrain_tables(g1::read_file(argv[2]))) : std::nullopt;
        std::size_t total{},operations{},empty_initial{};
        std::cout<<"{\"presentation\":\"synthetic headless acknowledgements\",\"playthrough\":false,\"entries\":[";
        for(std::uint8_t group=0;group<3;++group)for(std::size_t index=0;index<g1::campaign_mission_count(group);++index)
        for(std::uint8_t variant=0;variant<(group==2?4:1);++variant) {
            g1::CampaignContext context;context.group=group;context.mission_index=static_cast<std::int32_t>(index);
            if(group==2) {
                context.player_factions=variant<2 ? std::array<std::uint8_t,2>{{0,1}} : std::array<std::uint8_t,2>{{1,0}};
                context.player_controllers=variant%2==0 ? std::array<std::int8_t,2>{{0,1}} : std::array<std::int8_t,2>{{0,0}};
            }
            const g1::GameRandom rng(0x4741U);
            auto boot=g1::bootstrap_campaign(archive,context,rng,false,tables ? &*tables : nullptr);
            if(boot.world.units.empty())++empty_initial;
            g1::GameSession session(archive,std::move(boot),context);
            std::size_t presentations{},steps{};
            for(;steps<5000;++steps) {
                if(session.world_presentation()) {
                    operations+=session.world_presentation()->operations.size();++presentations;
                    session.report_world_presented(session.world_presentation()->token);
                }
                const auto& state=session.mission();
                if(state.gate==g1::MissionGate::objective || state.gate==g1::MissionGate::captured_site_item)break;
                if(state.stop!=g1::MissionStop::none)
                    throw g1::FormatError("campaign entry stopped before its first objective: "+state.stop_detail);
                if(state.gate==g1::MissionGate::camera)session.report_camera(state.command_index,true);
                g1::MissionTickInput input;
                if(state.gate==g1::MissionGate::dialogue)
                    input={16,g1::DialogueLayout{1,16,14,3,40,true}};
                (void)session.tick(30,input);
            }
            if(steps==5000 || session.world_presentation())throw g1::FormatError("campaign entry did not reach its real objective");
            const auto counts=session.campaign_context().active_unit_counts;
            const auto checkpoint=g1::encode_session_checkpoint(session.make_checkpoint());
            auto restored=g1::GameSession::restore_checkpoint(archive,g1::decode_session_checkpoint(archive,checkpoint,tables ? &*tables : nullptr));
            if(restored->world_presentation())restored->report_world_presented(restored->world_presentation()->token);
            if(restored->focus_request())restored->report_focus_presented(restored->focus_request()->token);
            if(g1::encode_session_checkpoint(restored->make_checkpoint())!=checkpoint)
                throw g1::FormatError("campaign checkpoint cold reconstruction changed native state");
            if(total++)std::cout<<',';
            std::cout<<"{\"group\":"<<unsigned(group)<<",\"index\":"<<index
                <<",\"variant\":"<<unsigned(variant)<<",\"player_factions\":["<<unsigned(context.player_factions[0])<<','<<unsigned(context.player_factions[1])
                <<"],\"player_controllers\":["<<int(context.player_controllers[0])<<','<<int(context.player_controllers[1])<<']'
                <<",\"resource\":"<<session.world().mission_resource_id<<",\"objective_command\":"<<session.mission().command_index
                <<",\"native_ticks\":"<<steps<<",\"world_presentations\":"<<presentations
                <<",\"unique_units\":"<<session.world().units.size()<<",\"counts\":["<<unsigned(counts[0])<<','<<unsigned(counts[1])
                <<"],\"funds\":["<<session.turn().funds[0]<<','<<session.turn().funds[1]<<"]"
                <<",\"rng_draws\":"<<session.random().state().draws
                <<",\"terrain_slots\":"<<(session.terrain_state()?session.terrain_state()->slots.size():0)
                <<",\"terrain_sprite_slots\":"<<(session.terrain_sprite_state()?session.terrain_sprite_state()->slot_phases.size():0)
                <<",\"terrain_sprite_count\":"<<(session.terrain_sprite_state()?session.terrain_sprite_state()->sprites.size():0)
                <<",\"checkpoint_bytes\":"<<checkpoint.size()<<",\"cold_resume_equal\":true}";
        }
        std::cout<<"],\"verified_entries\":"<<total<<",\"applied_world_operations\":"<<operations
            <<",\"empty_initial_rosters\":"<<empty_initial<<",\"checkpoint_round_trips\":"<<total
            <<",\"unique_mission_scripts\":30,\"arena_variants_per_script\":4}\n";
    }catch(const std::exception& e){std::cerr<<"session_campaign_probe: "<<e.what()<<'\n';return 1;}
}
