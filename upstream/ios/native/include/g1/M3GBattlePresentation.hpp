#pragma once

#include "g1/Combat.hpp"
#include "g1/M3GRenderFrame.hpp"
#include "g1/MissionExecution.hpp"

#include <memory>

namespace g1 {

// One already resolved real exchange. Presentation does not mutate combat,
// consume the game's random stream or authorize a gameplay action.
struct M3GBattleRequest {
    std::uint64_t token{};
    std::uint8_t terrain_style{};
    CombatUnitState attacker_before,defender_before;
    CombatExchange exchange;
};

struct M3GBattleAsset {
    std::uint32_t resource_id{};
    std::size_t selected_root{};
    M3GRenderAsset render;
};

struct M3GBattleStatus {
    std::uint64_t token{},revision{};
    std::uint8_t phase{};
    std::int32_t phase_time_ms{};
    std::int32_t slide_fixed16{};
    bool complete{};
    bool health_visible{};
    std::array<std::uint8_t,2> types{},forms{},health_before{},health_after{};
    std::array<std::int32_t,2> display_health{};
    std::optional<std::int32_t> damage_text;
    bool damage_on_left{};
};

struct M3GBattleSubmission {
    std::size_t asset_index{};
    // Each node submission has its own original queue. The World, model A,
    // model B, effects0..3 and death node are NOT globally sorted together.
    M3GPreparedFrame frame;
};

struct M3GBattleFrame {
    std::uint64_t token{},revision{};
    bool complete{};
    bool clear_color{},clear_depth{};
    std::array<float,4> background_color{};
    std::vector<M3GBattleSubmission> submissions;
};

// Portable original-asset battle graph and controller. The Apple host uploads
// assets once, then passes the GPU models' retained immutable asset identities
// to prepare_frame. Completion here is a controller condition; only an actual
// presented matching frame may acknowledge GameSession's pending token.
class M3GBattlePresentation {
public:
    M3GBattlePresentation(const Archive&,M3GBattleRequest);
    ~M3GBattlePresentation();
    M3GBattlePresentation(const M3GBattlePresentation&)=delete;
    M3GBattlePresentation& operator=(const M3GBattlePresentation&)=delete;

    const std::vector<M3GBattleAsset>& assets() const noexcept;
    M3GBattleStatus status() const noexcept;
    // Called with the native original-logic simulation delta, separately from
    // display refresh. A completed controller never restarts itself.
    void advance(std::int32_t delta_ms);
    std::vector<MissionAudioRequest> take_audio_requests();
    M3GBattleFrame prepare_frame(std::array<float,2> viewport_pixels,
        const std::vector<const M3GRenderAsset*>& uploaded_assets,
        float render_interpolation=1.0F,
        std::optional<float> projection_aspect_override=std::nullopt) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace g1
