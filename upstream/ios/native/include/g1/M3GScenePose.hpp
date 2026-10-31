#pragma once

#include "g1/M3GAnimation.hpp"
#include "g1/M3GCamera.hpp"

namespace g1 {

struct M3GNodeProperties {
    float alpha{1};
    bool rendering_enabled{true};
    std::uint32_t scope{0xffffffffU};
};

struct M3GEvaluatedNode {
    M3GMatrix model;
    float effective_alpha{1};
    bool rendering_enabled{true};
    // Original traversal tests scope at each drawable. Ancestor scope values
    // are not implicitly intersected with this node's own scope.
    std::uint32_t scope{0xffffffffU};
};

// Persistent evaluated state for the recovered rigid Node and Camera paths.
// A missing sample preserves the previous value. The caller supplies decoded
// camera objects from this same scene; no camera or game form is auto-selected.
class M3GScenePose {
public:
    M3GScenePose(const M3GScene&,
                const std::map<std::size_t,M3GCameraProjection>& cameras);
    explicit M3GScenePose(const Bytes&);

    // Supports alpha256, camera field-of-view264, orientation268, scale270,
    // translation275 and visibility276. All targets, widths and finite values
    // are checked before any assignment. Alpha and visibility are local node
    // fields; evaluated_nodes() applies their real parent inheritance.
    void apply(const std::vector<M3GPropertySample>&);
    void set_general_matrix(std::size_t,const M3GMatrix&);
    void set_translation(std::size_t,const std::array<float,3>&);
    void translate(std::size_t,const std::array<float,3>&);
    std::array<float,3> translation(std::size_t) const;
    void set_rendering_enabled(std::size_t,bool);
    void detach(std::size_t);
    const std::map<std::size_t,M3GNodeProperties>& local_nodes() const noexcept {
        return local_;
    }
    const std::map<std::size_t,M3GCameraProjection>& cameras() const noexcept {
        return cameras_;
    }
    std::map<std::size_t,M3GEvaluatedNode> evaluated_nodes() const;

private:
    M3GRigidPose transforms_;
    std::map<std::size_t,M3GNodeProperties> local_;
    std::map<std::size_t,std::optional<std::size_t>> parents_;
    std::map<std::size_t,M3GCameraProjection> cameras_;
};

} // namespace g1
