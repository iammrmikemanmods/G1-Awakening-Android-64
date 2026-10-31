#include "g1/UndoAction.hpp"
#include "g1/UnitCommands.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Values=std::vector<std::int64_t>;
void check(bool ok,const std::string& message) { if (!ok) throw std::runtime_error(message); }
std::uint8_t byte(std::int64_t value) { return static_cast<std::uint8_t>(value); }
std::int32_t word(std::int64_t value) { return static_cast<std::int32_t>(value); }

struct Input {
    g1::UnitDefinition definition;
    g1::MovementUnitState movement;
    g1::CombatUnitState combat;
    std::uint8_t pending{};
    bool moving{};
};

Input decode(const Values& input,std::size_t offset) {
    check(input.size()==offset+57,"Original fixture input shape differs");
    const auto* r=input.data()+offset;
    Input value;
    auto& m=value.movement;auto& u=value.combat;auto& d=value.definition;
    d.type=byte(r[0]);
    d.forms.resize(2);
    for (std::size_t form=0;form<2;++form)
        for (std::size_t field=0;field<14;++field)
            d.forms[form].push_back(byte(r[29+form*14+field]));
    u.type=d.type;u.active=r[1]!=0;u.form=byte(r[2]);u.x=byte(r[3]);u.y=byte(r[4]);
    m.active=u.active;m.form=u.form;m.tile_x=u.x;m.tile_y=u.y;
    m.offset_x=word(r[5]);m.offset_y=word(r[6]);m.facing=byte(r[7]);
    m.spent_budget=byte(r[8]);m.timer_ms=word(r[9]);u.timer_30_ms=m.timer_ms;
    m.anchor_x=byte(r[10]);m.anchor_y=byte(r[11]);m.anchor_facing=byte(r[12]);
    m.flag_2c=byte(r[13]);m.flag_2d=byte(r[14]);m.flag_2f=byte(r[15]);
    m.flag_38=byte(r[16]);m.flag_39=byte(r[17]);m.flag_3a=byte(r[18]);
    u.flag_38=m.flag_38;u.health=byte(r[19]);u.level=byte(r[20]);u.damage_immune=r[21]!=0;
    u.stats={static_cast<std::int16_t>(r[22]),static_cast<std::int16_t>(r[23]),
             static_cast<std::int16_t>(r[24])};
    u.support_bonus=word(r[25]);u.preset_34_ms=word(r[26]);
    value.pending=byte(r[27]);value.moving=r[28]!=0;
    return value;
}

Values state_values(const g1::MovementUnitState& m,const g1::CombatUnitState& u,
                    std::uint8_t pending,bool moving) {
    return {u.type,m.active,m.form,m.tile_x,m.tile_y,m.offset_x,m.offset_y,m.facing,
        m.spent_budget,m.timer_ms,m.anchor_x,m.anchor_y,m.anchor_facing,m.flag_2c,
        m.flag_2d,m.flag_2f,m.flag_38,m.flag_39,m.flag_3a,u.health,u.level,u.damage_immune,
        u.stats.attack_low,u.stats.attack_high,u.stats.defense,u.support_bonus,
        u.preset_34_ms,pending,moving};
}

Values evaluate(int kind,const Values& input,std::size_t parameter_count) {
    auto value=decode(input,parameter_count);
    auto& m=value.movement;auto& u=value.combat;const auto& d=value.definition;
    switch (kind) {
    case 1:
        check(parameter_count==1,"Availability parameters differ");
        return {input[0]!=0 && g1::undo_available(m,d)};
    case 2: {
        check(parameter_count==0,"Undo parameters differ");
        const auto result=g1::undo_unit_action(m,u,d,value.pending,value.moving);
        check(result.restored_anchor==(m.flag_3a==0),"Undo anchor branch metadata differs");
        check(result.reversed_transform==(m.flag_2d!=0),"Undo transform branch metadata differs");
        check(result.requested_selection_mode==2,"Undo selection mode differs");
        auto out=state_values(result.movement,result.combat,result.animation_pending_2e,
                              result.moving_after);
        out.insert(out.end(),{result.retain_selection,result.cursor.x,result.cursor.y,
            result.range_kind==g1::UndoRangeKind::signed_base_with_attack_envelope,
            result.audio_operation,result.audio_argument,result.pending_move_after});
        return out;
    }
    case 3:
        check(parameter_count==1,"Selection parameters differ");
        if (input[0]==2) g1::lock_undo_on_friendly_selection_change(m,d);
        return state_values(m,u,value.pending,value.moving);
    case 4: {
        check(parameter_count==0,"End action parameters differ");
        const auto result=g1::end_unit_action(m);
        check(result.close_command_menu,"End action must request actual menu closure");
        auto out=state_values(result.movement,u,value.pending,value.moving);
        out.push_back(result.clear_selection);
        return out;
    }
    case 5: {
        check(parameter_count==2,"Mask parameters differ");
        const auto flags=static_cast<unsigned>(input[1]);
        const g1::UnitCommandPredicates p{(flags&1)!=0,(flags&2)!=0,(flags&4)!=0,
            (flags&8)!=0,(flags&16)!=0,(flags&32)!=0,(flags&64)!=0,
            (flags&128)!=0,(flags&256)!=0};
        return {g1::unit_command_mask(input[0]!=0 ? &m : nullptr,p)};
    }
    default:throw std::runtime_error("Unknown original fixture kind");
    }
}

std::size_t native_guards() {
    std::size_t checks=0;
    Input sample;
    sample.definition.type=11;
    sample.definition.forms={{3,1,1,20,30,5,0,0,0,0,0,0,0,1},
                             {6,1,2,40,50,8,0,0,0,0,0,0,0,1}};
    sample.combat.type=11;sample.movement.spent_budget=1;
    const auto reject=[&checks](auto call) {
        bool rejected=false;
        try { call(); } catch (const std::invalid_argument&) { rejected=true; }
        check(rejected,"Malformed or ineligible action was silently accepted");++checks;
    };
    for (int field=0;field<7;++field) {
        auto bad=sample;
        switch(field) {
        case 0:bad.combat.type=12;break;
        case 1:bad.combat.form=1;break;
        case 2:bad.combat.active=false;break;
        case 3:bad.combat.x=1;break;
        case 4:bad.combat.y=1;break;
        case 5:bad.combat.flag_38=1;break;
        case 6:bad.combat.timer_30_ms=0;break;
        }
        reject([&]{(void)g1::undo_unit_action(bad.movement,bad.combat,bad.definition,0,false);});
    }
    for (int field=0;field<3;++field) {
        auto bad=sample;
        if (field==0) bad.definition.forms.clear();
        if (field==1) bad.definition.forms[0].pop_back();
        if (field==2) bad.movement.form=2;
        reject([&]{(void)g1::undo_available(bad.movement,bad.definition);});
    }
    auto acted=sample;acted.movement.flag_2c=1;
    reject([&]{(void)g1::undo_unit_action(acted.movement,acted.combat,acted.definition,0,false);});
    auto unchanged=sample;unchanged.movement.spent_budget=0;
    reject([&]{(void)g1::undo_unit_action(unchanged.movement,unchanged.combat,unchanged.definition,0,false);});
    auto inactive=sample.movement;inactive.active=false;
    reject([&]{(void)g1::end_unit_action(inactive);});
    reject([&]{(void)g1::end_unit_action(acted.movement);});
    auto invalid=sample;invalid.definition.forms[1].clear();
    const auto before=state_values(invalid.movement,invalid.combat,0,false);
    reject([&]{g1::lock_undo_on_friendly_selection_change(invalid.movement,invalid.definition);});
    check(state_values(invalid.movement,invalid.combat,0,false)==before,
          "Malformed selection-lock input changed caller state");++checks;
    check(g1::has_unit_command(0x201,g1::UnitCommand::move) &&
          g1::has_unit_command(0x201,g1::UnitCommand::upgrade) &&
          !g1::has_unit_command(0x201,g1::UnitCommand::produce),"Mask query differs");++checks;
    return checks;
}
}

int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("Usage: unit_commands_test ORIGINAL_VECTOR_FILE");
        std::ifstream input(argv[1]);std::string magic;std::size_t count=0;
        if (!(input>>magic>>count) || magic!="G1UNITCOMMANDS1")
            throw std::runtime_error("Invalid original command vector header");
        std::size_t scalar_checks=0;
        for (std::size_t index=0;index<count;++index) {
            int kind=0;std::string name;std::size_t parameters=0,ninputs=0,noutputs=0;
            if (!(input>>kind>>name>>parameters>>ninputs>>noutputs))
                throw std::runtime_error("Truncated original command vector header");
            Values in(ninputs),expected(noutputs);
            for (auto& v:in) if (!(input>>v)) throw std::runtime_error("Truncated original input");
            for (auto& v:expected) if (!(input>>v)) throw std::runtime_error("Truncated original output");
            const auto actual=evaluate(kind,in,parameters);
            if (actual!=expected) {
                std::cerr<<"Mismatch "<<name<<" kind "<<kind<<"\nexpected:";
                for (auto v:expected) std::cerr<<' '<<v;
                std::cerr<<"\nactual:";for (auto v:actual) std::cerr<<' '<<v;
                std::cerr<<'\n';throw std::runtime_error("Native command state differs from original");
            }
            scalar_checks+=actual.size();
        }
        std::string excess;check(!(input>>excess),"Original fixture has unexpected trailing data");
        const auto guards=native_guards();
        std::cout<<count<<" original command/undo vectors matched ("<<scalar_checks
                 <<" scalars); "<<guards<<" native guards passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
