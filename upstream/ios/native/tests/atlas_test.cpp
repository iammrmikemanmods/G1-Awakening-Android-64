#include "g1/Atlas.hpp"

#include <iostream>
#include <stdexcept>

namespace {
std::size_t checks{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void rejects(const g1::Bytes& bytes, const char* message) {
    try { const g1::AtlasDescriptor atlas(bytes); (void)atlas; }
    catch (const g1::FormatError&) { ++checks; return; }
    throw std::runtime_error(message);
}
void word(g1::Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value & 255U));
}
g1::Bytes fixture(std::uint8_t orientation = 0, bool wide = false) {
    // Synthetic contract fixture, not a copied original resource.
    g1::Bytes b{2, std::uint8_t(wide), 0, 1, 0x12, 0x34, 0x56, 0x78};
    for (const auto value : {10, 20, 3, 5}) word(b, std::uint16_t(value));
    word(b, 1); b.push_back(0); b.push_back(orientation); // Variant.
    word(b, 1); b.push_back(2); // One frame, two parts.
    for (int part = 0; part < 2; ++part) {
        b.push_back(0);
        if (wide) { word(b, std::uint16_t(part == 0 ? -300 : -7)); word(b, 9); }
        else { b.push_back(std::uint8_t(part == 0 ? -4 : -7)); b.push_back(9); }
    }
    b.push_back(1); // Explicit bounds (exercise signed origins / unsigned narrow sizes).
    if (wide) for (const auto value : {-300, -5, 400, 500}) word(b, std::uint16_t(value));
    else b.insert(b.end(), {std::uint8_t(-4), std::uint8_t(-5), 200, 201});
    b.push_back(1); // One extra point.
    if (wide) { word(b, std::uint16_t(-257)); word(b, 256); }
    else b.insert(b.end(), {std::uint8_t(-128), 127});
    b.insert(b.end(), {1, 1, 0, 0x12, 0x34}); // One animation / one frame / duration 0x1234.
    return b;
}
} // namespace

int main() {
    try {
        const auto bytes = fixture();
        const g1::AtlasDescriptor atlas(bytes);
        check(atlas.opaque_tag() == 0x12345678U, "opaque tag endianness changed");
        check(atlas.frames()[0].explicit_bounds == std::array<std::int16_t, 4>{-4, -5, 200, 201},
              "narrow bounds signedness changed");
        check(atlas.extra_points()[0] == std::array<std::int16_t, 2>{-128, 127}, "point signedness changed");
        check(atlas.animations()[0][0].duration_units == 0x1234U, "duration endianness changed");
        const auto quads = atlas.quads_for_frame(0);
        check(quads.size() == 2U && quads[0].source_part_index == 1U &&
              quads[1].source_part_index == 0U, "part drawing order changed");
        const std::array<std::array<std::int16_t, 3>, 4> expected_positions =
            {{{-7, -9, 0}, {-7, -14, 0}, {-4, -9, 0}, {-4, -14, 0}}};
        const std::array<std::array<std::int16_t, 2>, 4> expected_uv =
            {{{-31359, -30079}, {-31359, -29443}, {-30979, -30079}, {-30979, -29443}}};
        check(quads[0].positions == expected_positions, "normal positions differ from 0x728f0");
        check(quads[0].quantized_uv == expected_uv, "normal quantized UV differs from 0x728f0");
        const auto horizontal = g1::AtlasDescriptor(fixture(2)).quads_for_frame(0)[0];
        check(horizontal.quantized_uv[0] == expected_uv[2] && horizontal.quantized_uv[3] == expected_uv[1],
              "orientation 2 U reversal differs from 0x728f0");
        const auto vertical = g1::AtlasDescriptor(fixture(1)).quads_for_frame(0)[0];
        check(vertical.quantized_uv[0] == expected_uv[1] && vertical.quantized_uv[3] == expected_uv[2],
              "orientation 1 V reversal differs from 0x728f0");
        check(g1::AtlasDescriptor(fixture(3)).quads_for_frame(0)[0].quantized_uv == expected_uv,
              "orientation 3 incorrectly treated as combined flip bits");
        const std::array<std::array<std::int16_t, 3>, 4> rotated_positions =
            {{{-7, -12, 0}, {-2, -12, 0}, {-7, -9, 0}, {-2, -9, 0}}};
        for (std::uint8_t orientation = 4; orientation <= 7; ++orientation) {
            const auto quad = g1::AtlasDescriptor(fixture(orientation)).quads_for_frame(0)[0];
            check(quad.positions == rotated_positions && quad.quantized_uv == expected_uv,
                  "orientation 4..7 recovered branch changed");
        }
        auto bottom = fixture(); bottom[10] = 1; bottom[11] = 251; // y=507, height=5.
        const auto bottom_quad = g1::AtlasDescriptor(bottom).quads_for_frame(0)[0];
        check(bottom_quad.quantized_uv[0][1] == 32129 && bottom_quad.quantized_uv[1][1] == 32765,
              "bottom-edge Y adjustment differs from 0x736b4");
        const g1::AtlasDescriptor wide(fixture(0, true));
        check(wide.wide_coordinates() && wide.frames()[0].parts[0].offset_x == -300,
              "wide coordinate decoding changed");
        check(wide.frames()[0].explicit_bounds == std::array<std::int16_t, 4>{-300, -5, 400, 500} &&
              wide.extra_points()[0] == std::array<std::int16_t, 2>{-257, 256},
              "wide bounds or point decoding changed");
        // Empty observed descriptors retain a four-byte tag even though the
        // identified original loader's zero-rectangle path is unresolved.
        const g1::AtlasDescriptor empty({2, 0, 0, 0, 0x4a, 0x0d, 0x72, 0x42,
                                        0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 100});
        check(empty.rectangles().empty() && empty.quads_for_frame(0).empty() &&
              empty.animations()[0][0].duration_units == 100U, "empty descriptor framing changed");
        auto mutated = bytes;
        mutated[0] = 1; rejects(mutated, "unknown version accepted");
        mutated = bytes; mutated[1] = 2; rejects(mutated, "unknown coordinate flag accepted");
        mutated = bytes; mutated[18] = 1; rejects(mutated, "missing rectangle reference accepted");
        mutated = bytes; mutated[19] = 8; rejects(mutated, "unknown orientation accepted");
        mutated = bytes; mutated[23] = 1; rejects(mutated, "missing variant reference accepted");
        mutated = bytes; mutated[29] = 2; rejects(mutated, "unknown bounds flag accepted");
        mutated = bytes; mutated[mutated.size() - 3U] = 1; rejects(mutated, "missing animation frame accepted");
        mutated = bytes; mutated.push_back(0); rejects(mutated, "trailing data accepted");
        mutated = bytes; mutated[2] = 0xff; mutated[3] = 0xff; rejects(mutated, "oversized rectangle count accepted");
        for (std::size_t n = 0; n < bytes.size(); ++n)
            rejects(g1::Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n)),
                    "truncated atlas prefix accepted");
        rejects(g1::Bytes(g1::Archive::max_resource_size + 1U, 0), "resource size cap not enforced");
        try { (void)atlas.quads_for_frame(1); throw std::runtime_error("invalid frame query accepted"); }
        catch (const g1::FormatError&) { ++checks; }
        std::cout << "Passed " << checks << " atlas framing / geometry contract checks; no GPU rendering.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
