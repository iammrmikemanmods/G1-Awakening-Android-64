#include "g1/TechSpecs.hpp"
#include <cassert>
#include <stdexcept>

int main() {
    g1::UnitDefinition b{};b.type=11;b.scalar_fields={0,17,1,10,0};
    b.forms={g1::Bytes{2,1,1,65,75,55,0,0,0,0,1,0,0,0},
             g1::Bytes{5,1,1,0,0,65,0,0,0,0,0,0,0,1}};
    auto v=g1::read_tech_specs(b,0);
    assert(v.type==11 && v.faction==0 && v.forms==2 && v.energon_price==17);
    assert(v.form[0].combat.attack_low==65 && v.form[0].combat.attack_high==75);
    assert(v.form[0].combat.defense==55 && v.form[0].speed==2);
    assert(v.form[1].combat.attack_low==0 && v.form[1].combat.attack_high==0);
    assert(v.form[1].combat.defense==65 && v.form[1].speed==5);
    assert(v.form[0].min_range==1 && v.form[0].max_range==1);
    assert(g1::tech_specs_model_resource(11)==867);
    g1::UnitDefinition f=b;f.type=19;f.scalar_fields={1,8,0,5,0};
    f.forms[0]=g1::Bytes{3,1,1,45,55,30,0,0,1,0,1,0,0,0};
    auto k=g1::read_tech_specs(f,0);
    assert(k.faction==1 && k.energon_price==8 && k.form[0].speed==3);
    assert(k.form[0].combat.attack_low==45 && k.form[0].combat.attack_high==55);
    assert(k.form[0].combat.defense==30 && g1::tech_specs_model_resource(19)==1201);
    // Level scaling reads original combat formula and does not affect cost/SPD.
    auto up=g1::read_tech_specs(b,2);
    assert(up.form[0].combat.attack_low==91 && up.form[0].combat.attack_high==105);
    assert(up.form[0].combat.defense==77 && up.form[0].speed==2 && up.energon_price==17);
    assert(b.forms[0][3]==65); // Presentation never edits the canonical unit.
    g1::UnitDefinition wrong=b;wrong.forms[0].resize(13);
    try { (void)g1::read_tech_specs(wrong,0);assert(false); }catch(const std::invalid_argument&){}
    try { (void)g1::tech_specs_model_resource(23);assert(false); }catch(const std::invalid_argument&){}
}
