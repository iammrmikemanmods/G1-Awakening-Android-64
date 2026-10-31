#pragma once

#include "g1/M3GScenePose.hpp"
#include "g1/M3GSprite3D.hpp"

namespace g1 {

struct M3GMeshQueueInput {
    std::uint8_t blending{68};
    float effective_alpha{1};
    std::optional<float> material_diffuse_alpha;
    // Zero means the array is absent. Present color arrays have 3 or 4 components.
    std::uint8_t vertex_color_components{};
    float default_color_alpha{1};
    std::array<std::optional<std::uint8_t>,2> texture_image_formats;
};

// Original b5e2a..b5efe classification. This tests stored alpha-channel
// metadata; it does not scan pixels or infer opacity from the texture function.
bool m3g_mesh_queue_transparent(const M3GMeshQueueInput&);

// Explicit canonical keys for the recovered same-file/shared-Appearance path.
// The original loader constructs Appearances in record order; Mesh/Group clone
// shares those objects. Queue comparisons use their order/equality only within
// one node. These ranks are not claimed to be absolute runtime serial values.
// They require the documented no-signed-counter-transition policy and do not
// stand in for separately/directly cloned or cross-file Appearance objects.
std::map<std::size_t,std::int32_t> m3g_mesh_appearance_order_keys(const M3GScene&);

struct M3GRenderInstance {
    const M3GRenderAsset* asset{};
    const std::map<std::size_t,M3GEvaluatedNode>* nodes{};
    // Unique namespace per attached instance; nodes retain their object IDs.
    std::uint32_t identity{};
    M3GMatrix parent_transform{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    float parent_alpha{1};
    bool parent_enabled{true};
    // Required explicit original grouping metadata for opaque draws. Meshes
    // use Appearance creation serials; opaque Sprite3D uses Appearance identity.
    // A serialized object number is not silently substituted for either key.
    const std::map<std::size_t,std::int32_t>* mesh_appearance_serials{};
    const std::map<std::size_t,std::int32_t>* sprite_appearance_identities{};
};

struct M3GRenderView {
    M3GCameraProjection projection;
    M3GMatrix camera_to_world{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    std::array<std::uint32_t,2> viewport_pixels{};
    std::uint32_t scope{0xffffffffU};
};

struct M3GPreparedDraw {
    std::size_t instance{},geometry_index{},node_object{};
    M3GDrawableKind kind{M3GDrawableKind::mesh};
    M3GMatrix model_to_world;
    M3GMatrix metal_matrix;
    float effective_alpha{1};
    bool transparent{};
    // Optional Modern procedural assembly coverage. Zero role preserves the
    // exact original battle/UI rendering path. 1=robot, 2=vehicle.
    std::uint8_t assembly_role{};
    float assembly_mix{},assembly_highlight{};
    std::int32_t queue_key{},grouping_key{};
    M3GSprite3DProjection sprite_projection;
    // Sprite3D vertices are already in Metal clip space, with the recovered
    // NDC origin + fixed16 offsets and z=(z+w)/2. Mesh geometry stays immutable.
    std::array<M3GVertex,6> sprite_vertices{};
    bool draws_geometry() const noexcept {
        return kind==M3GDrawableKind::mesh || sprite_projection.drawn();
    }
};

struct M3GPreparedFrame {
    M3GMatrix camera_view,projection_gl,metal_view_projection;
    // Identity of each immutable source asset supplied by the caller. A GPU
    // host can reject a plan accidentally paired with different geometry.
    std::vector<const M3GRenderAsset*> assets;
    // One original queue for all input instances, mesh submeshes and sprites.
    // A queued but culled Sprite3D remains identifiable through its result.
    std::vector<M3GPreparedDraw> draws;
};

// Instances are supplied in the actual attachment/traversal order. Camera,
// form, attachment transforms and opaque grouping identities are caller data.
// The whole CPU frame is validated before a GPU encoder receives any draw.
M3GPreparedFrame prepare_m3g_frame(const std::vector<M3GRenderInstance>&,
                                 const M3GRenderView&);

} // namespace g1
