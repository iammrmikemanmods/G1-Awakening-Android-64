#include "g1/SpriteGeometry.hpp"

#include <cstddef>
#include <type_traits>

namespace g1 {

static_assert(std::is_standard_layout<SpriteVertex>::value, "SpriteVertex must support a GPU vertex descriptor");
static_assert(sizeof(float) == 4U, "Sprite geometry requires 32-bit float");
static_assert(sizeof(SpriteVertex) == 20U && offsetof(SpriteVertex, uv) == 12U,
              "SpriteVertex interleaving changed; update the native renderer descriptor");

SpriteQuad make_sprite_quad(const AtlasQuad& source) noexcept {
    SpriteQuad result{};
    result.source_part_index = source.source_part_index;
    for (std::size_t i = 0; i < result.vertices.size(); ++i) {
        for (std::size_t axis = 0; axis < 3U; ++axis)
            result.vertices[i].position[axis] = float(source.positions[i][axis]) * sprite_position_scale;
        for (std::size_t axis = 0; axis < 2U; ++axis)
            result.vertices[i].uv[axis] = float(source.quantized_uv[i][axis]) * sprite_uv_scale + sprite_uv_bias;
    }
    return result;
}

std::vector<SpriteQuad> make_sprite_geometry(const AtlasDescriptor& atlas, std::size_t frame_index) {
    const auto raw = atlas.quads_for_frame(frame_index);
    std::vector<SpriteQuad> result;
    result.reserve(raw.size());
    for (const auto& quad : raw) result.push_back(make_sprite_quad(quad));
    return result;
}

std::optional<SpriteResource> sprite_resource_for_type(std::uint8_t type) noexcept {
    constexpr std::array<std::uint32_t, 38> descriptors{
        1298, 1111, 1119, 1020, 1106, 1041, 426, 522, 1281, 1088,
        1155, 868, 876, 377, 985, 1036, 1196, 1032, 815, 517,
        1027, 1183, 1069, 607, 769, 708, 933, 809, 641, 1037,
        539, 781, 638, 825, 581, 1095, 1132, 982};
    const auto index = std::size_t(type <= 63U ? type : type - 41U);
    if (index >= descriptors.size()) return std::nullopt;
    return SpriteResource{descriptors[index], 961U, 0xf4e02223U};
}

} // namespace g1
