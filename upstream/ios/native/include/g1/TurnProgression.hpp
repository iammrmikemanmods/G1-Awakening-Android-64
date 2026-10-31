#pragma once

#include "g1/CaptureAction.hpp"
#include "g1/MovementAction.hpp"

namespace g1 {

struct TurnState {
    std::uint8_t current_player{};
    std::array<std::int8_t,2> controllers{{0,1}}; // Exactly1 is AI in original.
    std::array<std::int32_t,2> funds{};
    std::int32_t human_turn_counter_274{};
    std::int32_t ai_unit_cursor{};
    bool income_notice_pending{};
};

struct TurnTransition {
    bool player_changed{};
    std::optional<WorldCoordinate> camera_focus;
    bool snapshot_requested{};
};

// Original4e80c, also called by the map-load opcode9 before its first turn.
// Recomputes network income, credits only the current owner, and initializes
// that owner's human counter or AI cursor. Does not switch/reset unit actions.
void begin_current_player_turn(CaptureNetwork&, std::array<std::int8_t,2> player_factions,
                               TurnState&);

// Original55840(true), including actual4e8e8->4e80c turn-income writes and
// outgoing-player430fc resets. Units are matched by vector index; original
// owner-slot selection order is reconstructed from ascending type.
//
// Caller must first authorize an end-turn command in a stationary gameplay
// phase. This function does not run the incoming AI, invent a victory when a
// player has no units, move the camera, draw strings or persist a snapshot.
// Those requests remain explicit. All input validation precedes mutation.
TurnTransition advance_player_turn(const std::vector<WorldUnit>& roster,
    std::vector<MovementUnitState>& units, CaptureNetwork&,
    std::array<std::int8_t,2> player_factions, TurnState&);

} // namespace g1
