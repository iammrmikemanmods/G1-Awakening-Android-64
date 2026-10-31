#include "g1/AI.hpp"
#include "g1/AIIncome.hpp"

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
        if (argc!=2) throw std::invalid_argument("Usage: g1_ai_site_tests ORIGINAL_REFERENCE_FIXTURE");
        std::ifstream in(argv[1]);
        if (!in) throw std::runtime_error("Cannot open original AI reference fixture");
        tag(in,"G1_AI_SITE_V1");
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
            const g1::AIWorldView world{{grid,lookup,rules,definitions,flag227},board,units,motion,sites,
                                       captures,factions,controllers,current};
            const auto before=captures;
            tag(in,"A");
            const auto plan=g1::choose_ai_capture_plan(world,actor);
            const bool expected_plan=read(in)!=0;
            equal(plan.has_value(),expected_plan,"capture plan presence");
            if (expected_plan) {
                const std::int64_t values[]={plan->score,plan->action,plan->target_type,
                    plan->destination.x,plan->destination.y,plan->site,plan->group,plan->item,
                    plan->movement_form,plan->transform_on_arrival};
                for (auto value:values) equal(value,read(in),"capture plan field");
            }
            tag(in,"I");
            const auto count=read(in);
            for (std::int64_t i=0;i<count;++i) {
                const auto item=read(in)!=0;
                const auto s=static_cast<std::size_t>(read(in));
                const auto g=static_cast<std::size_t>(read(in));
                const auto n=static_cast<std::size_t>(read(in));
                const auto owner=static_cast<std::uint8_t>(read(in));
                const auto own_before=static_cast<std::int32_t>(read(in));
                const auto other_before=static_cast<std::int32_t>(read(in));
                const auto expected=read(in);
                const g1::CaptureTarget target{item ? g1::CaptureTargetKind::item :
                    g1::CaptureTargetKind::site,s,g,n};
                equal(g1::ai_capture_income_potential(captures,factions,owner,target,
                    own_before,other_before),expected,"income potential");
            }
            equal(captures.counters_68==before.counters_68,true,"income counters unchanged");
            equal(captures.item_links==before.item_links,true,"network links unchanged");
            equal(captures.sites.size(),before.sites.size(),"site count unchanged");
            for (std::size_t s=0;s<captures.sites.size();++s) {
                equal(captures.sites[s].state,before.sites[s].state,"site ownership unchanged");
                equal(captures.sites[s].item_states==before.sites[s].item_states,true,"item ownership unchanged");
            }
            if (cases==1) {
                rejects([&]{g1::choose_ai_capture_plan(world,units.size());});
                auto previous=motion[actor].form;motion[actor].form=2;
                rejects([&]{g1::choose_ai_capture_plan(world,actor);});motion[actor].form=previous;
                auto old=definitions[units[actor].type].forms;
                definitions[units[actor].type].forms.pop_back();
                rejects([&]{g1::choose_ai_capture_plan(world,actor);});
                definitions[units[actor].type].forms=old;
            }
        }
        if (kind!="END" || cases==0) throw std::runtime_error("AI fixture missing end/cases");
        if (in>>kind) throw std::runtime_error("Trailing AI fixture data");
        std::cout<<"{\"cases\":"<<cases<<",\"checks\":"<<checks<<",\"passed\":true}\n";
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
