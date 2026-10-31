#include "g1/M3GBattleController.hpp"

#include <fstream>
#include <iostream>
#include <string>

namespace {
std::size_t checks=0;
void check(bool value,const std::string& message) { ++checks;if(!value)throw std::runtime_error(message); }
template<std::size_t N> std::array<std::int32_t,N> read(std::istream& input) {
    std::array<std::int32_t,N> values{};for(auto& value:values)if(!(input>>value))throw std::runtime_error("truncated controller fixture");
    return values;
}
g1::M3GBattleControllerConfig config(std::istream& input) {
    const auto p=read<20>(input);g1::M3GBattleControllerConfig c;
    c.a_first=p[0]!=0;c.counter=p[1]!=0;c.intro=p[2]!=0;
    for(std::size_t i=0;i<2;++i) {
        c.forms[i]=static_cast<std::uint8_t>(p[3+i]);c.factions[i]=static_cast<std::uint8_t>(p[5+i]);
        c.adjustments[i]=static_cast<std::uint8_t>(p[7+i]);c.immune[i]=p[9+i]!=0;c.dead[i]=p[11+i]!=0;
        c.damage[i]=p[13+i];c.final_health[i]=p[15+i];
    }
    c.logical_width=p[17];c.logical_height=p[18];
    for(std::size_t i=0;i<13;++i)c.embedded_camera[i]=(std::uint32_t(p[19])&(1U<<i))!=0;
    return c;
}
g1::M3GBattleControllerState state(std::istream& input) {
    const auto p=read<10>(input);g1::M3GBattleControllerState s;
    s.phase=static_cast<std::uint8_t>(p[0]);s.complete=p[1]!=0;s.camera_side_a=p[2]!=0;
    s.embedded_current=p[3]!=0;s.elapsed_ms=p[4];s.slide_fixed16=p[5];
    s.clocks={{p[6],p[7]}};s.display_health={{p[8],p[9]}};return s;
}
std::array<std::int32_t,10> values(const g1::M3GBattleControllerState& s) {
    return {{s.phase,s.complete,s.camera_side_a,s.embedded_current,s.elapsed_ms,s.slide_fixed16,
             s.clocks[0],s.clocks[1],s.display_health[0],s.display_health[1]}};
}
std::array<std::int32_t,15> values(const g1::M3GBattleEvent& e) {
    return {{std::int32_t(e.kind),std::int32_t(e.target),e.time,e.clip,e.clock,e.delta,e.id,e.adjust,
             e.field,e.value,e.operation,e.argument,e.restart,e.side_a,e.hit}};
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("expected original-controller vector file");
        std::ifstream file(argv[1]);std::string magic;file>>magic;
        check(magic=="G1_BATTLE_CONTROLLER_1","unexpected fixture schema");
        std::size_t count=0;file>>count;
        for(std::size_t i=0;i<count;++i) {
            std::string name;file>>name;const auto c=config(file);const auto before=state(file);
            const auto delta=read<1>(file)[0];const auto expected=read<10>(file);
            const auto actual=g1::advance_m3g_battle_controller(c,before,delta);
            const auto actual_state=values(actual.state);
            for(std::size_t n=0;n<expected.size();++n)
                check(expected[n]==actual_state[n],name+" state["+std::to_string(n)+"] expected"+
                      std::to_string(expected[n])+" actual"+std::to_string(actual_state[n]));
            std::size_t events=0;file>>events;
            check(events==actual.events.size(),name+" event count expected"+std::to_string(events)+
                  " actual"+std::to_string(actual.events.size()));
            for(std::size_t e=0;e<events;++e) {
                const auto expected_event=read<15>(file),actual_event=values(actual.events[e]);
                for(std::size_t n=0;n<expected_event.size();++n)
                    check(expected_event[n]==actual_event[n],name+" event"+std::to_string(e)+" field"+
                          std::to_string(n)+" expected"+std::to_string(expected_event[n])+" actual"+std::to_string(actual_event[n]));
            }
        }
        std::size_t draws=0;file>>draws;
        for(std::size_t i=0;i<draws;++i) {
            std::string name;file>>name;const auto c=config(file);const auto s=state(file);
            const bool refresh=read<1>(file)[0]!=0;const auto expected=read<12>(file);
            const auto actual=g1::m3g_battle_draw_plan(c,s,refresh);
            std::array<std::int32_t,12> actual_values{};actual_values[0]=actual.render_3d;
            for(std::size_t n=0;n<7;++n)actual_values[1+n]=actual.root_enabled[n];
            actual_values[8]=actual.damage_text;actual_values[9]=actual.damage_x;actual_values[10]=actual.damage_y;
            actual_values[11]=actual.slide_fixed16;
            for(std::size_t n=0;n<expected.size();++n)
                check(expected[n]==actual_values[n],name+" draw field"+std::to_string(n));
        }
        std::string trailing;check(!(file>>trailing),"trailing controller fixture data");
        std::cout<<"{\"original_updates\":"<<count<<",\"original_draws\":"<<draws
                 <<",\"checks\":"<<checks<<"}\n";return 0;
    }catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
