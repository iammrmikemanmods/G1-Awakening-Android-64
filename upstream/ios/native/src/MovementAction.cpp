#include "g1/MovementAction.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace g1 {
namespace {

bool same(MovementTile a, MovementTile b) noexcept { return a.x == b.x && a.y == b.y; }

void validate_range(const MovementRange& range) {
    if (range.width == 0 || range.height == 0 || range.width > 255 || range.height > 255 ||
        range.remaining.size() != std::size_t(range.width) * range.height)
        throw std::invalid_argument("movement route requires a complete 1..255 by 1..255 budget field");
}

void validate_tile(const MovementRange& range, MovementTile tile) {
    if (tile.x >= range.width || tile.y >= range.height)
        throw std::out_of_range("movement route coordinate is outside the budget field");
}

void move_axis(std::uint8_t& tile, std::int32_t& offset, std::int32_t delta) {
    offset += delta;
    if (offset > 15) {
        offset -= 32;
        tile = std::uint8_t(tile + 1U);
    } else if (offset < -16) {
        offset += 32;
        tile = std::uint8_t(tile - 1U);
    }
}

} // namespace

MovementRoute choose_movement_route(const MovementRange& range, MovementTile origin,
                                    MovementTile destination) {
    validate_range(range);
    validate_tile(range, origin);
    validate_tile(range, destination);
    MovementRoute result;
    result.origin = origin;
    result.destination = destination;
    if (same(origin, destination)) {
        result.status = MovementRouteStatus::already_at_destination;
        return result;
    }
    const auto remaining = range.at(destination.x, destination.y);
    if (remaining < 0 || remaining == movement_reserved_cell) return result;

    std::vector<MovementTile> backwards;
    std::vector<bool> visited(range.remaining.size());
    auto current = destination;
    const auto original_capacity = 2U * (std::size_t(range.width) + range.height);
    for (;;) {
        if (same(current, origin)) break;
        const auto index = std::size_t(current.y) * range.width + current.x;
        if (visited[index]) {
            result.status = MovementRouteStatus::cyclic_field;
            return result;
        }
        visited[index] = true;
        backwards.push_back(current);
        if (backwards.size() >= original_capacity || backwards.size() > 256U) {
            result.status = MovementRouteStatus::original_storage_limit;
            return result;
        }
        const auto x = std::int32_t(current.x), y = std::int32_t(current.y);
        const std::array<std::array<std::int32_t, 2>, 4> neighbors{{
            {{x, y - 1}}, {{x, y + 1}}, {{x - 1, y}}, {{x + 1, y}}
        }};
        auto maximum = std::int32_t(-1);
        MovementTile next{};
        for (const auto& neighbor : neighbors) {
            if (neighbor[0] < 0 || neighbor[1] < 0 || neighbor[0] >= range.width ||
                neighbor[1] >= range.height) continue;
            const auto value = range.at(neighbor[0], neighbor[1]);
            if (value == movement_reserved_cell || value <= maximum) continue;
            maximum = value;
            next = {std::uint8_t(neighbor[0]), std::uint8_t(neighbor[1])};
        }
        if (maximum < 0) {
            result.status = MovementRouteStatus::disconnected_field;
            return result;
        }
        current = next;
    }
    result.steps.assign(backwards.rbegin(), backwards.rend());
    result.status = MovementRouteStatus::ready;
    return result;
}

MovementAction::MovementAction(MovementUnitState state, const MovementProfile& profile,
                               const MovementRange& range, MovementRoute route,
                               std::int8_t form0_field2)
    : state_(state), route_(std::move(route)), form0_field2_(form0_field2) {
    if (!state.active || state.offset_x != 0 || state.offset_y != 0 || state.facing > 3 ||
        state.timer_ms < -1 || state.spent_budget != profile.spent_budget)
        throw std::invalid_argument("movement action requires an active, centered, consistent unit state");
    const bool same_tile = route_.status == MovementRouteStatus::already_at_destination &&
                           route_.steps.empty() && same(route_.origin, route_.destination);
    const bool travelling = route_.status == MovementRouteStatus::ready && !route_.steps.empty();
    if ((!travelling && !same_tile) ||
        !same(route_.origin, {state.tile_x, state.tile_y}))
        throw std::invalid_argument("movement action requires a ready or same-tile route from the unit position");
    const auto original = choose_movement_route(range, route_.origin, route_.destination);
    if (original.status != route_.status || original.steps.size() != route_.steps.size())
        throw std::invalid_argument("movement route does not match the original budget-field reconstruction");
    for (std::size_t i = 0; i < original.steps.size(); ++i)
        if (!same(original.steps[i], route_.steps[i]))
            throw std::invalid_argument("movement route does not preserve the original tie order");
    const auto budget = remaining_movement_budget(profile);
    const auto remaining = range.at(route_.destination.x, route_.destination.y);
    // The same-origin base case of 0x49e90 does not scan neighbors or exclude
    // the value 127. It can be a genuine remaining byte budget at the origin.
    if (remaining < 0 || remaining > budget ||
        (remaining == movement_reserved_cell && !same_tile))
        throw std::invalid_argument("destination budget is inconsistent with the selected unit");
    pending_cost_ = std::uint8_t(std::int32_t(budget) - remaining);
    if (form0_field2_ > 1) state_.flag_38 = 1;
}

MovementTickResult MovementAction::advance_tick(std::uint32_t delta_ms) {
    if (delta_ms > std::uint32_t(std::numeric_limits<std::int32_t>::max()))
        throw std::out_of_range("movement delta exceeds the original signed 32-bit range");
    if (!state_.active) return MovementTickResult::inactive;
    auto result = MovementTickResult::stationary;
    if (moving_) {
        // 0x49e90 writes origin into physical record0 even when its route count
        // remains zero. 0x43e2e copies that record plus the sentinel; 0x4351c
        // reaches it and completes on its first tick. Public steps still
        // describe travel only, so no fabricated travel step is added there.
        const auto target = route_.steps.empty() ? route_.origin : route_.steps.at(route_index_);
        const auto target_x = std::int32_t(target.x) * 32;
        const auto target_y = std::int32_t(target.y) * 32;
        const auto x = std::int32_t(state_.tile_x) * 32 + state_.offset_x;
        const auto y = std::int32_t(state_.tile_y) * 32 + state_.offset_y;
        result = MovementTickResult::travelling;
        if (x != target_x) {
            const auto speed = state_.form == 0 ? 3 : 6;
            const auto delta = std::clamp(target_x - x, -speed, speed);
            move_axis(state_.tile_x, state_.offset_x, delta);
            state_.facing = delta > 0 ? 3 : 2;
        } else if (y != target_y) {
            const auto speed = state_.form == 0 ? 3 : 9;
            const auto delta = std::clamp(target_y - y, -speed, speed);
            move_axis(state_.tile_y, state_.offset_y, delta);
            state_.facing = delta > 0 ? 1 : 0;
        }
        if (std::int32_t(state_.tile_x) * 32 + state_.offset_x == target_x &&
            std::int32_t(state_.tile_y) * 32 + state_.offset_y == target_y) {
            if (route_.steps.empty() || std::size_t(route_index_) + 1U == route_.steps.size()) {
                moving_ = false;
                state_.spent_budget = std::uint8_t(state_.spent_budget + pending_cost_);
                if (form0_field2_ > 1) state_.flag_38 = 1;
                result = MovementTickResult::completed;
            } else {
                ++route_index_;
                result = MovementTickResult::waypoint_reached;
            }
        }
    }
    if (state_.timer_ms != -1)
        state_.timer_ms = std::int32_t(std::max(std::int64_t(0),
                                             std::int64_t(state_.timer_ms) - delta_ms));
    return result;
}

void reset_movement_turn(MovementUnitState& state) noexcept {
    state.spent_budget = 0;
    state.flag_2c = 0;
    state.flag_2d = 0;
    state.flag_2f = 0;
    state.flag_38 = 0;
    state.flag_39 = 0;
    state.flag_3a = 0;
    state.anchor_x = state.tile_x;
    state.anchor_y = state.tile_y;
}

MovementUndoResult MovementAction::undo_ordinary() {
    if (moving_) return MovementUndoResult::still_moving;
    if (state_.flag_2d != 0 || state_.flag_3a != 0)
        return MovementUndoResult::unsupported_special_state;
    // 0x4324a: restore saved position/facing, clear +2d and movement mode.
    // Offsets are not touched by the original routine. Completed routes are centered.
    state_.tile_x = state_.anchor_x;
    state_.tile_y = state_.anchor_y;
    state_.facing = state_.anchor_facing;
    state_.flag_2d = 0;
    // 0x430fc: reset this turn's movement/action flags and save current position.
    reset_movement_turn(state_);
    return MovementUndoResult::restored;
}

} // namespace g1
