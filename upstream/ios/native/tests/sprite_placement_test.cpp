#include "g1/SpritePlacement.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
std::uint32_t bits(float value) {
    std::uint32_t result{};
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
template<class F> void rejects(F function) {
    ++checks;
    try { function(); }
    catch (const std::out_of_range&) { return; }
    throw std::runtime_error("invalid coordinate was accepted");
}
} // namespace

int main() {
    try {
        const auto first = g1::make_unit_sprite_placement({1, 2, 0, 0, 0, false, false});
        check(first.original_position == std::array<std::int32_t, 3>{32, -6, 95}, "first spawn anchor");
        const std::array<std::uint32_t, 16> expected{
            0x3f4ccccd, 0, 0, 0, 0, 0x3f10d0c2, 0xbf10d0c3, 0,
            0, 0x3f10d0c3, 0x3f10d0c2, 0, 0x3eb0f27c, 0xbd0ce704, 0x3f79c0eb, 0x3f800000};
        for (std::size_t i = 0; i < expected.size(); ++i)
            check(bits(first.model[i]) == expected[i], "original first-unit model matrix");
        check(first.angle_degrees == -45.0f && first.axis == std::array<float, 3>{1.0f, 0.0f, 0.0f},
              "rotation must tilt around X");
        const auto second = g1::make_unit_sprite_placement({4, 7, 0, 0, 0, false, false});
        check(second.original_position == std::array<std::int32_t, 3>{128, -6, 255}, "second spawn anchor");
        check(bits(second.model[12]) == 1067916775U && bits(second.model[14]) == 1076156066U,
              "second model translation");
        const auto other_form = g1::make_unit_sprite_placement({1, 2, 1, 0, 0, false, false});
        check(other_form.original_position[2] == 90, "form offset");
        check(g1::make_unit_sprite_placement({1, 2, 7, 0, 0, false, false}).original_position ==
              other_form.original_position, "all nonzero forms follow original alternate branch");
        check(g1::make_unit_sprite_placement({1, 2, 0, -12, 3, false, false}).original_position ==
              std::array<std::int32_t, 3>{20, -6, 98}, "live motion offsets");
        check(g1::make_unit_sprite_placement({1, 2, 0, 0, 0, true, false}).original_position[1] == 14,
              "site height");
        check(g1::make_unit_sprite_placement({1, 2, 0, 0, 0, true, true}).original_position[1] == -1,
              "site-group override");
        check(g1::make_unit_sprite_placement({0, 0, 0, 0, -40, false, false}).original_position[2] == -8,
              "signed float conversion truncates toward zero");
        rejects([] { g1::make_unit_sprite_placement({255, 0, 0, std::numeric_limits<std::int32_t>::max(), 0, false, false}); });
        rejects([] { g1::make_unit_sprite_placement({0, 255, 0, 0, std::numeric_limits<std::int32_t>::max(), false, false}); });
        std::cout << checks << " sprite placement checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
