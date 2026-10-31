#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace g1 {

struct ArrivalUnit {
    std::uint8_t type{}; // Original map+90 has one unit object per type 0..22.
    std::uint8_t owner{};
    std::uint8_t x{};
    std::uint8_t y{};
    std::uint8_t facing{1};
    bool active{true};
};

struct MovementArrivalFacing {
    bool writes_facing{};
    std::uint8_t facing{};
    // nullopt means the original branch leaves the prior target cache intact.
    // An empty vector means it refreshes the cache and finds no targets.
    std::optional<std::vector<std::uint8_t>> target_types;
    std::optional<std::uint8_t> nearest_type;
};

// Exact first slice of 0x59798, ending at its natural 0x59884 boundary.
// min/max are unsigned selected-form stat bytes1/2; field13 is signed byte13.
// Original owner slots are populated in ascending type order (0x496e4).
// The caller may therefore provide world_units in command/storage order.
// This does not execute the remainder of arrival: transport, phase/menu reset,
// passive visual effects and later mission consequences are separate work.
MovementArrivalFacing choose_movement_arrival_facing(
    const ArrivalUnit& actor, const std::vector<ArrivalUnit>& world_units,
    std::uint8_t minimum_distance, std::uint8_t maximum_distance,
    std::int8_t selected_form_field13);

} // namespace g1
