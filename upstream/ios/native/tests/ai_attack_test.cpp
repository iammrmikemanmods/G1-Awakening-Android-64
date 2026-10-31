#include "g1/AI.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::size_t checks=0,cases=0;
std::int64_t read(std::istream& in) {
    std::int64_t n;
    if (!(in>>n)) throw std::runtime_error("Truncated AI reference fixture");
    return n;
}
void tag(std::istream& in,const std::string& expected) {
    std::string s;
    if (!(in>>s) || s!=expected) throw std::runtime_error("Expected AI fixture tag "+expected);
}
void equal(std::int64_t actual,std::int64_t expected,const char* what) {
    ++checks;
    if (actual!=expected) throw std::runtime_error(std::string(what)+" in case"+std::to_string(cases)+
        ": got"+std::to_string(actual)+" expected"+std::to_string(expected));
}
template<class Function> void rejects(Function f) {
    ++checks;
    try { f(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Malformed AI input accepted");
}
}

int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::invalid_argument("Usage: g1_ai_attack_tests ORIGINAL_REFERENCE_FIXTURE");
        std::ifstream in(argv[1]);
        if (!in) throw std::runtime_error("Cannot open original AI reference fixture");
        tag(in,"G1_AI_ATTACK_V1");
        std::string kind;
        while (in>>kind && kind!="END") {
            if (kind!="C") throw std::runtime_error("Expected AI case");
            ++cases;
            (void)read(in);
            g1::TerrainGrid grid;
            grid.width=static_cast<std::uint16_t>(read(in));
            grid.height=static_cast<std::uint16_t>(read(in));
            const auto actor=static_cast<std::size_t>(read(in));
            const auto current=static_cast<std::uint8_t>(read(in));
            const bool flag227=read(in)!=0;
            std::array<std::int8_t,2> factions{},controllers{};
            for (auto& v:factions) v=static_cast<std::int8_t>(read(in));
            for (auto& v:controllers) v=static_cast<std::int8_t>(read(in));
            g1::CaptureNetwork captures;
            for (auto& v:captures.counters_68) v=static_cast<std::int32_t>(read(in));
            const auto unit_count=static_cast<std::size_t>(read(in));
            const auto site_count=static_cast<std::size_t>(read(in));
            tag(in,"G");
            grid.tiles.resize(std::size_t(grid.width)*grid.height);
            for (auto& v:grid.tiles) v=static_cast<std::uint8_t>(read(in));
            tag(in,"L");
            g1::TerrainLookup lookup{};
            for (auto& v:lookup) v=static_cast<std::uint8_t>(read(in));
            tag(in,"T");
            g1::TerrainRules rules(static_cast<std::size_t>(read(in)));
            for (auto& r:rules) for (auto& v:r.fields) v=static_cast<std::uint8_t>(read(in));
            std::vector<g1::UnitDefinition> definitions(23);
            for (std::size_t t=0;t<23;++t) {
                definitions[t].type=static_cast<std::uint8_t>(t);
                definitions[t].forms.assign(2,g1::Bytes(14));
            }
            std::vector<g1::CombatUnitState> units;
            std::vector<g1::MovementUnitState> motion;
            for (std::size_t i=0;i<unit_count;++i) {
                tag(in,"U");
                g1::CombatUnitState u;
                u.type=static_cast<std::uint8_t>(read(in));
                u.owner=static_cast<std::uint8_t>(read(in));
                u.faction=static_cast<std::uint8_t>(read(in));
                u.form=static_cast<std::uint8_t>(read(in));
                u.level=static_cast<std::uint8_t>(read(in));
                u.health=static_cast<std::uint8_t>(read(in));
                u.active=read(in)!=0;
                u.damage_immune=read(in)!=0;
                u.flag_38=static_cast<std::uint8_t>(read(in));
                u.support_bonus=static_cast<std::int32_t>(read(in));
                u.timer_30_ms=static_cast<std::int32_t>(read(in));
                u.preset_34_ms=static_cast<std::int32_t>(read(in));
                u.x=static_cast<std::uint8_t>(read(in));
                u.y=static_cast<std::uint8_t>(read(in));
                g1::MovementUnitState m;
                m.active=u.active;m.form=u.form;m.tile_x=u.x;m.tile_y=u.y;
                m.spent_budget=static_cast<std::uint8_t>(read(in));
                m.flag_2c=static_cast<std::uint8_t>(read(in));
                m.offset_x=static_cast<std::int32_t>(read(in));
                m.offset_y=static_cast<std::int32_t>(read(in));
                auto& d=definitions.at(u.type);
                d.scalar_fields[0]=u.faction;
                d.scalar_fields[3]=static_cast<std::uint8_t>(read(in));
                d.scalar_fields[4]=static_cast<std::uint8_t>(read(in));
                for (auto& f:d.forms) for (auto& v:f) v=static_cast<std::uint8_t>(read(in));
                u.stats=g1::combat_stats(d,u.form,u.level);
                units.push_back(u);motion.push_back(m);
            }
            std::vector<g1::WorldSite> sites;
            for (std::size_t s=0;s<site_count;++s) {
                tag(in,"S");
                g1::WorldSite site;
                g1::MissionSiteState state;
                state.state=static_cast<std::int8_t>(read(in));
                site.position.x=static_cast<std::uint8_t>(read(in));
                site.position.y=static_cast<std::uint8_t>(read(in));
                const auto group_count=static_cast<std::size_t>(read(in));
                std::vector<std::vector<std::uint8_t>> links;
                for (std::size_t g=0;g<group_count;++g) {
                    tag(in,"I");
                    const auto item_count=static_cast<std::size_t>(read(in));
                    std::vector<g1::WorldCoordinate> points;
                    std::vector<std::int8_t> owners;
                    std::vector<std::uint8_t> connected;
                    for (std::size_t i=0;i<item_count;++i) {
                        owners.push_back(static_cast<std::int8_t>(read(in)));
                        connected.push_back(static_cast<std::uint8_t>(read(in)));
                        const auto x=static_cast<std::uint8_t>(read(in));
                        const auto y=static_cast<std::uint8_t>(read(in));
                        points.push_back({x,y});
                    }
                    site.coordinate_groups.push_back(points);
                    state.item_states.push_back(owners);
                    links.push_back(connected);
                }
                sites.push_back(site);captures.sites.push_back(state);captures.item_links.push_back(links);
            }
            g1::MovementBoard board{grid.width,grid.height,{}, {}, {}};
            for (auto tile:grid.tiles) {
                const auto& r=rules.at(lookup.at(tile)).fields;
                board.cells.push_back({r[0],r[2],-1});
            }
            for (std::uint8_t owner=0;owner<2;++owner)
                for (std::uint8_t type=0;type<23;++type)
                    for (std::size_t i=0;i<units.size();++i) {
                        const auto& u=units[i];const auto& m=motion[i];
                        if (u.active && u.owner==owner && u.type==type && m.offset_x==0 && m.offset_y==0) {
                            auto& cell=board.cells.at(std::size_t(u.y)*board.width+u.x);
                            if (cell.active_owner<0) cell.active_owner=owner;
                        }
                    }
            tag(in,"P");
            const auto point_count=read(in);
            for (std::int64_t i=0;i<point_count;++i) {
                const auto x=static_cast<std::int32_t>(read(in)),y=static_cast<std::int32_t>(read(in)),
                           state=static_cast<std::int32_t>(read(in));
                board.points.push_back({x,y,state});
            }
            tag(in,"R");
            const auto region_count=read(in);
            for (std::int64_t i=0;i<region_count;++i) {
                const auto x=static_cast<std::int32_t>(read(in)),y=static_cast<std::int32_t>(read(in)),
                           w=static_cast<std::int32_t>(read(in)),h=static_cast<std::int32_t>(read(in));
                board.cost99_regions.push_back({x,y,w,h});
            }
            g1::AIWorldView world{{grid,lookup,rules,definitions,flag227},board,units,motion,sites,
                                       captures,factions,controllers,current};
            in>>std::ws;
            if (in.peek()=='M') {
                tag(in,"M");
                for (auto& v:world.funds) v=static_cast<std::int32_t>(read(in));
                for (auto& v:world.upgrade_slots_34) v=static_cast<std::int32_t>(read(in));
                for (auto& slots:world.owner_slot_types) {
                    const auto n=read(in);
                    for (std::int64_t i=0;i<n;++i) slots.push_back(static_cast<std::uint8_t>(read(in)));
                }
                for (const auto& u:units) {
                    definitions[u.type].scalar_fields[1]=static_cast<std::uint8_t>(read(in));
                    definitions[u.type].scalar_fields[2]=static_cast<std::uint8_t>(read(in));
                }
            }
            in>>std::ws;
            if (in.peek()=='W') {
                tag(in,"W");
                for (auto& value:world.available_types) value=read(in)!=0;
            }
            tag(in,"A");
            const auto plan=g1::choose_ai_attack(world,actor);
            const bool expected_plan=read(in)!=0;
            equal(plan.has_value(),expected_plan,"attack plan presence");
            if (expected_plan) {
                const std::int64_t values[]={plan->score,plan->action,plan->target_type,
                    plan->destination.x,plan->destination.y,plan->site,plan->group,plan->item,
                    plan->movement_form,plan->transform_on_arrival};
                for (auto value:values) equal(value,read(in),"attack plan field");
            }
            tag(in,"Q");
            const auto count=read(in);
            for (std::int64_t i=0;i<count;++i) {
                const auto x=static_cast<std::uint8_t>(read(in)),y=static_cast<std::uint8_t>(read(in));
                const auto excluded=read(in);const auto alternate=read(in)!=0;
                const auto expected=read(in);
                equal(g1::ai_position_score(world,actor,{x,y},excluded<0 ? std::optional<std::size_t>{}
                    : std::optional<std::size_t>{static_cast<std::size_t>(excluded)},alternate),expected,"position score");
            }
            tag(in,"B");
            const auto budget=static_cast<std::int32_t>(read(in));
            const auto expected_vacant=read(in);
            const auto& a=units[actor];const auto& f=definitions[a.type].forms[a.form];
            const auto field6=f[6]<128 ? int(f[6]) : int(f[6])-256;
            const g1::MovementProfile profile{a.type,a.owner,f[0],motion[actor].spent_budget,
                                             static_cast<std::int8_t>(field6)};
            const auto range=g1::compute_movement_range_with_budget(board,profile,a.x,a.y,budget);
            equal(range.has_unoccupied_destination,expected_vacant,"explicit-budget vacancy");
            for (auto v:range.remaining) equal(v,read(in),"explicit-budget cell");
            in>>std::ws;
            if (in.peek()=='F') {
                for (const auto& key:std::array<std::string,2>{{"F","J"}}) {
                    tag(in,key);
                    const auto decision=key=="F" ? g1::choose_ai_fallback_plan(world,actor)
                                                 : g1::plan_ai_unit(world,actor);
                    equal(decision.form_after_planning,read(in),"planner form side effect");
                    equal(decision.stats_after_planning.attack_low,read(in),"planner attack low");
                    equal(decision.stats_after_planning.attack_high,read(in),"planner attack high");
                    equal(decision.stats_after_planning.defense,read(in),"planner defense");
                    const bool present=read(in)!=0;
                    equal(decision.plan.has_value(),present,"planner presence");
                    if (present) {
                        const auto& p=*decision.plan;
                        const std::int64_t values[]={p.score,p.action,p.target_type,p.destination.x,p.destination.y,
                            p.site,p.group,p.item,p.movement_form,p.transform_on_arrival};
                        for (auto v:values) equal(v,read(in),"planner field");
                    }
                }
            }
            in>>std::ws;
            if (in.peek()=='D') {
                tag(in,"D");
                const auto cursor=static_cast<std::int32_t>(read(in));
                const auto decision=g1::choose_ai_turn_admission(world,cursor);
                equal(static_cast<int>(decision.kind),read(in),"turn admission kind");
                equal(decision.actor ? units[*decision.actor].type : -1,read(in),"turn admission actor");
                equal(decision.production_type ? *decision.production_type : -1,read(in),"production type");
                equal(decision.next_cursor,read(in),"turn cursor");
                const auto reconstructed=g1::ai_owner_slots(definitions,units,factions);
                for (std::size_t owner=0;owner<2;++owner) {
                    equal(reconstructed[owner].size(),world.owner_slot_types[owner].size(),"owner slot count");
                    for (std::size_t i=0;i<reconstructed[owner].size();++i)
                        equal(reconstructed[owner][i],world.owner_slot_types[owner][i],"owner slot type");
                }
            }
            in>>std::ws;
            if (in.peek()=='X') {
                tag(in,"X");
                const auto type=read(in);
                const auto producer=std::find_if(units.begin(),units.end(),[&](const auto& unit) {
                    return unit.type==type;
                });
                if (producer==units.end()) throw std::runtime_error("Production fixture producer is absent");
                const auto target=g1::ai_production_destination(world,static_cast<std::size_t>(producer-units.begin()));
                equal(target.has_value(),read(in),"production destination presence");
                if (target) {
                    equal(target->x,read(in),"production destination x");
                    equal(target->y,read(in),"production destination y");
                }
            }
            if (cases==1) {
                rejects([&]{g1::choose_ai_attack(world,units.size());});
                rejects([&]{g1::ai_position_score(world,actor,{255,255});});
                rejects([&]{g1::ai_position_score(world,actor,{},units.size());});
                auto previous=motion[actor].form;motion[actor].form=2;
                rejects([&]{g1::choose_ai_attack(world,actor);});motion[actor].form=previous;
                const auto old_cost=board.cells[0].terrain_cost;board.cells[0].terrain_cost=255;
                rejects([&]{g1::choose_ai_attack(world,actor);});board.cells[0].terrain_cost=old_cost;
                auto old=definitions[0].forms;definitions[0].forms.pop_back();
                rejects([&]{g1::choose_ai_attack(world,actor);});definitions[0].forms=old;
            }
        }
        if (kind!="END" || cases==0) throw std::runtime_error("AI fixture missing end/cases");
        if (in>>kind) throw std::runtime_error("Trailing AI fixture data");
        std::cout<<"{\"cases\":"<<cases<<",\"checks\":"<<checks<<",\"passed\":true}\n";
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
