#pragma once

#include "g1/Combat.hpp"
#include "g1/MovementAction.hpp"

namespace g1 {

struct UnitSpawnModifiers {
    bool script_special_placement{}; // Opcode6 fifth word !=0: HP9, bytes18/19=1.
    bool grant_damage_immunity{}; // Explicit caller-resolved config+d9/controller0.
};

struct UnitActivation {
    WorldUnit world;
    MovementUnitState movement;
    CombatUnitState combat;
    std::int8_t signed_cost{};
    bool clear_pending_route{true}; // Original unit+0x24 becomes -1.
    std::uint8_t active_count_increment{1}; // Original0x54102..0x54110, wrapping byte.
};

// Original0x54016 activation/reset, including0x4333a and the terrain-forced
// form branch at0x5403e..0x54094. This is reusable for production and script
// placement. It does not authorize production, choose a random type, invert
// factions, deduct funds, update owner-slot arrays, or call a renderer.
//
// identity supplies the already resolved type/faction/owner. Coordinates are
// native bytes; the caller checks map bounds and ordinary production occupancy.
// terrain0/2 are the decoded unsigned rule bytes at the destination. Existing
// motion/combat fields not written by the original are preserved, including
// support_bonus and preset_34_ms. Caller retains any pending animation byte2e
// separately: ordinary activation does not clear it.
//
// The optional modifiers reproduce the later opcode6 HP/immunity writes, not
// ordinary production effects. All validation precedes mutation; input objects
// are copied. Funds use apply_spawn_funds_debit with the returned signed cost.
UnitActivation activate_unit(const UnitDefinition&, const WorldUnit& identity,
    WorldCoordinate destination, std::uint8_t terrain0, std::uint8_t terrain2,
    const MovementUnitState& previous_movement,
    const CombatUnitState& previous_combat, UnitSpawnModifiers = {});

} // namespace g1
