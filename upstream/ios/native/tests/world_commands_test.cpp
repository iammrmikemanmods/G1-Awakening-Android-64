#include "g1/WorldCommands.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
std::size_t checks{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& fn, const char* message) {
    try { fn(); } catch (const g1::FormatError&) { ++checks; return; }
    throw std::runtime_error(message);
}
void put32(g1::Bytes& bytes, std::size_t at, std::size_t value) {
    for (std::size_t i=0;i<4;++i) bytes.at(at+i)=static_cast<std::uint8_t>(value>>(i*8U));
}
using Payloads=std::map<std::uint32_t,g1::Bytes>;
g1::Archive archive(Payloads payloads={}) {
    payloads[268]=g1::Bytes(66,0);payloads[268][0]=8;payloads[268][1]=8;
    for (const auto id:{280U,281U,282U}) {
        payloads[id]=g1::Bytes(33,0);payloads[id][0]=1;
    }
    const auto count=payloads.size(), members=32U+count*8U, body=members+(count+1U)*8U;
    g1::Bytes bytes(body,0);
    put32(bytes,0,0x42494746U);put32(bytes,4,0x00800001U);
    put32(bytes,8,32);put32(bytes,12,count);put32(bytes,16,members);
    put32(bytes,20,count);put32(bytes,24,body);
    std::size_t ordinal{};
    for (const auto& item:payloads) {
        const auto at=32U+ordinal*8U;
        put32(bytes,at,item.first);bytes[at+4]=1;bytes[at+6]=static_cast<std::uint8_t>(ordinal);
        put32(bytes,members+ordinal*8U,0xf4e02223U);
        put32(bytes,members+ordinal*8U+4U,bytes.size());
        const auto payload=bytes.size();bytes.resize(payload+4U);put32(bytes,payload,4);
        bytes.insert(bytes.end(),item.second.begin(),item.second.end());
        ++ordinal;
    }
    put32(bytes,members+count*8U+4U,bytes.size());put32(bytes,28,bytes.size()-body);
    return g1::Archive(std::move(bytes));
}
g1::Archive campaign_archive(std::initializer_list<std::vector<std::uint16_t>> commands) {
    Payloads payloads;
    for (std::uint8_t type=0;type<23;++type) {
        g1::Bytes data{0,0,0,0,0,static_cast<std::uint8_t>(type%2U),7,1,0,0,0,2};
        for (int form=0;form<2;++form) {
            data.push_back(14);const auto at=data.size();data.resize(at+14,0);data[at]=4;
        }
        payloads[g1::unit_definition_resource(type)]=std::move(data);
    }
    payloads[306]={4,3,1,0,0,3,3,0,0,3,1,0,0,3,99,0,0};
    g1::Bytes script{1,static_cast<std::uint8_t>(commands.size())};
    for (const auto& command:commands)for (const auto word:command) {
        script.push_back(static_cast<std::uint8_t>(word>>8U));script.push_back(static_cast<std::uint8_t>(word));
    }
    payloads[315]=std::move(script);
    return archive(std::move(payloads));
}
g1::WorldBootstrap world(const g1::CampaignContext& context={}) {
    g1::WorldBootstrap value;
    value.mission_resource_id=g1::mission_resource_for_campaign(context.group,context.mission_index);
    value.player_factions=context.player_factions;
    value.grid={8,8,g1::Bytes(64,0)};
    value.terrain_rules={{{{1,0,0}}},{{{3,0,0}}},{{{1,0,0}}},{{{99,0,0}}}};
    value.sites.resize(3);
    for (std::uint8_t i=0;i<23;++i) {
        g1::UnitDefinition def;def.type=i;def.scalar_fields={{static_cast<std::uint8_t>(i%2U),7,1,0,0}};
        def.forms={g1::Bytes(14,0),g1::Bytes(14,0)};
        def.forms[0][0]=4;def.forms[1][0]=6;
        value.unit_definitions.push_back(std::move(def));
    }
    return value;
}
g1::MissionCommand command(std::uint16_t opcode, const std::vector<std::uint16_t>& words) {
    return {7,18,2U+words.size()*2U,opcode,words};
}
using Values=std::vector<std::int64_t>;
std::vector<std::uint16_t> words(const Values& input, std::size_t start) {
    std::vector<std::uint16_t> result;
    for (std::size_t i=start;i<input.size();++i) {
        check(input[i]>=0 && input[i]<=65535,"fixture word out of range");
        result.push_back(static_cast<std::uint16_t>(input[i]));
    }
    return result;
}
bool same_state(const g1::GameRandom::State& a,const g1::GameRandom::State& b) {
    return a.words==b.words && a.index==b.index && a.draws==b.draws;
}
Values native_row(char tag,const Values& a,const g1::Archive& data) {
    g1::CampaignContext campaign;
    if (tag=='A') {campaign.group=static_cast<std::uint8_t>(a.at(0));campaign.mission_index=static_cast<std::int32_t>(a.at(1));}
    if (tag=='S') campaign.player_factions={{static_cast<std::uint8_t>(a.at(3)),static_cast<std::uint8_t>(1-a.at(3))}};
    auto state=world(campaign);
    auto prepare=[&](std::uint16_t op,const std::vector<std::uint16_t>& args) {
        const auto result=g1::prepare_world_command(data,state,command(op,args),campaign);
        check(result.command_index==7U && result.opcode==op,"plan lost command token/opcode");
        return result;
    };
    switch (tag) {
    case 'L': {
        const auto p=prepare(0,{});const auto& phase=std::get<g1::WorldPhaseCommand>(p.data);
        check(!phase.title_text_index && !phase.title_text_id,"phase start invented a title");
        return {*p.phase_after,*phase.phase_flag_225,std::max(a.at(0),std::int64_t(phase.minimum_loading_stage))};
    }
    case 'T': {
        const auto p=prepare(16,{static_cast<std::uint16_t>(a.at(0))});
        const auto& phase=std::get<g1::WorldPhaseCommand>(p.data);
        check(!phase.phase_flag_225 && *phase.title_text_index==a.at(0),"title phase changed flag/raw index");
        return {*p.phase_after,std::max(a.at(1),std::int64_t(phase.minimum_loading_stage)),0x21ff0000U|*phase.title_text_id};
    }
    case 'F': {
        state.flag_227=a.at(1)!=0;
        const auto p=prepare(static_cast<std::uint16_t>(a.at(0)),{});
        return {*p.phase_after,std::get<g1::WorldCombatFlagCommand>(p.data).value};
    }
    case 'M': {
        const auto p=prepare(static_cast<std::uint16_t>(a.at(0)),words(a,1));
        const auto& m=std::get<g1::WorldMarkerCommand>(p.data);
        check((m.slot==g1::WorldMarkerSlot::field_254)==(a.at(0)==18),"wrong original marker slot");
        return {*p.phase_after,m.x,m.y,m.renderer_mode,m.rebuild_shared_geometry};
    }
    case 'I': {
        const auto p=prepare(20,words(a,0));const auto& m=std::get<g1::WorldImmunityCommand>(p.data);
        check(m.type==a.at(0),"immunity plan changed target");return {*p.phase_after,m.value};
    }
    case 'A': {
        const auto p=prepare(15,words(a,2));
        Values result{p.phase_after.value_or(99)};
        for (const auto value:std::get<g1::WorldAvailabilityCommand>(p.data).available)result.push_back(value);
        return result;
    }
    case 'P': {
        const auto p=prepare(17,words(a,0));Values result{p.phase_after.value_or(99)};
        std::array<std::int8_t,3> sites{{-1,-1,-1}};
        for (const auto& point:std::get<g1::WorldPointsCommand>(p.data).points) {
            result.insert(result.end(),{point.point.x,point.point.y,static_cast<std::int64_t>(point.site_index),point.point.state});
            sites.at(point.site_index)=1;
        }
        for (auto value:sites)result.push_back(value);
        return result;
    }
    case 'N': {
        const auto p=prepare(21,words(a,1));
        check(std::holds_alternative<g1::WorldOriginalNoEffect>(p.data),"verified no-op invented effects");
        return {p.phase_after.value_or(static_cast<std::int32_t>(a.at(0))),0};
    }
    case 'S': {
        g1::WorldUnit unit;unit.type=static_cast<std::uint8_t>(a.at(0));
        unit.faction=static_cast<std::uint8_t>(a.at(1));
        unit.owner=campaign.player_factions[0]==unit.faction?0U:1U;
        state.units.push_back(unit);
        const auto p=prepare(6,{unit.type,1,1,static_cast<std::uint16_t>(a.at(2)),0});
        const auto& spawn=std::get<g1::WorldSpawnCommand>(p.data);
        check(p.phase_after==99 && spawn.active_count_increment==1,"spawn phase/count mutation lost");
        return {spawn.identity.faction,spawn.identity.owner,spawn.rebuild_owner_slots};
    }
    case 'R': {
        for (std::uint8_t i=0;i<23;++i) {
            g1::WorldUnit unit;unit.type=i;unit.faction=static_cast<std::uint8_t>(i%2U);unit.owner=unit.faction;
            unit.active=(static_cast<std::uint32_t>(a.at(2))&(1U<<i))!=0U;state.units.push_back(unit);
        }
        g1::GameRandom random(static_cast<std::uint32_t>(a.at(0)));const auto before=random.state();
        const auto selected=g1::choose_random_script_unit(state.unit_definitions,state.units,
            static_cast<std::uint8_t>(a.at(1)),random);
        check(same_state(before,random.state()),"preparation consumed actual random state");
        check(selected.after.draws==selected.words_consumed,"random draw accounting mismatch");
        Values result{selected.type,selected.words_consumed,selected.after.index};
        random.restore(selected.after);for(int i=0;i<8;++i)result.push_back(random.next_word());
        return result;
    }
    case 'U': {
        state.grid={};
        const auto p=prepare(9,{0,static_cast<std::uint16_t>(a.at(0))});
        const auto& map=std::get<g1::WorldMapInstall>(p.data);
        check(map.style_changed && map.music_request.has_value(),"cold map did not request original music");
        Values result{1,map.music_request->operation,map.music_request->argument};
        if (a.at(1)==0 && map.stop_music_if_config_a4_disabled) {result[0]=2;result.insert(result.end(),{3,0});}
        return result;
    }
    default:throw std::runtime_error("unrecognized original fixture tag");
    }
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: world_commands_test original_vectors");
        const auto data=archive();std::ifstream input(argv[1]);
        std::string magic;std::size_t count{};input>>magic>>count;
        if(magic!="G1WORLDCOMMANDS1" || count!=151U)throw std::runtime_error("invalid original command fixture");
        for(std::size_t n=0;n<count;++n) {
            char tag{};std::size_t ac{},bc{};input>>tag>>ac;
            if(ac>128)throw std::runtime_error("oversize fixture input");
            Values a(ac);for(auto& value:a)input>>value;input>>bc;
            if(bc>128)throw std::runtime_error("oversize fixture output");
            Values b(bc);for(auto& value:b)input>>value;
            if(!input)throw std::runtime_error("truncated original fixture");
            const auto actual=native_row(tag,a,data);
            check(actual.size()==b.size(),"original/native output width mismatch");
            for(std::size_t i=0;i<b.size();++i)check(actual[i]==b[i],"original/native world command output mismatch");
        }
        std::string extra;check(!(input>>extra),"trailing fixture content");
        auto state=world();const g1::CampaignContext campaign;
        auto prepare=[&](std::uint16_t op,std::vector<std::uint16_t> args) {
            return g1::prepare_world_command(data,state,command(op,args),campaign);
        };
        const auto unchanged=std::get<g1::WorldMapInstall>(prepare(9,{0,0}).data);
        check(!unchanged.style_changed && !unchanged.music_request && !unchanged.stop_music_if_config_a4_disabled,
              "same-style reload restarted music");
        const auto site_plan=prepare(7,{1,2,2,3,0,1,1,2,2,3,4,5,6});
        const auto& sites=std::get<g1::WorldSitesCommand>(site_plan.data).sites;
        check(sites.size()==1 && sites[0].state==-1 && sites[0].coordinate_groups.size()==3 &&
              sites[0].coordinate_groups[0].empty() && sites[0].coordinate_groups[1].size()==1 &&
              sites[0].coordinate_groups[2].size()==2,"general site-group framing narrowed");
        g1::WorldUnit old;old.type=11;old.faction=1;old.owner=1;old.active=true;state.units.push_back(old);
        const auto repeated=std::get<g1::WorldSpawnCommand>(prepare(6,{11,3,4,0,0}).data);
        check(repeated.identity.active && repeated.active_count_increment==1,
              "repeated active type did not remain a valid count-incrementing placement");
        const auto special=std::get<g1::WorldSpawnCommand>(prepare(6,{11,3,4,0,1}).data);
        check(special.modifiers.script_special_placement,"special placement modifier omitted");
        for (const auto op:{0U,6U,7U,9U,13U,14U,15U,16U,17U,18U,19U,20U,21U})
            check(g1::is_world_command(static_cast<std::uint16_t>(op)),"recovered opcode missing");
        for (const auto op:{1U,2U,3U,4U,5U,8U,10U,11U,12U,22U})
            check(!g1::is_world_command(static_cast<std::uint16_t>(op)),"unrecovered opcode treated as world effect");
        rejects([&]{(void)prepare(11,{});},"unknown operation silently accepted");
        rejects([&]{(void)prepare(6,{11,8,0,0,0});},"off-map placement accepted");
        rejects([&]{(void)prepare(6,{0xffff,1,1,0,0});},"random placement invented generator");
        rejects([&]{(void)prepare(7,{1,0,0,1,1,0});},"truncated site group accepted");
        rejects([&]{(void)prepare(15,{1,23});},"out-of-table available type accepted");
        rejects([&]{(void)prepare(17,{1,1,1,3});},"point invented absent site state");
        rejects([&]{(void)prepare(20,{23,1});},"immunity applied to absent type");
        rejects([&]{(void)prepare(21,{1,1,2,3});},"malformed no-op data silently accepted");
        state.units.clear();
        for (std::uint8_t i=0;i<23;++i) {g1::WorldUnit u;u.type=i;u.owner=u.faction=static_cast<std::uint8_t>(i%2U);u.active=true;state.units.push_back(u);}
        g1::GameRandom random(5);const auto before=random.state();
        rejects([&]{(void)g1::choose_random_script_unit(state.unit_definitions,state.units,0,random);},
                "no-candidate original infinite retry was not bounded explicitly");
        check(same_state(before,random.state()),"failed random request consumed generator");

        const auto repeated_data=campaign_archive({{0},{16,208},{9,0,0},
            {6,11,1,1,0,1},{6,11,2,2,0,0},{4,2,2,1},{2},{3,11,280}});
        const auto boot=g1::bootstrap_campaign(repeated_data,campaign,random);
        check(boot.world.stop==g1::BootstrapStop::execution_required && boot.world.next_command_index==5 &&
              boot.world.units.size()==1 && boot.unit_activations.size()==1 && boot.applied_commands.size()==5,
              "generic bootstrap consumed camera or duplicated repeated roster object");
        check(boot.world.units[0].x==2 && boot.world.units[0].y==2 && boot.world.units[0].spawn.health==100 &&
              !boot.world.units[0].spawn.damage_immune && !boot.world.units[0].spawn.flag_18 &&
              boot.unit_activations[0].combat.preset_34_ms==750 &&
              boot.unit_activations[0].combat.timer_30_ms==-1,
              "repeated activation lost original reset/preserved timer fields");
        check(boot.world.initialization_effects.size()==3 &&
              boot.world.initialization_effects[0].kind==g1::WorldInitializationEffectKind::begin_current_player_turn &&
              boot.world.initialization_effects[0].sites_at_turn_start.empty() &&
              boot.world.initialization_effects[1].active_count_increment==1 &&
              boot.world.initialization_effects[2].active_count_increment==1,
              "bootstrap collapsed two real activation increments");
        check(same_state(before,random.state()) && same_state(before,boot.random_after),
              "nonrandom bootstrap changed shared stream");
        const auto camera_data=campaign_archive({{0},{16,208},{9,0,0},{4,3,2,1},
            {6,11,1,1,0,0},{2},{3,11,280}});
        const auto empty=g1::bootstrap_campaign(camera_data,campaign,random);
        check(empty.world.units.empty() && empty.unit_activations.empty() && empty.world.next_command_index==3 &&
              empty.world.camera && empty.world.camera->x==3 && empty.applied_commands.size()==3,
              "early camera reordered later unit placement");
        const auto random_data=campaign_archive({{0},{16,208},{9,0,0},{6,65535,1,1,0,0},
            {6,65535,2,1,0,0},{6,65534,3,1,0,0},{4,1,1,0},{12,2,239,65534}});
        const auto randomized=g1::bootstrap_campaign(random_data,campaign,random);
        check(randomized.world.units.size()==3 && randomized.world.units[0].type!=randomized.world.units[1].type &&
              randomized.world.units[0].faction==0 && randomized.world.units[1].faction==0 &&
              randomized.world.units[2].faction==1 && randomized.random_after.draws>before.draws &&
              same_state(before,random.state()),"randomized bootstrap lost shared stream/active rejection");
        const auto points_data=campaign_archive({{0},{16,208},{9,0,0},{7,2,2,2,0,3,3,0},
            {17,1,1,2,1},{18,65535,3},{19,7,6},{21,1,0,0,7,7},{13},{4,1,1,1}});
        const auto points_boot=g1::bootstrap_campaign(points_data,campaign,random);
        const auto board=points_boot.world.movement_board();
        check(points_boot.world.sites[0].state==-1 && points_boot.world.sites[1].state==1 &&
              points_boot.world.site_points.size()==1 && board.points.size()==1 && board.points[0].state==1 &&
              points_boot.world.marker_254==std::array<std::int16_t,2>{{-1,3}} &&
              points_boot.world.marker_25c==std::array<std::int16_t,2>{{1,1}} &&
              points_boot.world.flag_227 && board.cost99_regions.empty(),
              "point/marker/verified-no-op metadata changed original meanings");
        check(g1::movement_entry_cost(board,{11,0,4,0,0},1,2)==g1::movement_blocked,
              "installed original point did not reach movement rule inputs");
        std::cout<<"Compared "<<count<<" original world-command/RNG vectors; passed "<<checks<<" checks.\n";
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
