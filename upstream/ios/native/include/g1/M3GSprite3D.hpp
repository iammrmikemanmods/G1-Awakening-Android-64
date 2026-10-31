#pragma once

#include "g1/M3GCamera.hpp"

namespace g1 {

// Explicit evaluated inputs. Matrices use column-major storage and column
// vectors. Projection remains in the original OpenGL [-w,+w] depth space.
// Scene decoding, camera selection, inherited enable/alpha and scope admission
// remain the caller's responsibilities; this function does not mutate them.
struct M3GSprite3DInput {
    M3GMatrix model_to_world{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    M3GMatrix camera_view{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    M3GMatrix projection_gl{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    std::uint8_t projection_type{48}; // GENERIC48, PARALLEL49, PERSPECTIVE50.
    std::array<std::uint32_t,2> viewport_pixels{};
    std::array<std::uint32_t,2> image_pixels{};
    std::array<float,4> crop_pixels{}; // x, y, signed width, signed height.
    bool scaled{true};
    bool appearance_present{true};
};

enum class M3GSprite3DResult : std::uint8_t {
    drawn,
    no_appearance,
    zero_crop,
    outside_image,
    projection_at_infinity,
    outside_depth,
    outside_viewport
};

struct M3GSprite3DProjection {
    M3GSprite3DResult result{M3GSprite3DResult::outside_viewport};
    std::array<float,3> center_ndc_gl{};
    // The original pre-clip rectangle and source crop bounds are retained for
    // exact numeric verification. Bounds order is left,top,right,bottom.
    std::array<std::int32_t,4> rectangle_fixed16{};
    std::array<std::int32_t,4> crop_bounds_fixed16{};
    bool mirror_x{},mirror_y{};
    // TRIANGLE_STRIP order: top-left,bottom-left,top-right,bottom-right.
    // Final GL clip vertex = (center.x+offset.x/65536,
    // center.y+offset.y/65536,center.z,1). All corners share origin depth.
    std::array<std::int32_t,8> offsets_fixed16{};
    std::array<std::int32_t,8> uv_fixed16{};
    bool drawn() const noexcept { return result==M3GSprite3DResult::drawn; }
};

// Native reimplementation of the bounded original single-image-block path.
// Supports finite affine model/view transforms and power-of-two images of
// 1..1024 pixels per axis. Original non-power-of-two images use padded blocks;
// that texture-padding/upload path is explicitly outside this helper's scope.
// Crop magnitudes are bounded by the original 1024-pixel setter limit.
// Invalid/unsupported numerical inputs throw FormatError. Valid culled crops
// return an explicit result; no replacement geometry is generated.
M3GSprite3DProjection project_m3g_sprite3d(const M3GSprite3DInput&);

// Convenience for an already evaluated camera-to-world pose. Uses the existing
// checked camera inverse; no camera, aspect or viewport substitution is made.
inline M3GSprite3DProjection project_m3g_sprite3d_from_camera(
    M3GSprite3DInput input,const M3GMatrix& camera_to_world) {
    input.camera_view=m3g_camera_view_matrix(camera_to_world);
    return project_m3g_sprite3d(input);
}

// Proven shared mesh/Sprite3D queue arithmetic. The caller supplies original
// opaque/transparent classification. This is a distance-from-camera key, not
// view-space Z. Layer is -63..63. Invalid numeric input fails explicitly.
std::int32_t m3g_draw_queue_key(std::int32_t layer,bool transparent,
                               const std::array<float,3>& world_origin,
                               const std::array<float,3>& camera_world_origin);

struct M3GDrawQueueEntry {
    std::uint64_t node_identity{};
    std::int32_t layer{};
    bool transparent{};
    std::array<float,3> world_origin{};
    // Original mesh opaque entries use Appearance creation serial; transparent
    // entries use zero. This explicit key is not an image/texture identifier.
    std::int32_t grouping_key{};
};

// Returns input indices in the original linked-queue order, including the
// same-node/equal-key insertion anchor. No mesh-versus-sprite partition exists.
std::vector<std::size_t> m3g_draw_queue_order(
    const std::vector<M3GDrawQueueEntry>&,
    const std::array<float,3>& camera_world_origin);

} // namespace g1
