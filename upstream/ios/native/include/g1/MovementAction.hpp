#pragma once

#include "g1/Movement.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace g1 {

struct MovementTile {
    std::uint8_t x{};
    std::uint8_t y{};
};

enum class MovementRouteStatus {
    ready,
    already_at_destination,
    unreachable,
    disconnected_field,
    cyclic_field,
    original_storage_limit,
};

struct MovementRoute {
    MovementRouteStatus status{MovementRouteStatus::unreachable};
    MovementTile origin{};
    MovementTile destination{};
    // Forward order, excluding origin and including destination. Reconstructs
    // 0x49e90, including its UP, DOWN, LEFT, RIGHT tie preference.
    // already_at_destination has no travel steps. If separately admitted by
    // an AI/gameplay caller, it still begins moving and completes on one tick.
    std::vector<MovementTile> steps;
};

// Invalid dimensions/coordinates throw. Cyclic or disconnected budget fields
// are reported explicitly instead of guessing a different route. Original
// unit route storage is 2*(width+height), including a trailing (-1,-1) sentinel;
// the unit's route cursor is eight bits. Those bounds are enforced here.
MovementRoute choose_movement_route(const MovementRange&, MovementTile origin,
                                    MovementTile destination);

// Decoded native gameplay state, not a guest-memory layout. Fields whose wider
// gameplay meaning is unresolved retain their original offset as a name.
struct MovementUnitState {
    bool active{true};
    std::uint8_t form{};
    std::uint8_t tile_x{};
    std::uint8_t tile_y{};
    std::int32_t offset_x{};
    std::int32_t offset_y{};
    std::uint8_t facing{1}; // Original +0x4b: up=0, down=1, left=2, right=3.
    std::uint8_t spent_budget{};
    std::int32_t timer_ms{-1}; // Separate original +0x30 countdown.
    std::uint8_t anchor_x{};
    std::uint8_t anchor_y{};
    std::uint8_t anchor_facing{1};
    std::uint8_t flag_2c{};
    std::uint8_t flag_2d{};
    std::uint8_t flag_2f{};
    std::uint8_t flag_38{};
    std::uint8_t flag_39{};
    std::uint8_t flag_3a{};
};

// Exact unit-only 0x430fc transition, also used by the original objective/turn
// reset. Does not change form, facing, anchor_facing, offsets or timer.
void reset_movement_turn(MovementUnitState&) noexcept;

enum class MovementTickResult { inactive, travelling, waypoint_reached, completed, stationary };
enum class MovementUndoResult { restored, still_moving, unsupported_special_state };

class MovementAction {
public:
    // Reconstructs unit changes made by 0x51364 -> 0x43e2e. The caller must
    // already authorize the move through gameplay/occupancy rules. This class
    // does not admit transport, enemy destinations, selection or turn actions.
    // An admitted already_at_destination route preserves original record0 at
    // the origin and its trailing sentinel internally; it is not completed by
    // this constructor. Human same-tile click eligibility remains the caller's.
    // form0_field2 is signed stat byte 2 of FORM ZERO, even when moving in form1.
    MovementAction(MovementUnitState, const MovementProfile&, const MovementRange&,
                   MovementRoute, std::int8_t form0_field2);

    // ONE original simulation update, never one display refresh by implication.
    // Motion uses fixed per-tick increments. delta_ms only updates timer_ms.
    // Completion stops before map callback 0x59798 (arrival, transport, UI,
    // facing-nearby-enemies and mission effects are outside this module).
    MovementTickResult advance_tick(std::uint32_t delta_ms);

    // Only the ordinary branch 0x5a2e6 -> 0x4324a -> 0x430fc. Caller owns menu
    // eligibility and world synchronization. Transform/special flags reject
    // without mutation. Does not promise rollback of subsequent world effects.
    MovementUndoResult undo_ordinary();

    const MovementUnitState& state() const noexcept { return state_; }
    const MovementRoute& route() const noexcept { return route_; }
    std::uint8_t pending_cost() const noexcept { return pending_cost_; }
    std::uint8_t route_index() const noexcept { return route_index_; }
    bool moving() const noexcept { return moving_; }

private:
    MovementUnitState state_;
    MovementRoute route_;
    std::int8_t form0_field2_{};
    std::uint8_t pending_cost_{};
    std::uint8_t route_index_{};
    bool moving_{true};
};

} // namespace g1
