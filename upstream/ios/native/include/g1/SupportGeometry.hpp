#pragma once

#include "g1/OverlayDescriptor.hpp"
#include "g1/Scene.hpp"

namespace g1 {

// Original 0x5ca94 support point, including its +0.1 x and next-tile z.
// The active support call supplies zero pixel offsets.
std::array<float,3> support_world_anchor(std::uint8_t tile_x,std::uint8_t tile_y) noexcept;

// Native widescreen policy: preserve a 320-unit logical height and uniform
// sprite scale; expand horizontal view. Project through the host's tactical
// matrix, truncate the logical screen anchor as the original draw endpoint
// does, then emit actual drawable clip coordinates. This is not evidence of
// original tactical camera/easing or hardware filtering parity.
std::vector<SceneVertex> support_overlay_vertices(
    const OverlayDescriptor&,std::size_t frame_index,
    std::uint32_t image_width,std::uint32_t image_height,
    const std::array<float,16>& view_projection,
    std::uint8_t tile_x,std::uint8_t tile_y,
    float drawable_width,float drawable_height);

} // namespace g1
