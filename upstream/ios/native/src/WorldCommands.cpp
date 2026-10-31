#include "g1/WorldCommands.hpp"

#include <algorithm>
#include <limits>

namespace g1 {
namespace {
void require(bool value, const char* message) {
    if (!value) throw FormatError(message);
}
std::int16_t signed16(std::uint16_t value) noexcept {
    return static_cast<std::int16_t>(value < 0x8000U ? static_cast<std::int32_t>(value) :
                                    static_cast<std::int32_t>(value) - 0x10000);
}
std::int32_t signed32(std::uint32_t value) noexcept {
    return static_cast<std::int32_t>(value < 0x80000000U ? static_cast<std::int64_t>(value) :
                                    static_cast<std::int64_t>(value) - 0x100000000LL);
}
void words(const MissionCommand& command, std::size_t count) {
    require(command.argument_words_u16.size() == count,
            "world command argument count differs from recovered format");
}
Bytes read(const Archive& archive, std::uint32_t id) {
    const auto entries = archive.find(id,0xf4e02223U);
    require(entries.size()==1U,"world command resource is missing or ambiguous");
    return archive.read(entries.front());
}
void validate_units(const std::vector<UnitDefinition>& definitions,
                    const std::vector<WorldUnit>& units) {
    require(definitions.size()==23U,"world commands require the 23 original type definitions");
    for (std::size_t i=0;i<definitions.size();++i)
        require(definitions[i].type==i && definitions[i].faction()<2U,
                "world type definitions have inconsistent indices/factions");
    std::array<bool,23> seen{};
    for (const auto& unit:units) {
        require(unit.type<seen.size() && !seen[unit.type] && unit.faction<2U && unit.owner<2U,
                "world roster has a duplicate/invalid type or owner");
        seen[unit.type]=true;
    }
}
const WorldUnit* existing(const std::vector<WorldUnit>& units, std::uint8_t type) {
    const auto found=std::find_if(units.begin(),units.end(),[type](const auto& u){return u.type==type;});
    return found==units.end()?nullptr:&*found;
}
std::uint8_t owner_for(std::array<std::uint8_t,2> factions, std::uint8_t faction) {
    require(factions[0]<2U && factions[1]<2U && factions[0]!=factions[1],
            "world commands require two distinct original faction slots");
    return factions[0]==faction?std::uint8_t(0):std::uint8_t(1);
}
void grid_ready(const WorldBootstrap& world) {
    require(world.grid.width>0U && world.grid.height>0U && world.grid.width<=255U &&
            world.grid.height<=255U && world.grid.tiles.size()==
                std::size_t(world.grid.width)*world.grid.height,"world command requires a complete grid");
}
WorldCoordinate coordinate(const WorldBootstrap& world, std::uint16_t x, std::uint16_t y) {
    grid_ready(world);
    require(x<world.grid.width && y<world.grid.height,"world command coordinate is outside the map");
    return {static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)};
}
std::array<std::uint8_t,2> terrain(const WorldBootstrap& world, WorldCoordinate p) {
    const auto tile=world.grid.tile_at(p.x,p.y);
    require(tile<world.effective_lookup.size(),"world placement tile has no terrain category");
    const auto category=world.effective_lookup[tile];
    require(category<world.terrain_rules.size(),"world placement category has no terrain rule");
    const auto& fields=world.terrain_rules[category].fields;
    return {{fields[0],fields[2]}};
}
WorldSitesCommand sites(const WorldBootstrap& world, const MissionCommand& command) {
    const auto& args=command.argument_words_u16;
    std::size_t cursor{};
    const auto word=[&]() {
        require(cursor<args.size(),"truncated world site command");
        return args[cursor++];
    };
    const auto count=word();
    require(count<0x8000U && count<=(args.size()-cursor)/3U,"invalid signed world site count");
    WorldSitesCommand result;
    for (std::size_t i=0;i<count;++i) {
        const auto x=word(), y=word(), groups=word();
        WorldSite site{coordinate(world,x,y),-1,{}};
        require(groups<0x8000U && groups<=args.size()-cursor,"invalid world site group count");
        for (std::size_t group=0;group<groups;++group) {
            const auto count_pairs=word();
            require(count_pairs<0x8000U && count_pairs<=(args.size()-cursor)/2U,
                    "invalid world site coordinate count");
            std::vector<WorldCoordinate> pairs;
            for (std::size_t pair=0;pair<count_pairs;++pair) {
                const auto px=word(), py=word();
                pairs.push_back(coordinate(world,px,py));
            }
            site.coordinate_groups.push_back(std::move(pairs));
        }
        result.sites.push_back(std::move(site));
    }
    require(cursor==args.size(),"world site command has trailing words");
    return result;
}
} // namespace

ScriptRandomChoice choose_random_script_unit(const std::vector<UnitDefinition>& definitions,
    const std::vector<WorldUnit>& units, std::uint8_t faction, const GameRandom& random) {
    validate_units(definitions,units);
    require(faction<2U,"random script faction is outside the original pair");
    bool candidate_exists=false;
    for (const auto& definition:definitions) {
        const auto unit=existing(units,definition.type);
        if (definition.faction()==faction && (!unit || !unit->active)) candidate_exists=true;
    }
    require(candidate_exists,"random script spawn has no inactive candidate; original would retry forever");
    auto copied=random;
    for (std::uint32_t draws=1;draws<=1000000U;++draws) {
        const auto remainder=static_cast<std::int64_t>(signed32(copied.next_word())) %
                             static_cast<std::int64_t>(definitions.size());
        const auto type=static_cast<std::uint8_t>(remainder<0?-remainder:remainder);
        const auto unit=existing(units,type);
        if (definitions[type].faction()==faction && (!unit || !unit->active))
            return {type,copied.state(),draws};
    }
    throw FormatError("random script spawn exceeded the explicit one-million-draw resource bound");
}

bool is_world_command(std::uint16_t opcode) noexcept {
    switch (opcode) {
    case 0: case 6: case 7: case 9: case 13: case 14: case 15: case 16:
    case 17: case 18: case 19: case 20: case 21: return true;
    default:return false;
    }
}

WorldCommandPlan prepare_world_command(const Archive& archive, const WorldBootstrap& world,
    const MissionCommand& command, const CampaignContext& campaign,
    const GameRandom* random, bool config_immunity) {
    require(is_world_command(command.opcode),"command is not a recovered world operation");
    require(world.mission_resource_id==mission_resource_for_campaign(campaign.group,campaign.mission_index) &&
            world.player_factions==campaign.player_factions,
            "world command campaign identity/faction mapping mismatch");
    validate_units(world.unit_definitions,world.units);
    const auto& args=command.argument_words_u16;
    WorldCommandPlan plan{command.instruction_index,command.opcode,std::nullopt,WorldOriginalNoEffect{}};
    switch (command.opcode) {
    case 0:
        words(command,0);
        plan.phase_after=0;
        plan.data=WorldPhaseCommand{true,std::nullopt,std::nullopt,1};
        break;
    case 16:
        words(command,1);
        plan.phase_after=16;
        plan.data=WorldPhaseCommand{std::nullopt,args[0],script_text_id(signed16(args[0])),2};
        break;
    case 9: {
        words(command,2);
        require(args[1]<=2U,"world command terrain style is unsupported");
        WorldMapInstall map;
        map.style=static_cast<std::uint8_t>(args[1]);
        map.grid_resource_id=mission_grid_resource(args[0]);
        map.lookup_resource_id=mission_lookup_resource(map.style);
        map.grid=decode_terrain_grid(read(archive,map.grid_resource_id));
        map.effective_lookup=apply_mission_style(
            decode_terrain_lookup(read(archive,map.lookup_resource_id)),map.style);
        // Original0x51e2a initializes style=-1. An unloaded native grid is its
        // typed cold-state equivalent; uint8 style alone cannot represent it.
        map.style_changed=world.grid.width==0U || world.grid.height==0U || world.style!=map.style;
        if (map.style_changed) {
            map.music_request=MissionAudioRequest{5,map.style==0U?1:map.style==2U?2:3};
            map.stop_music_if_config_a4_disabled=true;
        }
        plan.data=std::move(map);
        break;
    }
    case 6: {
        words(command,5);
        WorldSpawnCommand spawn;
        spawn.destination=coordinate(world,args[1],args[2]);
        const auto type_word=signed16(args[0]);
        std::uint8_t type{}, faction{};
        if (type_word==-1 || type_word==-2) {
            require(random!=nullptr,"random script spawn requires the session's actual generator");
            faction=type_word==-1?std::uint8_t(0):std::uint8_t(1);
            spawn.random_choice=choose_random_script_unit(world.unit_definitions,world.units,faction,*random);
            type=spawn.random_choice->type;
            const auto old=existing(world.units,type);
            // The actual randomized campaign has definition-aligned factions.
            // Do not silently normalize a contradictory custom saved object.
            require(!old || old->faction==faction,
                    "randomized placement has a previously inverted faction object");
        } else {
            require(type_word>=0 && type_word<23,"script placement type/sentinel is unsupported");
            type=static_cast<std::uint8_t>(type_word);
            faction=world.unit_definitions[type].faction();
            if (args[3]!=0U) faction=static_cast<std::uint8_t>(faction==0U?1U:0U);
        }
        if (const auto old=existing(world.units,type)) spawn.identity=*old;
        else {
            spawn.identity.type=type;
            spawn.identity.faction=world.unit_definitions[type].faction();
            spawn.identity.owner=owner_for(world.player_factions,spawn.identity.faction);
        }
        spawn.rebuild_owner_slots=spawn.identity.faction!=faction;
        spawn.identity.faction=faction;
        spawn.identity.owner=owner_for(world.player_factions,faction);
        spawn.identity.placement_command_index=command.instruction_index;
        const auto fields=terrain(world,spawn.destination);
        spawn.terrain0=fields[0]; spawn.terrain2=fields[1];
        spawn.modifiers={args[4]!=0U,config_immunity && campaign.player_controllers[spawn.identity.owner]==0};
        plan.phase_after=99;
        plan.data=std::move(spawn);
        break;
    }
    case 7:
        grid_ready(world);
        plan.phase_after=99;
        plan.data=sites(world,command);
        break;
    case 13: case 14:
        words(command,0);
        plan.phase_after=command.opcode;
        plan.data=WorldCombatFlagCommand{command.opcode==13U};
        break;
    case 15: {
        require(!args.empty() && args[0]<0x8000U && args.size()==std::size_t(args[0])+1U,
                "available-type command has an invalid signed count");
        WorldAvailabilityCommand availability;
        for (std::size_t i=1;i<args.size();++i) {
            require(args[i]<availability.available.size(),"available type is outside original table");
            availability.available[args[i]]=true;
        }
        // The original early return for an empty list precedes this override.
        if (args[0]!=0U && campaign.group==0U && world.mission_resource_id==333U)
            availability.available[4]=true;
        plan.data=availability;
        break;
    }
    case 17: {
        require(!args.empty() && args[0]<0x8000U && args.size()==1U+std::size_t(args[0])*3U,
                "point command has an invalid signed count");
        WorldPointsCommand points;
        for (std::size_t i=0;i<args[0];++i) {
            const auto at=1U+i*3U;
            const auto p=coordinate(world,args[at],args[at+1U]);
            require(args[at+2U]<world.sites.size(),"point references an absent site state");
            points.points.push_back({{p.x,p.y,1},args[at+2U]});
        }
        plan.data=std::move(points);
        break;
    }
    case 18:
        words(command,2);
        plan.phase_after=18;
        plan.data=WorldMarkerCommand{WorldMarkerSlot::field_254,signed16(args[0]),signed16(args[1]),2,false};
        break;
    case 19:
        words(command,2);
        plan.phase_after=19;
        // The original ignores these two parsed words and writes literal1,1.
        plan.data=WorldMarkerCommand{WorldMarkerSlot::field_25c,1,1,3,true};
        break;
    case 20:
        words(command,2);
        require(args[0]<23U,"immunity command type is outside original table");
        plan.phase_after=20;
        plan.data=WorldImmunityCommand{static_cast<std::uint8_t>(args[0]),args[1]!=0U};
        break;
    case 21:
        require(!args.empty() && args[0]<0x8000U && args.size()==1U+std::size_t(args[0])*4U,
                "original no-effect command has invalid count/framing");
        plan.data=WorldOriginalNoEffect{};
        break;
    default:
        throw FormatError("unhandled recovered world command");
    }
    return plan;
}

CampaignBootstrap bootstrap_campaign(const Archive& archive,const CampaignContext& campaign,
    const GameRandom& random,bool config_immunity,const TerrainTables* terrain_tables) {
    CampaignBootstrap result;
    auto& world=result.world;
    world.mission_resource_id=mission_resource_for_campaign(campaign.group,campaign.mission_index);
    world.player_factions=campaign.player_factions;
    (void)owner_for(world.player_factions,0);
    world.terrain_rules=decode_terrain_rules(read(archive,306));
    for (std::uint8_t type=0;type<23U;++type) {
        const auto id=unit_definition_resource(type);
        world.unit_definitions.push_back(decode_unit_definition(read(archive,id),type,id));
    }
    world.commands=MissionStream(read(archive,world.mission_resource_id)).commands();
    auto working_random=random;
    bool map_loaded=false;
    for (;world.next_command_index<world.commands.size();++world.next_command_index) {
        const auto& command=world.commands[world.next_command_index];
        if (!is_world_command(command.opcode)) {
            switch (command.opcode) {
            case 1:case 2:case 3:case 4:case 5:case 8:case 10:case 12:
                grid_ready(world);
                world.stop=BootstrapStop::execution_required;
                world.stop_detail="unconsumed presentation/control command requires live mission execution";
                if (command.opcode==4U) {
                    words(command,3);
                    world.camera=WorldCameraRequest{signed16(command.argument_words_u16[0]),
                        signed16(command.argument_words_u16[1]),command.argument_words_u16[2]!=0U};
                }
                result.random_after=working_random.state();
                return result;
            default:
                world.stop=BootstrapStop::unsupported_command;
                world.stop_detail="unrecovered bootstrap command is not consumed";
                result.random_after=working_random.state();
                return result;
            }
        }
        auto plan=prepare_world_command(archive,world,command,campaign,&working_random,config_immunity);
        if (const auto phase=std::get_if<WorldPhaseCommand>(&plan.data)) {
            if (phase->phase_flag_225) world.flag_225=*phase->phase_flag_225;
            if (phase->title_text_index) world.title_text_index=*phase->title_text_index;
        } else if (const auto map=std::get_if<WorldMapInstall>(&plan.data)) {
            require(!map_loaded && world.units.empty(),"general cold bootstrap cannot replace an initialized map");
            world.grid_resource_id=map->grid_resource_id;
            world.lookup_resource_id=map->lookup_resource_id;
            world.style=map->style;world.grid=map->grid;world.effective_lookup=map->effective_lookup;
            world.sites.clear();world.site_points.clear();world.cost99_regions.clear();
            world.marker_254={{-1,-1}};world.marker_25c={{-1,-1}};
            world.initialization_effects.push_back({WorldInitializationEffectKind::begin_current_player_turn,
                command.instruction_index,0,0,{},0});
            // Original opcode9 executes the terrain constructor during its
            // renderer-loading phases, before the following opcode6 spawns.
            if(terrain_tables)result.terrain=create_terrain_runtime(archive,world,*terrain_tables,working_random);
            map_loaded=true;
        } else if (const auto spawn=std::get_if<WorldSpawnCommand>(&plan.data)) {
            const auto found=std::find_if(world.units.begin(),world.units.end(),[&](const auto& u){return u.type==spawn->identity.type;});
            const auto index=static_cast<std::size_t>(found-world.units.begin());
            MovementUnitState previous_movement;previous_movement.active=false;
            CombatUnitState previous_combat;previous_combat.active=false;
            if (index<result.unit_activations.size()) {
                previous_movement=result.unit_activations[index].movement;
                previous_combat=result.unit_activations[index].combat;
            }
            auto activation=activate_unit(world.unit_definitions.at(spawn->identity.type),spawn->identity,
                spawn->destination,spawn->terrain0,spawn->terrain2,previous_movement,previous_combat,spawn->modifiers);
            if (index==world.units.size()) {
                world.units.push_back(activation.world);
                result.unit_activations.push_back(activation);
            } else {
                world.units[index]=activation.world;
                result.unit_activations[index]=activation;
            }
            world.initialization_effects.push_back({WorldInitializationEffectKind::debit_spawn_funds,
                command.instruction_index,activation.world.owner,activation.signed_cost,{},activation.active_count_increment});
            if (spawn->random_choice) working_random.restore(spawn->random_choice->after);
        } else if (const auto site=std::get_if<WorldSitesCommand>(&plan.data)) {
            world.sites=site->sites;
        } else if (const auto available=std::get_if<WorldAvailabilityCommand>(&plan.data)) {
            world.available_types=available->available;
        } else if (const auto flag=std::get_if<WorldCombatFlagCommand>(&plan.data)) {
            world.flag_227=flag->value;
        } else if (const auto points=std::get_if<WorldPointsCommand>(&plan.data)) {
            world.site_points=points->points;
            for (const auto& point:points->points) world.sites.at(point.site_index).state=1;
        } else if (const auto marker=std::get_if<WorldMarkerCommand>(&plan.data)) {
            auto& stored=marker->slot==WorldMarkerSlot::field_254?world.marker_254:world.marker_25c;
            stored={{marker->x,marker->y}};
        } else if (const auto immune=std::get_if<WorldImmunityCommand>(&plan.data)) {
            const auto found=std::find_if(world.units.begin(),world.units.end(),[&](const auto& u){return u.type==immune->type;});
            require(found!=world.units.end(),"cold bootstrap immunity references a not-yet-activated object");
            const auto index=static_cast<std::size_t>(found-world.units.begin());
            found->spawn.damage_immune=immune->value;
            result.unit_activations[index].world.spawn.damage_immune=immune->value;
            result.unit_activations[index].combat.damage_immune=immune->value;
        } else {
            require(std::holds_alternative<WorldOriginalNoEffect>(plan.data),"unknown world plan in bootstrap");
        }
        world.last_phase_opcode=static_cast<std::uint16_t>(plan.phase_after.value_or(99));
        result.applied_commands.push_back(std::move(plan));
    }
    world.stop=BootstrapStop::end_of_stream;
    world.stop_detail="stream ended without asserting campaign completion";
    result.random_after=working_random.state();
    return result;
}

} // namespace g1
