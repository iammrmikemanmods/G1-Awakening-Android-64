#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace g1 {

inline constexpr std::uint32_t support_tutorial_text_resource = 0x21ff04a8U;
inline constexpr std::uint8_t support_tutorial_default_pending = 1;
using SupportTutorialSlots = std::array<std::optional<std::uint8_t>, 6>;

struct SupportTutorialDecision {
    std::uint8_t pending_flag{};
    bool request_message{};
};

// Original 0x58796..0x5884e. This is app/profile presentation state, not a
// combat modifier. The original scans all six allocated slots, even though
// the preceding support animation presents at most five helpers. Controller
// zero is human; other signed values must not pass this predicate.
//
// The caller publishes pending_flag before requesting the original message.
// A true request is made once for this battle entry, not every display tick.
// File persistence and rendered acknowledgement remain explicit host work.
inline SupportTutorialDecision support_tutorial_entry(
    std::uint8_t pending_flag, std::uint8_t current_player,
    std::int8_t current_controller, const SupportTutorialSlots& helper_owners) noexcept {
    bool owned_helper = false;
    for (const auto owner : helper_owners) {
        if (owner && *owner == current_player) {
            owned_helper = true;
            break;
        }
    }
    if (pending_flag != 0 && owned_helper && current_controller == 0)
        return {0, true};
    return {pending_flag, false};
}

} // namespace g1
