#pragma once

#include "g1/MissionProgression.hpp"

#include <optional>

namespace g1 {

enum class MusicMaintenanceSite { map_update, ordinary_battle };

// The map-update site is its active body, after the application's outer
// mode-100 dispatch. These two numbered values preserve the original inner
// guards: map+0x2d8 == 7 is results, app+0x1c0 == 10 is the menu state.
// The battle site has only the enabled-a4 guard.
struct MusicMaintenanceContext {
    MusicMaintenanceSite site{MusicMaintenanceSite::map_update};
    bool music_enabled{};
    std::int32_t map_interaction_mode{};
    std::int32_t application_mode{100};
};

struct MusicMaintenancePlan {
    bool query_playing{};
    // Dispatch this only when the real operation-6 query returns false.
    // A style of -1 still permits the query, but produces no play request.
    std::optional<MissionAudioRequest> when_idle;
};

// Original unchanged bodies 0x5b5a4..0x5b5e8 / 0x428a4..0x428c6,
// followed by the real style selector 0x4e7b8. The host supplies actual output
// state at the query boundary; no duration, loop flag or operation 4 is used.
inline MusicMaintenancePlan plan_music_maintenance(
    MusicMaintenanceContext context, std::int32_t terrain_style) noexcept {
    if (!context.music_enabled ||
        (context.site == MusicMaintenanceSite::map_update &&
         (context.map_interaction_mode == 7 || context.application_mode == 10)))
        return {};
    MusicMaintenancePlan plan{true, {}};
    if (terrain_style != -1) {
        const std::int32_t slot = terrain_style == 0 ? 1 : terrain_style == 2 ? 2 : 3;
        plan.when_idle = MissionAudioRequest{5, slot};
    }
    return plan;
}

} // namespace g1
