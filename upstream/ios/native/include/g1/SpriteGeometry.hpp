#pragma once

#include "g1/Atlas.hpp"

namespace g1 {

// Interleaved 20-byte vertex. Metal should use a vertex descriptor with offsets
// 0 and 12 / stride 20, or packed_float3 + packed_float2 in a storage buffer.
struct SpriteVertex {
    std::array<float, 3> position;
    std::array<float, 2> uv;
};

struct SpriteQuad {
    std::size_t source_part_index;
    std::array<SpriteVertex, 4> vertices;
};

// The original builds one four-vertex triangle strip per part (0x715d0).
// This equivalent triangle list preserves counterclockwise local XY winding.
inline constexpr std::array<std::uint16_t, 6> sprite_triangle_indices{0, 1, 2, 2, 1, 3};

// Verified original sprite setup: 0x73dba calls 0x71884(false, 2.0f).
// Position scale = 0.01f / 2.0f; UV = quantized * (1 / 65536) + 0.5f.
// Positions are original sprite model units, not screen pixels or world cells.
inline constexpr float sprite_position_scale = 0.005f;
inline constexpr float sprite_uv_scale = 0x1p-16f;
inline constexpr float sprite_uv_bias = 0.5f;

SpriteQuad make_sprite_quad(const AtlasQuad& source) noexcept;
std::vector<SpriteQuad> make_sprite_geometry(const AtlasDescriptor& atlas, std::size_t frame_index);

struct SpriteResource {
    std::uint32_t descriptor_id;
    std::uint32_t image_id;
    std::uint32_t resource_key;
};

// Recovered original factory 0x66d58 and its 38-entry table at 0xcf5b0.
// Types 0..37 map directly; types 64..78 map to indices type-41. Other
// inputs are rejected because they fall outside the recovered sprite table.
// Form/animation selection is a separate state-machine decision.
std::optional<SpriteResource> sprite_resource_for_type(std::uint8_t type) noexcept;

} // namespace g1
