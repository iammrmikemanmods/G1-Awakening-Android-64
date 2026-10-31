#pragma once

#include "g1/Combat.hpp"
#include "g1/MovementAction.hpp"

namespace g1 {

enum class TransformEligibility {
    allowed, inactive, action_used, type_cannot_transform,
    form_disallows_transform, terrain_disallows_transform, already_transformed,
};

// Outer active/flag2c command gate plus exact 0x4a6c0. The type flag is raw
// scalar_fields[2], normalized to boolean by original loader0x5a8f0.
// Terrain arguments are signed rule fields0 and2 at the unit's current cell.
// Caller owns current-player selection and the stationary command phase.
TransformEligibility transform_eligibility(const MovementUnitState&,
                                            const UnitDefinition&,
                                            std::int32_t terrain_field0,
                                            std::int32_t terrain_field2);

struct TransformResult {
    MovementUnitState unit;
    std::uint8_t animation_pending_2e{};
    CombatStats stats;
    std::optional<std::uint8_t> audio_event_in_group5;
};

// Unit transition0x43a54, including recalculation0x4328a at the unchanged level.
// Caller must authorize an ordinary transform before calling record=true.
// record=false is the unit-only reverse transition used by undo; it does not
// imply that all map/renderer effects of an action have been rolled back.
// Input is copied, so malformed definitions fail without partial mutation.
TransformResult transform_unit(const MovementUnitState&, const UnitDefinition&,
                               std::uint8_t level, std::uint8_t animation_pending_2e,
                               bool record_transition, bool audio_enabled);

} // namespace g1
