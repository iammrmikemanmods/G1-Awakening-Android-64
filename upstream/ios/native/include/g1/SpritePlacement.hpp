#pragma once

#include <array>
#include <cstdint>

namespace g1 {

struct UnitSpritePlacementInput {
    std::uint8_t tile_x{};
    std::uint8_t tile_y{};
    std::uint8_t form{};
    std::int32_t offset_x{}; // Original unit+0x40, in engine coordinate units.
    std::int32_t offset_y{}; // Original unit+0x44; zero at spawn.
    bool on_site{};         // Original 0x4b3aa predicate.
    bool in_site_group{};   // Original 0x4b06e predicate; overrides on_site.
};

// Exact arguments and transform components from 0x6cf00..0x6cf66 and 0x727a4.
// Original call order is identity, uniform scale, translation, rotation about X,
// then assignment to the sprite mesh. These are model units, matching terrain
// positions divided by 100 and SpriteGeometry's 0.005 local vertex scale.
struct SpritePlacement {
    std::array<std::int32_t, 3> original_position;
    float scale;
    std::array<float, 3> translation;
    float angle_degrees;
    std::array<float, 3> axis;
    // Column-major, for column vectors: S(0.8) * T(translation) * Rx(-45).
    // Apply directly to SpriteGeometry vertices, with no additional world scale.
    std::array<float, 16> model;
};

SpritePlacement make_unit_sprite_placement(const UnitSpritePlacementInput& input);

} // namespace g1
