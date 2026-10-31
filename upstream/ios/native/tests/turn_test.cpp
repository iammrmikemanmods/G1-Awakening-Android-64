#include "g1/TurnProgression.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {
int number(std::istream& in) {
    std::int32_t value{};
    if (!(in>>value)) throw std::runtime_error("truncated turn fixture");
    return value;
}

std::vector<std::int64_t> execute(std::istream& in) {
    g1::TurnState state;
    state.current_player=std::uint8_t(number(in));
    state.controllers={{std::int8_t(number(in)),std::int8_t(number(in))}};
    state.funds={{number(in),number(in)}};
    state.human_turn_counter_274=number(in); state.ai_unit_cursor=number(in);
    state.income_notice_pending=number(in)!=0;
    const std::array<std::int8_t,2> factions{{std::int8_t(number(in)),std::int8_t(number(in))}};
    g1::CaptureNetwork network;
    network.counters_68={{number(in),number(in)}};
    const int count=number(in);
    if (count<0 || count>23) throw std::runtime_error("turn fixture roster bound");
    std::vector<g1::WorldUnit> roster;
    std::vector<g1::MovementUnitState> units;
    for(int n=0;n<count;++n) {
        g1::WorldUnit w;
        w.type=std::uint8_t(number(in));w.owner=std::uint8_t(number(in));
        g1::MovementUnitState u;
        u.active=number(in)!=0;u.form=std::uint8_t(number(in));
        u.tile_x=std::uint8_t(number(in));u.tile_y=std::uint8_t(number(in));
        u.offset_x=number(in);u.offset_y=number(in);u.facing=std::uint8_t(number(in));
        u.spent_budget=std::uint8_t(number(in));u.timer_ms=number(in);
        u.anchor_x=std::uint8_t(number(in));u.anchor_y=std::uint8_t(number(in));
        u.anchor_facing=std::uint8_t(number(in));
        u.flag_2c=std::uint8_t(number(in));u.flag_2d=std::uint8_t(number(in));
        u.flag_2f=std::uint8_t(number(in));u.flag_38=std::uint8_t(number(in));
        u.flag_39=std::uint8_t(number(in));u.flag_3a=std::uint8_t(number(in));
        roster.push_back(w);units.push_back(u);
    }
    const int site_count=number(in);
    if(site_count<0 || site_count>4)throw std::runtime_error("turn fixture site bound");
    for(int s=0;s<site_count;++s) {
        g1::MissionSiteState site;site.state=std::int8_t(number(in));
        const int groups=number(in);
        if(groups<0 || groups>4)throw std::runtime_error("turn fixture group bound");
        std::vector<std::vector<std::uint8_t>> links;
        for(int g=0;g<groups;++g){
            const int items=number(in);
            if(items<0 || items>8)throw std::runtime_error("turn fixture item bound");
            std::vector<std::int8_t> owners;std::vector<std::uint8_t> connected;
            for(int i=0;i<items;++i){owners.push_back(std::int8_t(number(in)));connected.push_back(std::uint8_t(number(in)));}
            site.item_states.push_back(owners);links.push_back(connected);
        }
        network.sites.push_back(site);network.item_links.push_back(links);
    }
    const auto transition=g1::advance_player_turn(roster,units,network,factions,state);
    std::vector<std::int64_t> out{state.current_player,transition.player_changed,
        transition.camera_focus ? transition.camera_focus->x : -1,
        transition.camera_focus ? transition.camera_focus->y : -1,
        transition.snapshot_requested,state.funds[0],state.funds[1],network.counters_68[0],
        network.counters_68[1],state.human_turn_counter_274,state.ai_unit_cursor,state.income_notice_pending};
    for(const auto& u:units) {
        const std::vector<std::int64_t> fields{u.active,u.form,u.tile_x,u.tile_y,u.offset_x,u.offset_y,
            u.facing,u.spent_budget,u.timer_ms,u.anchor_x,u.anchor_y,u.anchor_facing,
            u.flag_2c,u.flag_2d,u.flag_2f,u.flag_38,u.flag_39,u.flag_3a};
        out.insert(out.end(),fields.begin(),fields.end());
    }
    for(const auto& site:network.item_links) for(const auto& group:site)
        for(const auto value:group)out.push_back(value);
    return out;
}
}

int main(int argc,char** argv){try{
    if(argc>2)throw std::runtime_error("usage: turn_tests [original_fixture]");
    std::ifstream file;
    if(argc==2){file.open(argv[1]);if(!file)throw std::runtime_error("missing original turn fixture");}
    std::istream& input=argc==2 ? static_cast<std::istream&>(file) : std::cin;
    std::size_t cases{},checks{};
    std::string line;
    while(std::getline(input,line)){
        if(line.empty() || line.front()=='#')continue;
        const auto split=line.find('|');
        std::istringstream values(line.substr(0,split));
        const auto out=execute(values);
        if(argc==1){
            for(std::size_t n=0;n<out.size();++n)std::cout<<(n?" ":"")<<out[n];
            std::cout<<'\n';
        }else{
            if(split==std::string::npos)throw std::runtime_error("missing original turn output");
            std::istringstream expected(line.substr(split+1));
            for(const auto actual:out){std::int64_t original{};
                if(!(expected>>original) || actual!=original)
                    throw std::runtime_error("original turn mismatch at case"+std::to_string(cases)+" scalar"+std::to_string(checks));
                ++checks;
            }
            std::int64_t extra{};if(expected>>extra)throw std::runtime_error("uncompared original turn fields");
        }
        ++cases;
    }
    if(argc==2){if(cases!=192)throw std::runtime_error("turn fixture count changed");
        std::cout<<cases<<" original turn cases / "<<checks<<" state comparisons passed\n";}
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
