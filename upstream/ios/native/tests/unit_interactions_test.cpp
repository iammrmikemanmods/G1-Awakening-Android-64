#include "g1/UnitInteractions.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using Values=std::vector<std::int64_t>;
void check(bool ok,const std::string& message) { if (!ok) throw std::runtime_error(message); }
std::uint8_t byte(std::int64_t value) { return static_cast<std::uint8_t>(value); }
std::int32_t word(std::int64_t value) { return static_cast<std::int32_t>(value); }
std::int8_t signed_byte(std::int64_t value) {
    return static_cast<std::int8_t>(value<128 ? value : value-256);
}

struct World {
    std::vector<g1::UnitDefinition> definitions;
    std::vector<g1::CombatUnitState> units;
    std::vector<g1::MovementUnitState> motion;
    std::array<std::vector<std::uint8_t>,2> slots;
    std::array<bool,23> available{};
    g1::UnitInteractionView view() const { return {definitions,units,motion,slots}; }
};

World decode(const Values& input,std::size_t offset) {
    check(input.size()==offset+23*19,"Fixture input length is invalid");
    World world;
    for (std::size_t t=0;t<23;++t) {
        const auto* r=input.data()+offset+t*19;
        g1::UnitDefinition d;d.type=byte(t);d.scalar_fields={byte(r[0]),byte(r[1]),0,0,0};
        d.forms={{3,1,1,20,30,5,byte(r[2]),0,byte(r[4]),byte(r[6]),0,0,byte(r[8]),1},
                 {6,1,2,40,50,8,byte(r[3]),0,byte(r[5]),byte(r[7]),0,0,byte(r[9]),1}};
        world.definitions.push_back(d);
        g1::CombatUnitState u;
        u.type=byte(t);u.owner=byte(r[0]);u.faction=byte(r[0]);u.form=byte(r[11]);
        u.active=r[10]!=0;u.x=byte(r[12]);u.y=byte(r[13]);u.level=byte(r[17]);
        u.health=31;u.damage_immune=true;u.flag_38=5;u.stats={31,42,53};
        u.support_bonus=word(std::int64_t(t)*3-10);u.preset_34_ms=word(std::int64_t(t)*7-20);
        u.timer_30_ms=1234;world.units.push_back(u);
        g1::MovementUnitState m;
        m.active=u.active;m.form=u.form;m.tile_x=u.x;m.tile_y=u.y;
        m.offset_x=word(r[14]);m.offset_y=word(r[15]);m.facing=3;m.spent_budget=99;
        m.timer_ms=1234;m.anchor_x=9;m.anchor_y=8;m.anchor_facing=2;
        m.flag_2c=byte(r[16]);m.flag_2d=3;m.flag_2f=4;m.flag_38=5;m.flag_39=6;m.flag_3a=7;
        world.motion.push_back(m);world.available[t]=r[18]!=0;world.slots[u.owner].push_back(byte(t));
    }
    return world;
}

g1::MovementBoard board_for(const World& world,std::uint16_t width,std::uint16_t height) {
    g1::MovementBoard board;board.width=width;board.height=height;
    board.cells.resize(std::size_t(width)*height,{1,0,-1});
    for (const auto& slots:world.slots) for (auto type:slots) {
        const auto& u=world.units[type];const auto& m=world.motion[type];
        if (u.active && u.x<width && u.y<height && m.offset_x==0 && m.offset_y==0) {
            auto& cell=board.cells[std::size_t(u.y)*width+u.x];
            if (cell.active_owner<0) cell.active_owner=u.owner;
        }
    }
    return board;
}

Values unit_values(const g1::UnitActivation& activation) {
    const auto& m=activation.movement;const auto& u=activation.combat;
    return {m.active,m.form,m.tile_x,m.tile_y,m.offset_x,m.offset_y,m.facing,
            m.spent_budget,m.timer_ms,m.anchor_x,m.anchor_y,m.anchor_facing,m.flag_2c,
            m.flag_2d,m.flag_2f,m.flag_38,m.flag_39,m.flag_3a,u.health,u.level,
            u.damage_immune,u.stats.attack_low,u.stats.attack_high,u.stats.defense,
            u.support_bonus,u.preset_34_ms};
}

Values evaluate(int kind,const Values& input,std::size_t parameter_count) {
    World world=decode(input,parameter_count);const auto& p=input;
    Values out;
    switch (kind) {
    case 1: {
        const auto links=g1::derive_unit_static_links(world.definitions,world.slots);
        for (const auto& link:links) {
            out.push_back(link.field_10_type ? *link.field_10_type : -1);
            out.push_back(link.field_14_type ? *link.field_14_type : -1);
        }
        break;
    }
    case 2: {
        const auto list=g1::production_candidates(world.view(),world.available,byte(p[0]),
            p[1] ? g1::ProductionMode::deploy : g1::ProductionMode::ordinary);
        out.push_back(static_cast<std::int64_t>(list.size()));
        for (auto type:list) out.push_back(type);
        out.resize(24,-1);break;
    }
    case 3: {
        g1::MovementBoard board;board.width=static_cast<std::uint16_t>(p[0]);
        board.height=static_cast<std::uint16_t>(p[1]);
        const auto size=std::size_t(board.width)*board.height;
        check(parameter_count==5+size*2,"Placement parameter shape is invalid");
        for (std::size_t i=0;i<size;++i)
            board.cells.push_back({byte(p[5+i]),0,static_cast<std::int16_t>(p[5+size+i])});
        board.points.push_back({word(p[2]),word(p[3]),1});
        board.cost99_regions.push_back({0,0,board.width,board.height});
        const auto result=g1::production_placement(board,{byte(p[2]),byte(p[3])},signed_byte(p[4]));
        int cursor=-1;
        for (int i=0;i<4;++i) if (result.reachable[static_cast<std::size_t>(i)]) {cursor=i;break;}
        check(bool(result.initial_cursor)==(cursor>=0),"Initial cursor presence differs");
        if (cursor>=0) {
            const int dx[4]={0,0,-1,1},dy[4]={-1,1,0,0};
            check(result.initial_cursor->x==p[2]+dx[cursor] &&
                  result.initial_cursor->y==p[3]+dy[cursor],"Initial cursor direction differs");
        }
        out={result.range.has_unoccupied_destination,cursor};
        for (bool v:result.reachable) out.push_back(v);
        out.insert(out.end(),result.range.remaining.begin(),result.range.remaining.end());
        break;
    }
    case 4: {
        const auto result=g1::friendly_destination_admission(world.view(),static_cast<std::size_t>(p[0]),
            {byte(p[1]),byte(p[2])},p[3]!=0,p[4]!=0);
        const bool move=result==g1::FriendlyDestinationAdmission::move;
        out={move,result==g1::FriendlyDestinationAdmission::select_actor,move};break;
    }
    case 5: {
        const auto actor=static_cast<std::size_t>(p[0]);
        const auto result=g1::apply_transport_arrival(world.view(),actor,byte(p[1]),word(p[2]));
        // The original reference asserted byte-for-byte preservation except+d.
        const auto& before=world.motion[actor];const auto& after=result.movement;
        check(after.form==before.form && after.tile_x==before.tile_x && after.tile_y==before.tile_y &&
            after.offset_x==before.offset_x && after.offset_y==before.offset_y && after.facing==before.facing &&
            after.spent_budget==before.spent_budget && after.timer_ms==before.timer_ms &&
            after.anchor_x==before.anchor_x && after.anchor_y==before.anchor_y && after.anchor_facing==before.anchor_facing &&
            after.flag_2c==before.flag_2c && after.flag_2d==before.flag_2d && after.flag_2f==before.flag_2f &&
            after.flag_38==before.flag_38 && after.flag_39==before.flag_39 && after.flag_3a==before.flag_3a,
            "Boarding changed a preserved movement field");
        const auto& b=world.units[actor];const auto& a=result.combat;
        check(a.type==b.type && a.owner==b.owner && a.faction==b.faction && a.form==b.form &&
            a.x==b.x && a.y==b.y && a.level==b.level && a.health==b.health &&
            a.damage_immune==b.damage_immune && a.flag_38==b.flag_38 &&
            a.stats.attack_low==b.stats.attack_low && a.stats.attack_high==b.stats.attack_high &&
            a.stats.defense==b.stats.defense && a.support_bonus==b.support_bonus &&
            a.timer_30_ms==b.timer_30_ms && a.preset_34_ms==b.preset_34_ms,
            "Boarding changed a preserved combat field");
        const bool loaded=!result.matching_carrier_types.empty();
        check(result.clear_status_cache==loaded && result.deactivate_renderer==loaded &&
              result.combat.active==result.movement.active,"Boarding effects are inconsistent");
        out={result.combat.active,result.active_count_after,result.upgrade_slot_after,
             result.clear_status_cache ? 0 : 123+static_cast<std::int64_t>(actor),
             static_cast<std::int64_t>(result.matching_carrier_types.size())};
        for (auto type:result.matching_carrier_types) out.push_back(type);
        out.resize(28,-1);break;
    }
    case 6: {
        const auto producer=static_cast<std::size_t>(p[0]);const auto type=byte(p[1]);
        auto board=board_for(world,static_cast<std::uint16_t>(p[3]),static_cast<std::uint16_t>(p[4]));
        const auto result=g1::produce_unit(world.view(),board,world.available,producer,
            p[2] ? g1::ProductionMode::deploy : g1::ProductionMode::ordinary,type,
            {byte(p[5]),byte(p[6])},word(p[7]),p[9] ?
                g1::ProductionConfirmation::direct_type_list : g1::ProductionConfirmation::funds_checked_button);
        out=unit_values(result.activation);
        out.insert(out.end(),{result.producer_movement.flag_2c,result.funds_after,
            byte(p[8]+result.activation.active_count_increment),result.pending_type_after,
            result.selection_mode_after});
        for (auto flag:result.command_flags_after) out.push_back(flag);
        out.insert(out.end(),{result.return_cursor.x,result.return_cursor.y,
                            result.return_cursor.x,result.return_cursor.y,0});
        check(result.activation.clear_pending_route && result.activation.active_count_increment==1 &&
              result.audio_operation==5 && result.audio_argument==16,"Human production effects differ");
        check(result.activation.world.type==type && result.activation.world.owner==world.units[producer].owner &&
              result.activation.world.active && result.activation.world.form==result.activation.movement.form,
              "Production world identity differs");
        break;
    }
    case 7: {
        auto board=board_for(world,static_cast<std::uint16_t>(p[2]),static_cast<std::uint16_t>(p[3]));
        for (std::size_t i=0;i<board.cells.size();++i) board.cells[i].terrain_cost=byte(p[5+i]);
        const auto result=g1::production_eligibility(world.view(),board,world.available,
            static_cast<std::size_t>(p[0]),p[1] ? g1::ProductionMode::deploy : g1::ProductionMode::ordinary,
            word(p[4]));
        out={result==g1::ProductionEligibility::allowed};break;
    }
    case 8: {
        const auto& definition=world.definitions.at(static_cast<std::size_t>(p[0]));
        out={g1::production_affordable(definition,word(p[1])),g1::unit_spawn_cost(definition)};
        break;
    }
    default:throw std::runtime_error("Unknown original fixture kind");
    }
    return out;
}

template<class F> void rejects(F&& f,const char* message) {
    bool rejected=false;try {f();} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,message);
}

std::size_t guards() {
    Values input;
    for (std::size_t t=0;t<23;++t) {
        Values row={static_cast<std::int64_t>(t%2),8,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        input.insert(input.end(),row.begin(),row.end());
    }
    World w=decode(input,0);w.units[0].active=true;w.motion[0].active=true;
    w.units[0].x=1;w.units[0].y=1;w.motion[0].tile_x=1;w.motion[0].tile_y=1;
    w.definitions[0].forms[0][12]=1;w.definitions[0].forms[1][12]=1;
    auto board=board_for(w,3,3);
    const auto produce=[&]{return g1::produce_unit(w.view(),board,w.available,0,
        g1::ProductionMode::ordinary,2,{2,1},8,g1::ProductionConfirmation::funds_checked_button);};
    check(produce().funds_after==0,"Exact affordability boundary failed");
    w.motion[0].flag_2c=1;
    rejects(produce,"Acted producer admitted");w.motion[0].flag_2c=0;
    w.available[2]=false;rejects(produce,"Unavailable type admitted");w.available[2]=true;
    rejects([&]{g1::produce_unit(w.view(),board,w.available,0,g1::ProductionMode::ordinary,2,{2,1},7,
                g1::ProductionConfirmation::funds_checked_button);},
            "Insufficient funds admitted");
    rejects([&]{g1::produce_unit(w.view(),board,w.available,0,g1::ProductionMode::ordinary,2,{1,1},8,
                g1::ProductionConfirmation::funds_checked_button);},
            "Producer tile admitted");
    board.cells[5].active_owner=1;rejects(produce,"Stale occupied board admitted");board=board_for(w,3,3);
    w.motion[0].form=1;rejects(produce,"Unsynchronized form admitted");w.motion[0].form=0;
    auto bad_slots=w.slots;std::reverse(bad_slots[0].begin(),bad_slots[0].end());
    rejects([&]{g1::derive_unit_static_links(w.definitions,bad_slots);},"Unordered owner slots admitted");
    auto bad=w.definitions;bad[4].forms[1].pop_back();
    rejects([&]{g1::derive_unit_static_links(bad,w.slots);},"Malformed definition admitted");
    check(!g1::production_affordable(w.definitions[21],39) &&
          g1::production_affordable(w.definitions[21],40),"Type21 override was lost");
    w.definitions[2].scalar_fields[1]=255;
    check(g1::production_affordable(w.definitions[2],-1) &&
          !g1::production_affordable(w.definitions[2],-2),"Signed affordability was lost");
    // Unmaterialized type remains an original inactive candidate. Its fresh
    // preset34 is zero, while an already materialized inactive type preserves it.
    w.definitions[2].scalar_fields[1]=8;
    const auto existing=produce();check(existing.activation.combat.preset_34_ms==-6,"Inactive preset lost");
    w.units.erase(w.units.begin()+2);w.motion.erase(w.motion.begin()+2);
    // board_for indexes a full synthetic roster, so keep the already checked board.
    const auto fresh=produce();check(fresh.activation.combat.preset_34_ms==0,"Fresh preset differs from430c2");
    const auto list=g1::production_candidates(w.view(),w.available,0,g1::ProductionMode::ordinary);
    check(std::find(list.begin(),list.end(),2)!=list.end(),"Absent inactive candidate omitted");
    return 14;
}
} // namespace

int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("Pass original unit interaction fixture");
        std::ifstream stream(argv[1]);std::string magic;std::size_t cases=0;
        stream>>magic>>cases;check(magic=="G1UNITINTERACTIONS1" && cases>0,"Bad fixture header");
        std::size_t scalars=0;
        for (std::size_t i=0;i<cases;++i) {
            int kind;std::string name;std::size_t parameters,inputs,outputs;
            stream>>kind>>name>>parameters>>inputs>>outputs;
            Values input(inputs),expected(outputs);
            for(auto& v:input)stream>>v;
            for(auto& v:expected)stream>>v;
            check(bool(stream),"Truncated fixture");
            const auto actual=evaluate(kind,input,parameters);
            if (actual!=expected) {
                std::ostringstream error;error<<name<<" native/original mismatch";
                for(std::size_t j=0;j<std::min(actual.size(),expected.size());++j)
                    if(actual[j]!=expected[j]) {error<<" at scalar"<<j<<": native="<<actual[j]<<" original="<<expected[j];break;}
                throw std::runtime_error(error.str());
            }
            scalars+=outputs;
        }
        std::string trailing;check(!(stream>>trailing),"Trailing fixture records");
        const auto guard_count=guards();
        std::cout<<cases<<" unchanged-original unit interaction cases matched ("<<scalars
                 <<" scalars); "<<guard_count<<" native guards passed.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
