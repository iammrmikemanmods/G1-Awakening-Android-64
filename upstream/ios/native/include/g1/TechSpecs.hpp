#pragma once

#include "g1/Combat.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>

namespace g1 {

// Portrait/Tech Specs read the original definition table; no independently
// maintained attack, defense, speed, or Energon prices. These records do NOT
// modify GameSession units, campaign progression, or save format.
struct TechSpecsForm {
    CombatStats combat{};
    std::uint8_t speed{}, min_range{}, max_range{};
};

struct TechSpecsData {
    std::uint8_t type{}, faction{}, level{}, forms{};
    std::int8_t energon_price{};
    std::array<TechSpecsForm,2> form{};
};

// Original type->M3G binding table used by the reconstructed native combat
// presenter (0xceb14 is for material images, not model resource identities).
constexpr std::array<std::uint32_t,23> tech_specs_model_resources{{
    408,699,444,492,1300,561,369,565,634,395,475,867,
    1039,631,795,967,564,1299,829,1201,1070,489,490
}};

inline std::uint32_t tech_specs_model_resource(std::uint8_t type) {
    if(type>=tech_specs_model_resources.size())
        throw std::invalid_argument("Tech Specs type is outside the original model roster");
    return tech_specs_model_resources[type];
}

inline TechSpecsData read_tech_specs(const UnitDefinition& unit, std::uint8_t level) {
    if(unit.type>=tech_specs_model_resources.size() || unit.forms.empty() || unit.forms.size()>2)
        throw std::invalid_argument("Tech Specs original unit definition is unsupported");
    TechSpecsData specs{};
    specs.type=unit.type;specs.faction=unit.faction();specs.level=level;
    specs.energon_price=unit_spawn_cost(unit);
    specs.forms=static_cast<std::uint8_t>(unit.forms.size());
    for(std::size_t form=0;form<unit.forms.size();++form) {
        const auto& raw=unit.forms.at(form);
        if(raw.size()!=14)throw std::invalid_argument("Tech Specs original form is not 14 bytes");
        specs.form[form]={combat_stats(unit,static_cast<std::uint8_t>(form),level),
            raw[0],raw[1],raw[2]};
    }
    return specs;
}

} // namespace g1
