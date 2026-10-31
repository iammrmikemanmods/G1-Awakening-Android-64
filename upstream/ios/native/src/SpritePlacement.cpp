#include "g1/SpritePlacement.hpp"

#include <limits>
#include <stdexcept>

namespace g1 {
namespace {
std::int32_t checked_coordinate(std::int64_t value) {
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max())
        throw std::out_of_range("unit sprite coordinate exceeds original signed range");
    return std::int32_t(value);
}
} // namespace

SpritePlacement make_unit_sprite_placement(const UnitSpritePlacementInput& input) {
    const auto x = checked_coordinate(std::int64_t(input.tile_x)*32 + input.offset_x);
    const auto row = checked_coordinate(std::int64_t(input.tile_y)*32 + input.offset_y + 32);
    // Exact original constant bits 0x412aaaab for form 0, 0x41800000 otherwise.
    const auto form_offset = input.form == 0U ? 0x1.555556p+3f : 16.0f;
    const auto z_float = (float(row) - form_offset) + 10.0f;
    if (double(z_float) < double(std::numeric_limits<std::int32_t>::min()) ||
        double(z_float) > double(std::numeric_limits<std::int32_t>::max()))
        throw std::out_of_range("unit sprite Z conversion exceeds original signed range");
    const auto base_height = input.in_site_group ? 5 : (input.on_site ? 20 : 0);
    SpritePlacement result{{x, base_height-6, std::int32_t(z_float)}, 0.8f, {}, -45.0f,
                           {1.0f, 0.0f, 0.0f}, {}};
    // Original 0x7431c receives (angle, axisX=1, axisY=0, axisZ=0).
    // This is rotation about X; the axis order is preserved explicitly.
    const auto adjustment = ((1.0f - result.scale)*0.5f)*32.0f;
    for (std::size_t i = 0; i < 3U; ++i)
        result.translation[i] = (float(result.original_position[i])/result.scale + adjustment)/100.0f;
    // Fixed unit orientation, not a general axis-angle API. The original M3G
    // wrapper uses float degrees-to-radians, a quaternion, then postmultiplies.
    // These fixed basis coefficients preserve that rounding (cosine and sine
    // differ by one ULP). Original-core oracle uses host sinf/cosf at the external
    // libm boundary; it does not establish bit-identical old-iOS libm behavior.
    constexpr auto tilted_cosine = 0x1.21a184p-1f;
    constexpr auto tilted_sine = 0x1.21a186p-1f;
    result.model = {result.scale, 0.0f, 0.0f, 0.0f,
                    0.0f, tilted_cosine, -tilted_sine, 0.0f,
                    0.0f, tilted_sine, tilted_cosine, 0.0f,
                    result.scale*result.translation[0],
                    result.scale*result.translation[1],
                    result.scale*result.translation[2], 1.0f};
    return result;
}

} // namespace g1
