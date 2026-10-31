#pragma once

#include "g1/M3GBindings.hpp"

namespace g1 {

struct M3GBattleCameraRequest {
    std::uint8_t camera_id{};
    std::array<std::uint8_t,2> forms{};
    // Original cinematic bytes +0x6c/+0x6d. Their producer is a separate
    // model-setup boundary; preserve the supplied byte, not a guessed form rule.
    std::array<std::uint8_t,2> adjustment_flags{};
};

struct M3GBattleCameraRoute {
    bool side_a{};
    // ID8 bypasses model lookup. IDs6/7 both prefer model A's robot camera.
    std::optional<M3GModelCameraKind> preferred_model_camera;
    std::string_view scene_camera_name;
    std::uint32_t scene_camera_user_id{}; // raw hash, without Transform suffix
    std::uint8_t adjustment_flag{};
};

// Exact original camera-ID routing from 3d9d0. The caller resolves the actual
// model camera through M3GNodeBindings, or the raw named Camera in the scene.
// Missing required scene data must not be replaced by a fabricated camera.
M3GBattleCameraRoute m3g_battle_camera_route(const M3GBattleCameraRequest&);

struct M3GBattleCameraNode {
    std::uint32_t instance{};
    std::size_t object{};
};
inline bool operator==(const M3GBattleCameraNode& a,const M3GBattleCameraNode& b) noexcept {
    return a.instance==b.instance && a.object==b.object;
}
inline bool operator!=(const M3GBattleCameraNode& a,const M3GBattleCameraNode& b) noexcept {
    return !(a==b);
}

// Explicit actual parent links, including any attachment across instances.
// Node handles identify native instances and serialized objects, not guest
// pointers. A null current Camera is valid. Parents cannot exist without it.
struct M3GBattleCameraAncestry {
    std::optional<M3GBattleCameraNode> camera,parent,grandparent;
};

struct M3GBattleCameraState {
    bool has_model_camera{}; // original non-null cinematic+0x8c
    bool side_a{};           // original cinematic+0x9c
};

enum class M3GBattleCameraEffectKind : std::uint8_t {
    animate, set_active_camera, translate
};
struct M3GBattleCameraEffect {
    M3GBattleCameraEffectKind kind{M3GBattleCameraEffectKind::animate};
    M3GBattleCameraNode node;
    std::int32_t time{};
    std::array<float,3> translation{};
};
struct M3GBattleCameraSwitch {
    M3GBattleCameraRoute route;
    M3GBattleCameraState state;
    std::int32_t before_time{},after_time{};
    std::vector<M3GBattleCameraEffect> effects;
};

// Original 3d51e: animate the grandparent when present, otherwise the parent.
// A supplied nonzero adjustment with no cached model camera then adds (0,0,2)
// to the immediate parent's local component translation, after animation.
// The source-confirmed public slot is additive Translate, not SetTranslation.
std::vector<M3GBattleCameraEffect> m3g_battle_camera_propagation(
    const M3GBattleCameraAncestry&,std::int32_t time,std::uint8_t adjustment_flag,
    bool has_model_camera);

// Plan the original before-selection / setCamera / after-selection sequence.
// model_camera_found must report a real successful model binding; selected
// ancestry must describe that Camera or the real scene fallback. The caller
// applies the effects to native poses in order and owns rendering/presentation.
// Both ancestry inputs must describe the same graph; no reparent is performed.
// An animate effect applies only to that actual Node's animation subtree.
// No phase advancement, camera completion or GPU acknowledgement occurs here.
M3GBattleCameraSwitch m3g_battle_camera_switch(
    const M3GBattleCameraRequest&,M3GBattleCameraState previous,
    bool model_camera_found,const std::array<std::int32_t,2>& model_clocks,
    const M3GBattleCameraAncestry& current,const M3GBattleCameraAncestry& selected);

} // namespace g1
