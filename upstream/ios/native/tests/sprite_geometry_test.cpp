#include "g1/SpriteGeometry.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
std::size_t checks{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
std::uint32_t bits(float value) {
    std::uint32_t result{};
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
} // namespace

int main() {
    try {
        // Scale/bias bit patterns captured from unchanged original 0x71884
        // for all ten observed nonempty sprite capacities. No emulator is
        // required by this test or by the native library.
        check(bits(g1::sprite_position_scale) == 0x3ba3d70aU, "original position scale changed");
        check(bits(g1::sprite_uv_scale) == 0x37800000U, "original UV scale changed");
        check(bits(g1::sprite_uv_bias) == 0x3f000000U, "original UV bias changed");
        const g1::AtlasQuad raw{7,
            {{{-100, 200, 0}, {-100, -200, 0}, {100, 200, 0}, {100, -200, 0}}},
            {{{-32768, -32768}, {-32768, 32767}, {32767, -32768}, {32767, 32767}}}};
        const auto quad = g1::make_sprite_quad(raw);
        check(quad.source_part_index == 7U, "part identity changed");
        check(quad.vertices[0].position == std::array<float, 3>{-0.5f, 1.0f, 0.0f} &&
              quad.vertices[3].position == std::array<float, 3>{0.5f, -1.0f, 0.0f},
              "local model position conversion changed");
        check(quad.vertices[0].uv == std::array<float, 2>{0.0f, 0.0f}, "minimum UV endpoint changed");
        check(bits(quad.vertices[3].uv[0]) == 0x3f7fff00U &&
              bits(quad.vertices[3].uv[1]) == 0x3f7fff00U, "maximum UV endpoint changed");
        // Exact dyadic mapping is representable for every signed 16-bit input.
        // These independent endpoint/zero/border values catch signedness,
        // 65535-vs-65536 normalization, bias and extra vertical-flip mistakes.
        auto borders = raw;
        borders.quantized_uv = {{{-32767, -32640}, {-16384, -1}, {0, 1}, {16384, 32640}}};
        const auto uv = g1::make_sprite_quad(borders);
        check(uv.vertices[0].uv == std::array<float, 2>{1.0f/65536.0f, 1.0f/512.0f}, "UV origin inset changed");
        check(uv.vertices[1].uv == std::array<float, 2>{0.25f, 0.5f-1.0f/65536.0f}, "negative UV mapping changed");
        check(uv.vertices[2].uv == std::array<float, 2>{0.5f, 0.5f+1.0f/65536.0f}, "UV midpoint changed");
        check(uv.vertices[3].uv == std::array<float, 2>{0.75f, 511.0f/512.0f}, "UV positive border changed");
        check(g1::sprite_triangle_indices == std::array<std::uint16_t, 6>{0, 1, 2, 2, 1, 3},
              "original four-vertex strip triangulation changed");
        for (std::size_t i = 0; i < 6U; i += 3U) {
            const auto& a = quad.vertices[g1::sprite_triangle_indices[i]].position;
            const auto& b = quad.vertices[g1::sprite_triangle_indices[i+1U]].position;
            const auto& c = quad.vertices[g1::sprite_triangle_indices[i+2U]].position;
            check((b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0]) > 0.0f,
                  "quad triangle winding changed");
        }
        const auto first = g1::sprite_resource_for_type(11);
        const auto second = g1::sprite_resource_for_type(19);
        check(first && first->descriptor_id == 868U && first->image_id == 961U &&
              first->resource_key == 0xf4e02223U, "original type 11 sprite lookup changed");
        check(second && second->descriptor_id == 517U, "original type 19 sprite lookup changed");
        check(g1::sprite_resource_for_type(64)->descriptor_id == 607U &&
              g1::sprite_resource_for_type(78)->descriptor_id == 982U,
              "original high-type table offset changed");
        for (const auto invalid : {38U, 63U, 79U, 255U})
            check(!g1::sprite_resource_for_type(std::uint8_t(invalid)), "unsupported sprite type accepted");
        std::cout << "Passed " << checks << " sprite geometry checks; no GPU/device execution.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
