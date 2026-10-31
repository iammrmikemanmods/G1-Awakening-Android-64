#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace g1 {

// Recovered from G1 Awakening iOS 1.1.3. These are decoded gameplay inputs,
// not an ARM object layout. The original resource-to-world loader is separate.
struct MovementCell {
    std::uint8_t terrain_cost{};       // Terrain definition byte 0, not a tile id.
    std::uint8_t terrain_field2{};     // Terrain definition byte 2; name unresolved.
    std::int16_t active_owner{-1};     // -1 if original unit lookup finds no active unit.
};

struct MovementPoint {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t state{};             // Original point record field 3: nonzero blocks.
};

struct MovementCostRegion {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};
};

struct MovementBoard {
    std::uint16_t width{};             // Original dimensions are bytes, range 1..255.
    std::uint16_t height{};
    std::vector<MovementCell> cells;    // Row-major: y * width + x.
    std::vector<MovementPoint> points;
    std::vector<MovementCostRegion> cost99_regions;
};

struct MovementProfile {
    std::uint8_t unit_type{};          // Type 11 has an original explicit cost rule.
    std::uint8_t owner{};
    std::uint8_t base_budget{};        // Selected form's stat byte 0.
    std::uint8_t spent_budget{};       // Unit object byte +0x59.
    std::int8_t traversal_field6{};    // Selected form's signed stat byte 6; >0 is special.
};

struct MovementRange {
    std::uint16_t width{};
    std::uint16_t height{};
    std::vector<std::int32_t> remaining; // -1 is unvisited; nonnegative is stored budget.
    bool has_unoccupied_destination{};

    std::int32_t at(std::int32_t x, std::int32_t y) const;
};

constexpr std::int32_t movement_unvisited = -1;
constexpr std::int32_t movement_cost99 = 99;
constexpr std::int32_t movement_blocked = 10000;
constexpr std::int32_t movement_reserved_cell = 127;

// Preserves the original eight-bit subtraction, including wraparound.
std::uint8_t remaining_movement_budget(const MovementProfile&) noexcept;

// Reconstructs 0x4a16e. Outside the board returns 10000. Malformed decoded
// input throws std::invalid_argument; no rules are invented for malformed state.
std::int32_t movement_entry_cost(const MovementBoard&, const MovementProfile&,
                                 std::int32_t x, std::int32_t y);

// Reconstructs the full-fill mode used by 0x50844 -> 0x4a29c, then the vacant
// destination predicate at 0x49b5a. Uses a native explicit stack, not guest code.
// This computes a budget field; it does not choose a route or execute a move.
// Origin must be in bounds. Occupancy must already reflect original lookup
// rules (active byte +0xd and unit fields +0x40/+0x44), not every visible sprite.
MovementRange compute_movement_range(const MovementBoard&, const MovementProfile&,
                                      std::int32_t origin_x, std::int32_t origin_y);

// Same original 0x4a29c full-fill mode with its explicit signed register budget.
// AI callers use a signed form budget or a larger board-search budget. The
// selected form's original base remains in profile for the one-expensive-step
// rule; it is not replaced by initial_budget. Negative budgets visit no cells.
MovementRange compute_movement_range_with_budget(const MovementBoard&,
    const MovementProfile&, std::int32_t origin_x, std::int32_t origin_y,
    std::int32_t initial_budget);

} // namespace g1
