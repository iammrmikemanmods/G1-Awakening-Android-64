#include "g1/Movement.hpp"

#include <limits>
#include <stdexcept>

namespace g1 {
namespace {

bool inside(const MovementBoard& board, std::int32_t x, std::int32_t y) noexcept {
    return x >= 0 && y >= 0 && x < board.width && y < board.height;
}

std::size_t index_of(const MovementBoard& board, std::int32_t x, std::int32_t y) noexcept {
    return static_cast<std::size_t>(y) * board.width + static_cast<std::size_t>(x);
}

void validate(const MovementBoard& board) {
    if (board.width == 0 || board.height == 0 || board.width > 255 || board.height > 255)
        throw std::invalid_argument("Movement board dimensions must be 1..255");
    if (board.cells.size() != static_cast<std::size_t>(board.width) * board.height)
        throw std::invalid_argument("Movement cell count does not match dimensions");
    for (const auto& cell : board.cells) {
        if (cell.active_owner < -1 || cell.active_owner > 255)
            throw std::invalid_argument("Active owner must be absent or an original byte value");
    }
    for (const auto& region : board.cost99_regions) {
        const auto right = static_cast<std::int64_t>(region.x) + region.width;
        const auto bottom = static_cast<std::int64_t>(region.y) + region.height;
        if (right < std::numeric_limits<std::int32_t>::min() ||
            right > std::numeric_limits<std::int32_t>::max() ||
            bottom < std::numeric_limits<std::int32_t>::min() ||
            bottom > std::numeric_limits<std::int32_t>::max())
            throw std::invalid_argument("Movement region arithmetic exceeds original signed range");
    }
}

bool in_region(const MovementCostRegion& region, std::int32_t x, std::int32_t y) noexcept {
    // Original 0x4979c uses inclusive left/top and exclusive right/bottom.
    return x >= region.x && y >= region.y &&
           static_cast<std::int64_t>(x) < static_cast<std::int64_t>(region.x) + region.width &&
           static_cast<std::int64_t>(y) < static_cast<std::int64_t>(region.y) + region.height;
}

std::int32_t entry_cost(const MovementBoard& board, const MovementProfile& profile,
                        std::int32_t x, std::int32_t y) {
    // 0x4a17e..0x4a194: bounds are checked before destination lookups.
    if (!inside(board, x, y)) return movement_blocked;
    const auto& cell = board.cells[index_of(board, x, y)];
    const bool special = profile.traversal_field6 > 0;

    // 0x4a1c8..0x4a1ee: special traversal skips the enemy-owner rejection.
    if (!special && cell.active_owner >= 0 && cell.active_owner != profile.owner)
        return movement_blocked;

    // 0x4a1dc..0x4a22c: a point blocks only when its fourth integer is nonzero.
    for (const auto& point : board.points)
        if (point.state != 0 && point.x == x && point.y == y) return movement_blocked;

    std::int32_t cost = cell.terrain_cost;
    for (const auto& region : board.cost99_regions)
        if (in_region(region, x, y)) { cost = movement_cost99; break; }

    // Order matters: this rule can override a cost-99 region, but never a point
    // or the bounds/owner rejection above. Byte 2's semantic name is unresolved.
    if (special && cell.terrain_field2 == 0) return 1;
    if (profile.unit_type == 11 && cost != movement_cost99) return 1;
    return cost;
}

struct PendingCell {
    std::int32_t x;
    std::int32_t y;
    std::int32_t budget;
    int reverse_direction;
};

} // namespace

std::uint8_t remaining_movement_budget(const MovementProfile& profile) noexcept {
    return static_cast<std::uint8_t>(static_cast<unsigned>(profile.base_budget) -
                                     static_cast<unsigned>(profile.spent_budget));
}

std::int32_t MovementRange::at(std::int32_t x, std::int32_t y) const {
    if (x < 0 || y < 0 || x >= width || y >= height)
        throw std::out_of_range("Movement range coordinate is outside the board");
    return remaining.at(static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x));
}

std::int32_t movement_entry_cost(const MovementBoard& board, const MovementProfile& profile,
                                 std::int32_t x, std::int32_t y) {
    validate(board);
    return entry_cost(board, profile, x, y);
}

MovementRange compute_movement_range(const MovementBoard& board, const MovementProfile& profile,
                                      std::int32_t origin_x, std::int32_t origin_y) {
    return compute_movement_range_with_budget(board, profile, origin_x, origin_y,
                                              remaining_movement_budget(profile));
}

MovementRange compute_movement_range_with_budget(const MovementBoard& board,
    const MovementProfile& profile, std::int32_t origin_x, std::int32_t origin_y,
    std::int32_t initial_budget) {
    validate(board);
    if (!inside(board, origin_x, origin_y))
        throw std::invalid_argument("Movement origin is outside the board");

    MovementRange result{board.width, board.height,
                         std::vector<std::int32_t>(board.cells.size(), movement_unvisited), false};
    std::vector<PendingCell> pending;
    pending.reserve(board.cells.size());
    pending.push_back({origin_x, origin_y, initial_budget, -1});

    // Original order is up, down, left, right. Reverse insertion into the LIFO
    // work list preserves that depth-first order without native stack recursion.
    constexpr int dx[4] = {0, 0, -1, 1};
    constexpr int dy[4] = {-1, 1, 0, 0};
    constexpr int opposite[4] = {1, 0, 3, 2};
    const int signed_base = profile.base_budget < 128 ? profile.base_budget
                                                    : static_cast<int>(profile.base_budget) - 256;
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        const auto index = index_of(board, current.x, current.y);
        // 0x4a2e2..0x4a320: only a strictly greater remaining budget improves
        // the field. The raw terrain-99 gate differs from region cost overrides.
        if (result.remaining[index] >= current.budget) continue;
        if (profile.traversal_field6 <= 0 && board.cells[index].terrain_cost == movement_cost99)
            continue;
        result.remaining[index] = current.budget;

        for (int direction = 3; direction >= 0; --direction) {
            if (direction == current.reverse_direction) continue;
            const auto x = current.x + dx[direction];
            const auto y = current.y + dy[direction];
            if (!inside(board, x, y)) continue;
            const auto cost = entry_cost(board, profile, x, y);
            auto budget = current.budget - cost;
            // 0x4a3ac..0x4a3ca and three matching directional branches:
            // one expensive step is allowed only at the unchanged signed base
            // budget, with nonzero base and neither special blocking sentinel.
            if (budget < 0 && cost != movement_cost99 && cost != movement_blocked &&
                signed_base != 0 && current.budget == signed_base)
                budget = 0;
            if (budget >= 0) pending.push_back({x, y, budget, opposite[direction]});
        }
    }

    // 0x49b5a..0x49bfe: the original returns false without scanning when all
    // four clamped adjacent positions have negative budgets. Preserve this
    // gate even for an unoccupied origin: an interior zero-budget origin alone
    // does not count as a vacant destination. The original ARM oracle catches
    // that distinction from a simple whole-field existence test.
    const auto left = origin_x > 0 ? origin_x - 1 : 0;
    const auto up = origin_y > 0 ? origin_y - 1 : 0;
    const auto right = origin_x + 1 < board.width ? origin_x + 1 : board.width - 1;
    const auto down = origin_y + 1 < board.height ? origin_y + 1 : board.height - 1;
    if (result.at(origin_x, up) < 0 && result.at(left, origin_y) < 0 &&
        result.at(right, origin_y) < 0 && result.at(origin_x, down) < 0)
        return result;

    // Adjacent candidates are included in the subsequent full-field scan, so
    // combining those candidate checks is equivalent after the gate above.
    // Value 127 is explicitly excluded, although it can be an ordinary budget.
    for (std::size_t i = 0; i < result.remaining.size(); ++i) {
        if (result.remaining[i] >= 0 && result.remaining[i] != movement_reserved_cell &&
            board.cells[i].active_owner < 0) {
            result.has_unoccupied_destination = true;
            break;
        }
    }
    return result;
}

} // namespace g1
