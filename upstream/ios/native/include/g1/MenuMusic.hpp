#pragma once

#include "g1/MissionProgression.hpp"

#include <optional>

namespace g1 {

// Identity and parent link are the only original menu fields read by the
// music maintenance body. Menus form an acyclic parent chain.
struct MenuMusicNode {
    const MenuMusicNode* parent{};
};

struct MenuMusicContext {
    bool music_enabled{};
    std::int32_t application_mode{10};
    const MenuMusicNode* current{};
    const MenuMusicNode* enable_sounds{}; // Original app+0x258: current only.
    const MenuMusicNode* pause{};         // Original app+0x260: any ancestor.
    const MenuMusicNode* opponent{};      // Original app+0x278: any ancestor.
    const MenuMusicNode* unit_actions{};  // Original app+0x268: any ancestor.
};

// Original 0x36eae..0x36ed8. Call operation 6 only after these guards pass.
// Mode 3 is loading/transition; mode 100 is gameplay. A null current equals
// a null enable-sounds pointer and therefore still suppresses the query.
inline bool menu_music_queries_playing(const MenuMusicContext& context) noexcept {
    return context.music_enabled && context.application_mode != 3 &&
           context.application_mode != 100 && context.current != context.enable_sounds;
}

// Original 0x36ee0..0x36f1e. Call only after operation 6 reports no voice
// playing. The three exclusions include the current menu and all parents;
// the enable-sounds menu is deliberately not an ancestor exclusion.
inline std::optional<MissionAudioRequest> idle_menu_music(
    const MenuMusicContext& context) noexcept {
    for (auto* menu = context.current; menu; menu = menu->parent)
        if (menu == context.pause || menu == context.opponent ||
            menu == context.unit_actions)
            return {};
    return MissionAudioRequest{5, 0};
}

} // namespace g1
