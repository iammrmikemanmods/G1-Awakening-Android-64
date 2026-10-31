#include "g1/WorldCommands.hpp"
#include "g1/MissionExecution.hpp"

#include <iostream>

// Uses the user's local archive only. This checks resource associations and
// bootstrap requests; it neither advances objectives nor simulates playthrough.
int main(int argc,char** argv) {
    if(argc!=2) {std::cerr<<"usage: world_commands_probe /path/to/iphone.big\n";return 2;}
    try {
        const g1::Archive archive(g1::read_file(argv[1]));
        const g1::GameRandom random(0x4741U);
        std::size_t total{},empty_rosters{},placements{};
        std::cout<<"{\"seed\":18241,\"campaigns\":[";
        for(std::uint8_t group=0;group<3;++group) {
            for(std::size_t index=0;index<g1::campaign_mission_count(group);++index) {
                g1::CampaignContext context;context.group=group;context.mission_index=static_cast<std::int32_t>(index);
                const auto boot=g1::bootstrap_campaign(archive,context,random);
                const auto& world=boot.world;
                if(world.stop!=g1::BootstrapStop::execution_required || world.next_command_index>=world.commands.size() ||
                    world.commands[world.next_command_index].opcode!=4U || world.units.size()!=boot.unit_activations.size())
                    throw g1::FormatError("actual campaign prefix failed its unconsumed camera boundary");
                g1::MissionExecution execution(world,context);
                if(execution.state().gate!=g1::MissionGate::camera || !execution.take_initial_effects().empty())
                    throw g1::FormatError("actual campaign boundary did not become a real camera request");
                const auto board=world.movement_board();
                if(board.cells.size()!=world.grid.tiles.size())throw g1::FormatError("bootstrap board size mismatch");
                std::array<unsigned,2> counts{};std::size_t activation_events{};
                for(const auto& effect:world.initialization_effects) {
                    if(effect.kind==g1::WorldInitializationEffectKind::debit_spawn_funds) {
                        counts.at(effect.owner)+=effect.active_count_increment;++activation_events;
                    }
                }
                for(std::size_t i=0;i<world.units.size();++i) {
                    const auto& unit=world.units[i];const auto& a=boot.unit_activations[i];
                    if(unit.type!=a.world.type || unit.form!=a.movement.form || unit.form!=a.combat.form ||
                       unit.x!=a.movement.tile_x || unit.y!=a.movement.tile_y || !unit.active || !a.movement.active ||
                       !a.combat.active || a.combat.type!=unit.type || a.combat.health!=unit.spawn.health)
                        throw g1::FormatError("activation arrays are not aligned with unique world roster");
                }
                if(total++)std::cout<<',';
                std::cout<<"{\"group\":"<<unsigned(group)<<",\"index\":"<<index
                    <<",\"mission\":"<<world.mission_resource_id<<",\"grid\":"<<world.grid_resource_id
                    <<",\"style\":"<<unsigned(world.style)<<",\"width\":"<<world.grid.width<<",\"height\":"<<world.grid.height
                    <<",\"camera_command\":"<<world.next_command_index<<",\"unique_units\":"<<world.units.size()
                    <<",\"activation_events\":"<<activation_events<<",\"counts\":["<<counts[0]<<','<<counts[1]
                    <<"],\"sites\":"<<world.sites.size()<<",\"rng_draws\":"<<boot.random_after.draws<<'}';
                placements+=activation_events;if(world.units.empty())++empty_rosters;
            }
        }
        if(random.state().draws!=0)throw g1::FormatError("preparation changed caller RNG");
        std::cout<<"],\"verified_prefixes\":"<<total<<",\"empty_initial_rosters\":"<<empty_rosters
            <<",\"activation_events\":"<<placements<<",\"playthrough\":false}\n";
    } catch(const std::exception& error) {std::cerr<<"world_commands_probe: "<<error.what()<<'\n';return 1;}
}
