#pragma once

#include "g1/M3GScene.hpp"

#include <set>

namespace g1 {

struct M3GKeyframe {
    std::int32_t time{};
    std::vector<float> value;
};

struct M3GSequence {
    std::size_t object{};
    std::uint8_t interpolation{},repeat_mode{};
    std::int32_t duration{};
    std::size_t first{},last{},components{};
    std::vector<M3GKeyframe> keyframes;
};

struct M3GController {
    std::size_t object{};
    float speed{1},weight{1};
    std::int32_t active_start{},active_end{};
    float reference_sequence_time{};
    std::int32_t reference_world_time{};
};

struct M3GTrack {
    std::size_t object{},sequence{},controller{};
    std::uint32_t property{};
};

struct M3GPropertySample {
    std::size_t target{};
    std::uint32_t property{};
    std::vector<float> value;
};

struct M3GPreparedKeyframe {
    std::int32_t time{};
    std::vector<float> value,outgoing,incoming;
};

// Immutable prepared curves. Only keys in the valid range are normalized or
// given cubic auxiliaries. A sampler owns its copy and can be reused per frame.
class M3GSequenceSampler {
public:
    explicit M3GSequenceSampler(const M3GSequence&);
    std::vector<float> sample(float sequence_time) const;
    const std::vector<M3GPreparedKeyframe>& keyframes() const noexcept { return keyframes_; }
private:
    std::uint8_t interpolation_{},repeat_mode_{};
    std::int32_t duration_{};
    std::size_t components_{};
    std::vector<std::size_t> order_;
    std::vector<M3GPreparedKeyframe> keyframes_;
};

// Recovered controller mapping and sequence sampling. Supported interpolation
// is LINEAR176, SLERP177, SPLINE178, SQUAD179 and STEP180. Sampling returns
// original arithmetic policy; it does not select a game battle clip.
bool m3g_controller_active(const M3GController&,std::int32_t world_time) noexcept;
float m3g_controller_position(const M3GController&,std::int32_t world_time,
                              std::int32_t loop_duration=0);
std::vector<float> m3g_sample_sequence(const M3GSequence&,float sequence_time);

class M3GAnimations {
public:
    explicit M3GAnimations(const Bytes&);
    const std::map<std::size_t,M3GSequence>& sequences() const noexcept { return sequences_; }
    const std::map<std::size_t,M3GController>& controllers() const noexcept { return controllers_; }
    const std::map<std::size_t,M3GTrack>& tracks() const noexcept { return tracks_; }
    const std::map<std::size_t,std::vector<std::size_t>>& bindings() const noexcept { return bindings_; }

    // An override changes only that real controller. Inactive/zero-weight
    // tracks yield no assignment, so the target retains its preceding state.
    // Orientation contributions are normalized after weighted accumulation.
    // Alpha256, camera field-of-view264 and visibility276 remain scalar sums;
    // M3GScenePose applies their original clamps/thresholds after accumulation.
    std::vector<M3GPropertySample> sample(std::int32_t world_time,
        const std::map<std::size_t,M3GController>& overrides={}) const;
    // The caller supplies the actual traversed Object3D targets. Excluded
    // tracks are not sampled, so animating one camera subtree cannot advance
    // another actor or evaluate an unrelated unsupported property.
    std::vector<M3GPropertySample> sample_targets(std::int32_t world_time,
        const std::set<std::size_t>& targets,
        const std::map<std::size_t,M3GController>& overrides={}) const;
private:
    std::vector<M3GPropertySample> sample_selected(std::int32_t,
        const std::set<std::size_t>*,const std::map<std::size_t,M3GController>&) const;
    std::map<std::size_t,M3GSequence> sequences_;
    std::map<std::size_t,M3GSequenceSampler> samplers_;
    std::map<std::size_t,M3GController> controllers_;
    std::map<std::size_t,M3GTrack> tracks_;
    std::map<std::size_t,std::vector<std::size_t>> bindings_;
};

// Stateful rigid transforms initialized from the authored scene. apply() only
// changes actively sampled translation/orientation/scale. Visibility, texture
// animation, skinning, morphing and runtime alignment are outside this slice.
class M3GRigidPose {
public:
    explicit M3GRigidPose(const M3GScene&);
    void apply(const std::vector<M3GPropertySample>&);
    // Original runtime operations used by model attachment and camera
    // propagation. General assignment preserves TRS; Translate adds to the
    // local component translation, not a world or general-matrix element.
    void set_general_matrix(std::size_t,const M3GMatrix&);
    void set_translation(std::size_t,const std::array<float,3>&);
    void translate(std::size_t,const std::array<float,3>&);
    std::array<float,3> translation(std::size_t) const;
    void detach(std::size_t); // Retains all local components and descendants.
    std::map<std::size_t,M3GMatrix> world_matrices() const;
    // Reuses existing rigid geometry/images; changes per-surface matrices and
    // recomputes bounds. A serialized pose is not reselected at each update.
    void update_model(M3GStaticModel&) const;
private:
    struct State {
        M3GObjectInfo info;
        std::optional<std::array<float,4>> orientation;
    };
    std::map<std::size_t,State> states_;
};

} // namespace g1
