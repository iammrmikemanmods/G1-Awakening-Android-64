#pragma once

#include "g1/M3GScene.hpp"

namespace g1 {

struct M3GCameraProjection {
    std::size_t object{};
    std::uint8_t type{48}; // GENERIC48, PARALLEL49, PERSPECTIVE50.
    // Perspective: vertical degrees, aspect, near, far. Parallel: view-volume
    // height, aspect, near, far. No viewport/aspect replacement is implicit.
    std::array<float,4> parameters{};
    M3GMatrix generic{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
};

// Decodes actual Camera bodies after native scene/reference validation. Object
// numbers identify cameras; this does not guess a game camera from its name.
std::map<std::size_t,M3GCameraProjection> decode_m3g_cameras(const Bytes&);

// Original projection arithmetic and column-major layout. Clip-space depth
// is the authored M3G/OpenGL interval [-w,+w], before a host API conversion.
M3GMatrix m3g_camera_projection_matrix(const M3GCameraProjection&);

// Inverts a finite affine camera-to-world matrix. Singular or numerically
// ill-conditioned transforms fail instead of producing a fabricated view.
// Hierarchy animation/alignment must already have produced the supplied pose.
M3GMatrix m3g_camera_view_matrix(const M3GMatrix& camera_to_world);

// P * inverse(camera_to_world), followed only by Metal's z=(z+w)/2 mapping.
// No axis flip, view-volume resize, invented camera placement or game binding.
M3GMatrix m3g_camera_metal_view_projection(const M3GCameraProjection&,
                                         const M3GMatrix& camera_to_world);

} // namespace g1
