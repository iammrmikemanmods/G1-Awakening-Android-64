#include "g1/AIExecution.hpp"
#include "g1/TransformAction.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {
std::size_t cases{},checks{};
void expect(bool okay,const char* message) {
    ++checks;
    if (!okay) throw std::runtime_error(std::string(message)+" in case "+std::to_string(cases));
}
void tag(std::istream& in,const char* wanted) {
    std::string value;
    if (!(in>>value) || value!=wanted) throw std::runtime_error(std::string("Expected ")+wanted);
}
int read(std::istream& in) {int v;if (!(in>>v)) throw std::runtime_error("Truncated AI execution fixture");return v;}
}
int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("Expected original AI execution fixture");
        std::ifstream in(argv[1]);tag(in,"G1_AI_EXECUTION_V1");
        std::string next;
        while(in>>next && next!="END") {
            if(next!="C")throw std::runtime_error("Expected AI execution case");
            ++cases;(void)read(in);
            g1::AIPlan plan;
            plan.action=static_cast<std::uint8_t>(read(in));
            (void)read(in); // Original same-tile generator flag; destination is explicit.
            g1::MovementUnitState unit;
            unit.form=static_cast<std::uint8_t>(read(in));
            plan.movement_form=static_cast<std::uint8_t>(read(in));
            plan.transform_on_arrival=read(in)!=0;
            plan.destination.x=static_cast<std::uint8_t>(read(in));
            plan.destination.y=static_cast<std::uint8_t>(read(in));
            std::vector<g1::WorldSite> sites(static_cast<std::size_t>(read(in)));
            for(auto& site:sites) {
                tag(in,"S");site.position.x=static_cast<std::uint8_t>(read(in));
                site.position.y=static_cast<std::uint8_t>(read(in));
                site.coordinate_groups.resize(static_cast<std::size_t>(read(in)));
                for(auto& group:site.coordinate_groups) {
                    tag(in,"I");group.resize(static_cast<std::size_t>(read(in)));
                    for(auto& item:group) {item.x=static_cast<std::uint8_t>(read(in));item.y=static_cast<std::uint8_t>(read(in));}
                }
            }
            tag(in,"R");
            const auto expected_form=read(in),acted=read(in),transformed=read(in),pending=read(in),
                       gate_type=read(in),delay=read(in),event_count=read(in);
            std::vector<std::string> expected;
            for(int e=0;e<event_count;++e) {
                tag(in,"E");std::string event;std::getline(in,event);expected.push_back(event.substr(1));
            }
            const auto result=g1::prepare_ai_plan(plan,unit,sites);
            std::vector<std::string> events;
            std::uint8_t animation{};int gate=-1;
            g1::UnitDefinition definition;definition.type=11;definition.forms.assign(2,g1::Bytes(14));
            auto transform=[&] {
                const auto changed=g1::transform_unit(unit,definition,0,animation,true,false);
                unit=changed.unit;animation=changed.animation_pending_2e;
                events.emplace_back("transform 11");
            };
            if(result.transform_to_movement_form) {
                transform();events.emplace_back("render_transform 0 0");gate=11;
            }
            if(result.transform_again) {
                transform();if(result.second_transform_sets_attack_gate)gate=11;
            }
            for(auto phase:result.phase_requests)
                events.push_back("phase "+std::to_string(static_cast<int>(phase))+" 11");
            if(result.upgrade_immediately)events.emplace_back("upgrade 11");
            expect(events==expected,"Original action dispatch sequence differs");
            expect(unit.form==expected_form,"Original resulting form differs");
            expect(unit.flag_2c==acted && unit.flag_2d==transformed && animation==pending,"Original transform flags differ");
            expect(gate==gate_type,"Original attack transform gate differs");
            expect(delay==0,"Original plan consumption did not reset its delay");
        }
        expect(next=="END" && cases>0,"Missing fixture end/cases");
        if(in>>next)throw std::runtime_error("Trailing fixture data");
        std::cout<<cases<<" original plan-consumer cases / "<<checks<<" checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
