#pragma once

#include "g1/Image.hpp"
#include "g1/M3G.hpp"

#include <map>
#include <optional>

namespace g1 {

enum class M3GMaterialSlot : std::uint8_t { material, material2 };

// Identity metadata, not rendering state. References are a complete SET of
// outgoing Object3D references; their order is deliberately not a find order.
// children, texture_units and submesh_appearances retain their original order.
// The explicit-data constructor is useful for contract/original-vector tests.
struct M3GMaterialBindingObject {
    std::size_t index{};
    std::uint8_t type{};
    std::uint32_t user_id{};
    std::vector<std::size_t> references;
    std::vector<std::size_t> children;
    std::vector<std::size_t> texture_units;
    std::vector<std::size_t> submesh_appearances;
    std::size_t texture_image{};
};

struct M3GTextureImageOverride {
    M3GMaterialSlot slot{M3GMaterialSlot::material};
    std::uint32_t resource_id{}; // Original 0x03000000-tagged image, tag removed.
    std::optional<std::size_t> named_object;
    std::size_t appearance_object{};
    // The original fallback permits an existing Appearance with no texture0:
    // the image was loaded but no SetImage call follows. This remains explicit.
    std::optional<std::size_t> texture_object;
    std::optional<std::size_t> authored_image_object;
    bool used_mesh_fallback{};
};

// Native source-only binding recovery for original 0x3e7e6..0x3f560.
// Each required name must have at most one matching reachable Object3D ID.
// Ambiguous IDs fail: this is NOT a claim to implement the complete original
// Object3D.find reference ordering. All seven authored override models meet
// this bounded condition. The fallback is the original first-child chain,
// not a whole-tree "find any mesh" search. Visibility does not affect lookup.
class M3GMaterialBindings {
public:
    explicit M3GMaterialBindings(const Bytes& original_model_m3g);
    explicit M3GMaterialBindings(std::vector<M3GMaterialBindingObject> objects);
    const std::vector<M3GMaterialBindingObject>& objects() const noexcept { return objects_; }

    // Pass the actually selected model Group (9 or World22's Group interface)
    // and the effective constructor type after the original type17 remap.
    // Steps are ordered material, then material2. Applying them in order to
    // every reference to texture_object preserves shared-texture last writes.
    // Other Texture2D state is unchanged. This does not load/attach a model,
    // perform the type remap, mutate a scene or upload GPU resources.
    std::vector<M3GTextureImageOverride> plan(std::size_t root_group,
                                             std::uint32_t effective_type) const;
private:
    std::vector<M3GMaterialBindingObject> objects_;
    std::map<std::size_t,std::size_t> positions_;
    void validate();
};

// Original table0xceb14: absent slots are nullopt; type must be0..23.
std::array<std::optional<std::uint32_t>,2>
m3g_material_image_resources(std::uint32_t effective_type);

struct M3GMaterialImage {
    std::uint32_t resource_id{};
    std::uint8_t image_format{}; // Proven Image2D format99 (RGB) for this set.
    RgbaImage image;            // Straight RGBA8, top-to-bottom; alpha is255.
};

// The eleven proven replacement PNGs only. Requires original source dimensions
// and opaque RGB/indexed-RGB encoding; a changed format fails explicitly.
// Pixel decoding uses the native PNG decoder without display/gamma conversion.
M3GMaterialImage decode_m3g_material_image(std::uint32_t resource_id,const Bytes& png);
M3GMaterialImage load_m3g_material_image(const Archive&,std::uint32_t resource_id);

} // namespace g1
