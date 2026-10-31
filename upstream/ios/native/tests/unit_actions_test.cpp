#include "g1/HealAction.hpp"
#include "g1/UpgradeAction.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::size_t checks=0,cases=0;
std::int64_t read(std::istream& in) {
    std::int64_t n;
    if (!(in>>n)) throw std::runtime_error("Truncated unit action fixture");
    return n;
}
void tag(std::istream& in,const std::string& expected) {
    std::string s;
    if (!(in>>s) || s!=expected) throw std::runtime_error("Expected unit action tag "+expected);
}
void equal(std::int64_t actual,std::int64_t expected,const char* what) {
    ++checks;
    if (actual!=expected) throw std::runtime_error(std::string(what)+" in case "+std::to_string(cases)+
        ": got "+std::to_string(actual)+" expected "+std::to_string(expected));
}
template<class F>void rejects(F f) {
    ++checks;
    try {f();}catch(const std::invalid_argument&){return;}
    throw std::runtime_error("Unsupported unit action accepted");
}
}

int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::invalid_argument("Usage: g1_unit_action_tests ORIGINAL_FIXTURE");
        std::ifstream in(argv[1]);
        if (!in) throw std::runtime_error("Cannot open unit action fixture");
        tag(in,"G1_ACTIONS_V1");
        std::string kind;
        while (in>>kind && kind!="END") {
            if (kind!="C") throw std::runtime_error("Expected unit action case");
            ++cases;(void)read(in);
            const auto actor=static_cast<std::size_t>(read(in));
            const auto current=static_cast<std::uint8_t>(read(in));
            std::array<std::int32_t,2> funds{},stored{};
            for (auto& v:funds)v=static_cast<std::int32_t>(read(in));
            for (auto& v:stored)v=static_cast<std::int32_t>(read(in));
            const auto count=read(in);
            if (count!=23 || current>1 || actor>=23) throw std::runtime_error("Wrong action fixture shape");
            std::vector<g1::UnitDefinition> definitions(23);
            std::vector<g1::CombatUnitState> units;
            std::vector<g1::MovementUnitState> motion;
            std::array<std::vector<std::uint8_t>,2> slots;
            for (std::int64_t i=0;i<count;++i) {
                tag(in,"U");
                g1::CombatUnitState u;
                u.type=static_cast<std::uint8_t>(read(in));u.owner=static_cast<std::uint8_t>(read(in));
                u.faction=static_cast<std::uint8_t>(read(in));u.form=static_cast<std::uint8_t>(read(in));
                u.level=static_cast<std::uint8_t>(read(in));u.health=static_cast<std::uint8_t>(read(in));
                u.active=read(in)!=0;u.damage_immune=read(in)!=0;
                u.x=static_cast<std::uint8_t>(read(in));u.y=static_cast<std::uint8_t>(read(in));
                g1::MovementUnitState m;
                m.active=u.active;m.form=u.form;m.tile_x=u.x;m.tile_y=u.y;
                m.flag_2c=static_cast<std::uint8_t>(read(in));
                u.timer_30_ms=static_cast<std::int32_t>(read(in));u.preset_34_ms=static_cast<std::int32_t>(read(in));
                auto& d=definitions.at(u.type);d.type=u.type;d.scalar_fields[0]=u.faction;
                d.scalar_fields[1]=static_cast<std::uint8_t>(read(in));d.forms.assign(2,g1::Bytes(14));
                for (auto& f:d.forms)for(auto& v:f)v=static_cast<std::uint8_t>(read(in));
                u.stats=g1::combat_stats(d,u.form,u.level);
                units.push_back(u);motion.push_back(m);slots.at(u.owner).push_back(u.type);
            }
            g1::UpgradeWorldView world{definitions,units,motion,slots};
            const auto& a=units.at(actor);const auto& m=motion.at(actor);
            const auto& d=definitions.at(a.type);
            tag(in,"G");
            const bool upgrade=read(in)!=0,heal=read(in)!=0;
            equal(g1::upgrade_eligibility(world,actor,funds[a.owner])==g1::UpgradeEligibility::allowed,
                  upgrade,"upgrade eligibility");
            equal(g1::heal_eligibility(a,m,d)==g1::HealEligibility::allowed,heal,"heal command eligibility");
            tag(in,"T");
            const auto target_count=read(in);
            if (target_count>=0) {
                const auto targets=g1::healing_targets(units,actor);
                equal(targets.size(),target_count,"heal target count");
                for (auto i:targets)equal(units[i].type,read(in),"heal target order");
            }
            if (upgrade) {
                tag(in,"A");
                const auto result=g1::apply_upgrade(world,actor,funds[a.owner]);
                const std::int64_t values[]={result.combat.level,result.combat.stats.attack_low,
                    result.combat.stats.attack_high,result.combat.stats.defense,result.movement.flag_2c,
                    result.combat.health,result.combat.timer_30_ms,result.combat.preset_34_ms};
                for(auto v:values)equal(v,read(in),"upgrade state");
                auto after=funds;after[a.owner]=result.funds_after;
                for(auto v:after)equal(v,read(in),"upgrade funds");
                auto selected=stored;selected[current]=g1::advance_upgrade_slot(world,current,stored[current]);
                for(auto v:selected)equal(v,read(in),"upgrade slot advancement");
            }
            if (heal) {
                tag(in,"H");
                const auto target=static_cast<std::size_t>(read(in));
                const auto result=g1::apply_heal(a,m,d,units.at(target),funds[current]);
                equal(static_cast<int>(result.outcome),read(in),"heal outcome");
                equal(result.target.health,read(in),"healed health");
                equal(result.target.timer_30_ms,read(in),"healed countdown");
                equal(result.target.preset_34_ms,read(in),"unchanged health preset");
                equal(result.actor_movement.flag_2c,read(in),"healer action spent");
                auto after=funds;after[current]=result.funds_after;
                for(auto v:after)equal(v,read(in),"heal funds");
                equal(result.requested_health,read(in),"heal increment request");
                const auto audio_count=read(in);
                equal(result.audio_event_in_group5.has_value(),audio_count,"heal audio requests");
                if (audio_count) {
                    equal(5,read(in),"heal audio group");
                    equal(*result.audio_event_in_group5,read(in),"heal audio event");
                }
            }
            tag(in,"S");
            for(std::uint8_t owner=0;owner<2;++owner)
                equal(g1::first_upgrade_slot(world,owner,stored[owner]),read(in),"post-spawn upgrade selection");
            auto after=stored;
            after[a.owner]=g1::upgrade_slot_after_removal(world,a.owner,a.type,stored[a.owner]);
            for(auto v:after)equal(v,read(in),"death upgrade selection");
            for(int i=0;i<2;++i)equal(-1,read(in),"original map reset slot");
            if(cases==1) {
                rejects([&]{g1::upgrade_eligibility(world,23,0);});
                rejects([&]{g1::first_upgrade_slot(world,2,-1);});
                rejects([&]{g1::advance_upgrade_slot(world,0,23);});
                rejects([&]{g1::upgrade_slot_after_removal(world,0,23,-1);});
                const auto acted=motion[actor].flag_2c;
                motion[actor].flag_2c=1;
                rejects([&]{g1::apply_upgrade(world,actor,funds[a.owner]);});
                rejects([&]{g1::apply_heal(a,m,d,a,10);});
                motion[actor].flag_2c=acted;
                std::vector<bool> active;
                for(std::size_t i=0;i<units.size();++i) {
                    active.push_back(units[i].active);units[i].active=false;motion[i].active=false;
                }
                rejects([&]{g1::advance_upgrade_slot(world,0,-1);});
                for(std::size_t i=0;i<units.size();++i)units[i].active=motion[i].active=active[i];
                rejects([&]{g1::healing_targets(units,23);});
            }
        }
        if(kind!="END" || cases==0)throw std::runtime_error("Missing unit action fixture end");
        if(in>>kind)throw std::runtime_error("Trailing unit action fixture input");
        std::cout<<"{\"cases\":"<<cases<<",\"checks\":"<<checks<<",\"passed\":true}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
