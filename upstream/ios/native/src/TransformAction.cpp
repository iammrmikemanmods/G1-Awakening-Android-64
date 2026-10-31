#include "g1/TransformAction.hpp"

#include <stdexcept>

namespace g1 {
namespace {
const Bytes& form_bytes(const UnitDefinition& definition, std::uint8_t form) {
    if (form >= definition.forms.size() || definition.forms[form].size() != 14)
        throw std::invalid_argument("Transform needs a decoded fourteen-byte form");
    return definition.forms[form];
}
}

TransformEligibility transform_eligibility(const MovementUnitState& unit,
                                            const UnitDefinition& definition,
                                            std::int32_t terrain0, std::int32_t terrain2) {
    const auto& form = form_bytes(definition, unit.form);
    if (!unit.active) return TransformEligibility::inactive;
    if (unit.flag_2c != 0) return TransformEligibility::action_used;
    if (definition.scalar_fields[2] == 0) return TransformEligibility::type_cannot_transform;
    if (form[8] == 1) return TransformEligibility::form_disallows_transform;
    if (terrain0 == 99 && terrain2 == 0) return TransformEligibility::terrain_disallows_transform;
    if (unit.flag_2d != 0) return TransformEligibility::already_transformed;
    return TransformEligibility::allowed;
}

TransformResult transform_unit(const MovementUnitState& unit, const UnitDefinition& definition,
                               std::uint8_t level, std::uint8_t pending,
                               bool record, bool audio_enabled) {
    TransformResult result;
    result.unit = unit;
    result.animation_pending_2e = pending;
    result.unit.form = unit.form == 0 ? 1 : 0;
    const auto& form = form_bytes(definition, result.unit.form);
    result.stats = combat_stats(definition, result.unit.form, level);
    if (record) {
        result.unit.flag_2d = 1;
        result.animation_pending_2e = 1;
        result.unit.flag_2f = 0;
        if (form[13] == 1) result.unit.facing = 1;
        if (audio_enabled) result.audio_event_in_group5 = result.unit.form == 1 ? 7 : 6;
    }
    return result;
}

} // namespace g1
