#pragma once

#include "g1/CaptureAction.hpp"
#include "g1/Combat.hpp"
#include "g1/MovementAction.hpp"

namespace g1 {

// Native inputs to the original opponent planner. Unit vectors use the same
// indices; original owner-slot iteration is reconstructed by ascending type.
// The movement board must already contain current stationary occupancy.
struct AIWorldView {
    CombatRulesView combat;
    const MovementBoard& movement;
    const std::vector<CombatUnitState>& units;
    const std::vector<MovementUnitState>& movement_units;
    const std::vector<WorldSite>& sites;
    const CaptureNetwork& captures;
    std::array<std::int8_t, 2> player_factions;
    std::array<std::int8_t, 2> controllers;
    std::uint8_t current_player{};
    std::array<std::int32_t, 2> funds{};
    std::array<std::int32_t, 2> upgrade_slots_34{{-1,-1}};
    // Full original owner arrays include inactive/unspawned type objects.
    // Required for a nonnegative upgrade slot and ordinary driver traversal.
    std::array<std::vector<std::uint8_t>, 2> owner_slot_types{};
    std::array<bool, 23> available_types{};
};

// Recovered 0x24-byte plan's meaningful fields, represented without pointers.
// Action values are original discriminators: 0 move, 1 attack, 2 capture,
// 3 heal, 4 move then upgrade. Returning a plan does not implement any action;
// heal/upgrade require separately recovered execution adapters.
struct AIPlan {
    std::int32_t score{};
    std::uint8_t action{};
    std::int32_t target_type{};
    WorldCoordinate destination;
    std::int32_t site{}, group{}, item{};
    std::uint8_t movement_form{};
    bool transform_on_arrival{};
};

// Original attack candidate 0x50ed6 and score helper 0x4e8f4. These perform
// planning only: no unit motion, HP change, RNG draw or battle presentation.
std::int32_t ai_position_score(const AIWorldView&, std::size_t actor,
                              WorldCoordinate position,
                              std::optional<std::size_t> excluded_unit = {},
                              bool consider_enemy_alternate_form = false);
std::optional<AIPlan> choose_ai_attack(const AIWorldView&, std::size_t actor);

// Original site/item candidate 0x508a2. Separate ownership implementation
// delegates only the income recomputation to the recovered capture network.
std::optional<AIPlan> choose_ai_capture_plan(const AIWorldView&, std::size_t actor);

struct AIPlanningResult {
    std::optional<AIPlan> plan;
    // 0x4ec72 sometimes leaves its temporary form change in place. These
    // explicit results preserve that original side effect even with no plan.
    // Applying them must not invent action flags or a transform animation.
    std::uint8_t form_after_planning{};
    CombatStats stats_after_planning;
};

AIPlanningResult choose_ai_fallback_plan(const AIWorldView&, std::size_t actor);

// Original 0x512e6: attack vs capture, greater score wins; ties choose capture.
// Only when neither exists does it invoke the original fallback reconstruction.
AIPlanningResult plan_ai_unit(const AIWorldView&, std::size_t actor);

// Original 0x496e4 includes every type object, even inactive/unspawned ones.
// Live faction overrides take precedence over the decoded definition default.
std::array<std::vector<std::uint8_t>, 2> ai_owner_slots(
    const std::vector<UnitDefinition>&, const std::vector<CombatUnitState>&,
    std::array<std::int8_t, 2> player_factions);

enum class AITurnAdmissionKind { unit, production, turn_finished };
struct AITurnAdmission {
    AITurnAdmissionKind kind{AITurnAdmissionKind::turn_finished};
    std::optional<std::size_t> actor;
    std::optional<std::uint8_t> production_type;
    std::int32_t next_cursor{23};
};

// Original idle-phase 0x58dcc..0x58f4a: production priorities precede ordinary
// owner-slot traversal, then the caller requests the existing end-turn logic.
// For a unit result, commit next_cursor AFTER planning; production preserves
// the cursor. This chooses requests only, not spawning/HP/turn side effects.
AITurnAdmission choose_ai_turn_admission(const AIWorldView&, std::int32_t cursor);

// Original production phase 0x59102..0x59260, after 0x4a678's adjacency fill:
// choose the first vacant reachable neighbor in NORTH, WEST, EAST, SOUTH order.
// This does not create a unit or spend funds; absence is an explicit no-target
// result. Original signed-byte selected coordinates are preserved.
std::optional<WorldCoordinate> ai_production_destination(const AIWorldView&,
                                                       std::size_t producer);

} // namespace g1
