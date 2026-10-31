#pragma once

#include "g1/Combat.hpp"
#include "g1/MovementAction.hpp"

namespace g1 {

enum class HealEligibility { allowed, inactive, action_used, form_cannot_heal };

// Original command outer active/flag2c gate plus0x4a73c selected-form field7==1.
// Health and funds are checked on target confirmation, not command admission.
// Caller still owns current-player, stationary-phase and higher UI/script locks.
HealEligibility heal_eligibility(const CombatUnitState& actor,
                                 const MovementUnitState& actor_movement,
                                 const UnitDefinition& actor_definition);

// Original0x4926a(min1,max1,skip opposing owner), then actor appended last.
// Active same-owner orthogonal neighbors are ordered by original type; health,
// offsets, funds, terrain and LOS do not filter this list.
std::vector<std::size_t> healing_targets(const std::vector<CombatUnitState>&,
                                       std::size_t actor);

enum class HealOutcome { healed, no_health_missing, insufficient_funds };
struct HealResult {
    CombatUnitState target;
    MovementUnitState actor_movement;
    HealOutcome outcome{HealOutcome::no_health_missing};
    std::int32_t funds_after{};
    std::int32_t spent{};
    std::int32_t requested_health{};
    std::optional<std::uint32_t> message_id;
    std::optional<std::uint8_t> audio_event_in_group5;
};

// Native0x588e0 core after ordinary admission and a chosen friendly/self target.
// Charges CURRENT player's funds. Preserves original10-HP increments and the
// extra increment at an exact multiple of10; limited funds can partially heal.
// Success marks the actor acted, clears target timer30 only, and requests event15.
// Failure does not spend funds or mark the actor. Copies make rejection atomic.
HealResult apply_heal(const CombatUnitState& actor,
                      const MovementUnitState& actor_movement,
                      const UnitDefinition& actor_definition,
                      const CombatUnitState& target,
                      std::int32_t current_player_funds);

} // namespace g1
