#pragma once

#include "g1/M3GScene.hpp"

#include <string_view>

namespace g1 {

// Original 0x26b4c: reverse signed bytes, wrapped uint32 multiply/add, then
// two's-complement absolute-value bits masked to 30 bits. Input is the name
// without a C terminator; embedded NUL is rejected explicitly.
std::uint32_t m3g_name_hash(std::string_view name);

// Original 0x2701c always appends "Transform", including for an empty name or
// an already suffixed name. This computes the same hash without allocating.
std::uint32_t m3g_transform_name_hash(std::string_view base_name);

struct M3GFormNodeBindings {
    std::optional<std::size_t> robot;
    std::optional<std::size_t> car;
};

enum class M3GModelCameraKind : std::uint8_t { robot, vehicle };

struct M3GModelCameraBinding {
    std::size_t transform{};
    std::size_t inner_group{};
    std::size_t camera{};
};

// Immutable bounded Node hierarchy, copied from decoded object metadata.
// This is a Node lookup, NOT Object3D.find across arbitrary references:
// animation tracks, images, appearances and other non-child references are
// deliberately not traversed. IDs need not be contiguous or numerically
// sorted. Child order is retained; parents are derived from child lists.
// Duplicate IDs, dangling/non-Node children, multiple parents and cycles fail.
class M3GNodeBindings {
public:
    explicit M3GNodeBindings(const std::vector<M3GObjectInfo>& objects);
    explicit M3GNodeBindings(const M3GScene& scene) : M3GNodeBindings(scene.objects()) {}

    // Iterative self-first, depth-first traversal, bounded by this root's
    // descendants. A valid subtree with no matching Node returns nullopt.
    std::optional<std::size_t> find_node(std::size_t root,std::uint32_t user_id) const;

    // The caller supplies the actual loaded-container child order. The
    // container is not assumed to be a serialized World or any inferred class.
    // Select child0 if it exposes the recovered Group interface; otherwise
    // require that interface on child1. Group9 and World22 both support it,
    // as the original public query confirms. Never scan child2+.
    // Missing/invalid required children fail; the graph is not reparented.
    std::size_t select_loaded_container_group(const std::vector<std::size_t>& children) const;

    // Uses the suffixed carTransform/robotTransform keys. No visibility or
    // selected-form policy is applied, and missing forms remain optional.
    M3GFormNodeBindings form_nodes(std::size_t root) const;

    // Search roboCAMTransform or vehiCAMTransform, then require exactly
    // Group-compatible -> child0 Group-compatible -> child0 Camera5.
    // Group9 and World22 expose the required interface. An absent transform returns
    // nullopt; a found but malformed chain fails. No alternative camera or
    // later child is substituted. Camera-ID and phase choice belong to caller.
    std::optional<M3GModelCameraBinding> model_camera(
        std::size_t root,M3GModelCameraKind kind) const;

private:
    struct Object {
        std::uint8_t type{};
        std::uint32_t user_id{};
        std::vector<std::size_t> children;
    };
    std::map<std::size_t,Object> objects_;
    const Object& object(std::size_t index) const;
    const Object& node(std::size_t index) const;
    bool group_candidate(std::size_t index) const;
};

} // namespace g1
