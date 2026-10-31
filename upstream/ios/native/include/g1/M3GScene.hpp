#pragma once

#include "g1/Image.hpp"
#include "g1/M3G.hpp"

#include <map>
#include <memory>
#include <optional>

namespace g1 {

using M3GMatrix = std::array<float,16>; // Column-major, column vectors.

struct M3GTransform {
    std::array<float,3> translation{};
    std::array<float,3> scale{{1,1,1}};
    float angle_degrees{};
    std::array<float,3> axis{};
    M3GMatrix general{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
};

// JSR184 Transformable composite: T * R * S * M. General matrices from the
// file are transposed from their serialized row-major order exactly once.
M3GMatrix m3g_transform_matrix(const M3GTransform&);
M3GMatrix m3g_multiply(const M3GMatrix&,const M3GMatrix&) noexcept;

struct M3GObjectInfo {
    std::size_t index{};
    std::uint8_t type{};
    std::uint32_t user_id{};
    std::string name;
    std::vector<std::uint32_t> animation_tracks;
    bool is_node{}, rendering_enabled{true}, has_alignment{};
    std::uint8_t alpha_factor{255};
    std::uint32_t scope{0xffffffffU};
    std::optional<std::size_t> parent;
    std::vector<std::uint32_t> children;
    M3GTransform transform;
};

struct M3GTextureState {
    // Serialized Texture2D identity survives image replacement. Two textures
    // may share an Image2D without sharing subsequent SetImage operations.
    std::size_t texture_object{};
    std::size_t image_object{};
    std::uint8_t image_format{};
    std::array<float,3> blend_color{};
    // Original JSR184 enumeration values, not invented renderer defaults.
    std::uint8_t function{227}; // MODULATE
    std::uint8_t wrap_s{241},wrap_t{241}; // REPEAT
    std::uint8_t level_filter{208},image_filter{210}; // BASE_LEVEL, NEAREST
    M3GMatrix transform{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
};

struct M3GMaterial {
    std::array<float,3> ambient{},emissive{},specular{};
    std::array<float,4> diffuse{};
    float shininess{};
    bool vertex_color_tracking{};
};

struct M3GAppearanceState {
    std::int8_t layer{};
    bool depth_test{true},depth_write{true},color_write{true},alpha_write{true};
    std::uint8_t blending{68}; // REPLACE
    float alpha_threshold{},depth_offset_factor{},depth_offset_units{};
    std::uint8_t culling{160},shading{165},winding{168}; // BACK, SMOOTH, CCW
    bool two_sided_lighting{},local_camera_lighting{},perspective_correction{};
    std::optional<M3GMaterial> material;
    std::optional<M3GTextureState> texture;
};

// Explicit 64-byte GPU layout. Positions are decoded local coordinates;
// model is carried separately on the surface. Texture coordinates remain
// homogeneous (S,T,R,Q); the texture transform is applied before projection.
struct alignas(16) M3GVertex {
    std::array<float,4> position;
    std::array<float,4> normal;
    std::array<float,4> color;
    std::array<float,4> texcoord;
};
static_assert(sizeof(M3GVertex)==64,"M3G GPU vertex stride changed");

struct M3GSurface {
    std::size_t mesh_object{},submesh_index{},vertex_buffer_object{},index_buffer_object{},appearance_object{};
    M3GMatrix model;
    float effective_alpha{1};
    std::uint32_t scope{0xffffffffU};
    // Original queue admission uses array presence/component count, not a
    // scan of expanded color alpha values. Zero means no vertex-color array.
    std::uint8_t vertex_color_components{};
    float default_color_alpha{1};
    M3GAppearanceState appearance;
    // Expanded strip triangles preserve alternating winding and degenerate
    // triangles. No normal, texture or coordinate-axis substitution is made.
    std::vector<M3GVertex> vertices;
};

struct M3GStaticModel {
    std::size_t root_object{};
    std::vector<M3GSurface> surfaces;
    std::map<std::size_t,RgbaImage> images;
    std::array<float,3> minimum{},maximum{};
    bool has_geometry{};
    std::size_t animation_track_bindings{};
    // This is the serialized static pose. Animation tracks, controller time,
    // runtime alignment and battle-event/choreography are not evaluated here.
};

struct M3GSpriteAsset {
    std::size_t node_object{},image_object{},appearance_object{};
    M3GMatrix model;
    float effective_alpha{1};
    std::uint32_t scope{0xffffffffU};
    bool scaled{true};
    std::uint8_t image_format{};
    std::array<std::int32_t,4> crop_pixels{}; // Signed x,y,width,height.
    M3GAppearanceState appearance;
};

enum class M3GDrawableKind : std::uint8_t { mesh,sprite };
struct M3GDrawableReference {
    M3GDrawableKind kind{M3GDrawableKind::mesh};
    std::size_t index{}; // Index into geometry.surfaces or sprites.
};
struct M3GRenderAsset {
    // Images for both meshes and Sprite3D are held once in geometry.images.
    M3GStaticModel geometry;
    std::vector<M3GSpriteAsset> sprites;
    // Original child-order traversal and submesh order, before the common
    // opaque/transparent queue. Meshes and sprites are not separate batches.
    std::vector<M3GDrawableReference> traversal;
};

// Native body decoder for rigid M3G scenes. Object graph and references are
// checked independently of the container framing. Nonrigid meshes, external
// resources and unsupported render-state paths fail explicitly when required.
class M3GScene {
public:
    explicit M3GScene(const Bytes&);
    const std::vector<M3GObjectInfo>& objects() const noexcept;
    std::vector<std::size_t> find_user_id(std::uint32_t) const;
    // root0 selects the one serialized World. A selected subtree retains its
    // real authored ancestors' transforms. Ancestor visibility/alpha are also
    // honored. This does not select a game form or an animation automatically.
    M3GStaticModel static_model(std::size_t root_object=0) const;
    // Immutable geometry for stateful rendering. Hidden nodes are retained
    // because a later original VISIBILITY sample can enable them. The caller
    // must use current evaluated enable, alpha and scope at drawing time.
    // The supported Sprite3D slice retains its real crop/image/appearance.
    M3GRenderAsset render_asset(std::size_t root_object=0) const;
private:
    M3GRenderAsset build_asset(std::size_t root_object,bool stateful) const;
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};

} // namespace g1
