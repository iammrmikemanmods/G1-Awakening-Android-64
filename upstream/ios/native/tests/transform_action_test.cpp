#include "g1/TransformAction.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks{};
void check(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
g1::UnitDefinition definition() {
    g1::UnitDefinition d;
    d.type=11;d.scalar_fields={{0,17,1,10,0}};
    d.forms={{2,1,1,65,75,55,0,0,0,0,1,0,0,0},
             {5,1,1,0,0,65,0,0,0,0,0,0,0,1}};
    return d;
}
}
int main(){try{
    auto d=definition();g1::MovementUnitState unit;
    unit.tile_x=5;unit.tile_y=7;unit.facing=3;unit.spent_budget=2;
    using E=g1::TransformEligibility;
    check(g1::transform_eligibility(unit,d,1,0)==E::allowed,
          "ordinary transformation can remain available after movement");
    unit.active=false;
    check(g1::transform_eligibility(unit,d,1,0)==E::inactive,"inactive gate");unit.active=true;
    unit.flag_2c=1;
    check(g1::transform_eligibility(unit,d,1,0)==E::action_used,"used action gate");unit.flag_2c=0;
    unit.flag_2d=1;
    check(g1::transform_eligibility(unit,d,1,0)==E::already_transformed,"one recorded transform gate");unit.flag_2d=0;
    check(g1::transform_eligibility(unit,d,99,0)==E::terrain_disallows_transform,"blocked terrain gate");
    check(g1::transform_eligibility(unit,d,99,1)==E::allowed,"terrain field2 exception");
    d.scalar_fields[2]=0;
    check(g1::transform_eligibility(unit,d,1,0)==E::type_cannot_transform,"per-type flag");d.scalar_fields[2]=255;
    check(g1::transform_eligibility(unit,d,1,0)==E::allowed,"raw nonzero type flag normalized by loader");
    d.forms[0][8]=1;
    check(g1::transform_eligibility(unit,d,1,0)==E::form_disallows_transform,"selected form byte8 gate");d.forms[0][8]=0;
    unit.flag_2f=9;
    auto result=g1::transform_unit(unit,d,2,7,true,true);
    check(result.unit.form==1 && result.unit.flag_2d==1 && result.animation_pending_2e==1 && result.unit.flag_2f==0,
          "recorded form switch flags");
    check(result.unit.facing==1 && result.audio_event_in_group5==7,"form1 facing and audio request");
    check(result.stats.attack_low==0 && result.stats.attack_high==0 && result.stats.defense==91,
          "new form stats recomputed at unchanged level");
    check(result.unit.spent_budget==2 && result.unit.tile_x==5 && result.unit.tile_y==7 && result.unit.flag_2c==0,
          "transform preserves spent movement and action-used state");
    auto back=g1::transform_unit(result.unit,d,2,result.animation_pending_2e,true,true);
    check(back.unit.form==0 && back.unit.facing==1 && back.audio_event_in_group5==6 && back.stats.attack_low==91,
          "reverse form switch and unchanged facing when new byte13 is zero");
    result=g1::transform_unit(unit,d,0,7,false,true);
    check(result.unit.form==1 && result.unit.flag_2d==0 && result.animation_pending_2e==7 &&
          result.unit.flag_2f==9 && result.unit.facing==3 && !result.audio_event_in_group5,
          "unrecorded unit reversal preserves transition flags/facing and requests no audio");
    result=g1::transform_unit(unit,d,0,7,true,false);
    check(!result.audio_event_in_group5,"global audio setting suppresses request");
    d.forms[1].pop_back();bool threw{};
    try{g1::transform_unit(unit,d,0,0,true,true);}catch(const std::invalid_argument&){threw=true;}
    check(threw && unit.form==0 && unit.flag_2d==0 && unit.flag_2f==9,
          "malformed new form fails without touching caller state");
    std::cout<<"transform action contracts: "<<checks<<" passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
