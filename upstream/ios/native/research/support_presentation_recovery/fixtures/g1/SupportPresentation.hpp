#pragma once

#include "g1/Archive.hpp"

#include <cstdint>
#include <limits>

namespace g1 {

// Original map allocation stores six helper pointers. Its presentation update
// hands off when the incremented slot reaches five, even with six helpers.
// Combat still accounts for all six; this interface changes no combat state.
inline constexpr std::uint8_t support_storage_slots=6;
inline constexpr std::uint8_t support_presented_slots=5;
inline constexpr std::uint16_t support_text_id=0x01f7;
inline constexpr std::int32_t support_effect_animation=2;
inline constexpr std::int32_t support_effect_repetitions=1;

struct SupportPresentationState {
    std::uint8_t helper_count{},slot{};
    std::int32_t elapsed_ms{};
    bool complete{true};
};

enum class SupportPresentationAction : std::uint8_t {
    waiting, show_next_helper, start_main_battle,
};

// A nonempty sequence immediately shows helper0. An empty list bypasses this
// phase. The caller retains the already-resolved SessionBattle and its token.
inline SupportPresentationState begin_support_presentation(std::uint8_t count) {
    if(count>support_storage_slots)throw FormatError("support presentation exceeds original helper storage");
    return {count,0,0,count==0};
}

// Original0x583f0 with its actual always-true0x5d176 helper. This is elapsed
// simulation time, not sprite completion or a guessed rendering-ready event.
// Each call advances at most one helper; surplus time is discarded at the
// next helper, while the final elapsed value remains stored at handoff.
inline SupportPresentationAction advance_support_presentation(SupportPresentationState& state,
                                                               std::int32_t delta_ms) {
    if(state.helper_count>support_storage_slots)
        throw FormatError("support presentation exceeds original helper storage");
    if(state.complete)return SupportPresentationAction::waiting;
    if(state.helper_count==0 || state.slot>=state.helper_count || state.slot>=support_presented_slots)
        throw FormatError("support presentation has an invalid active slot");
    const auto raw=static_cast<std::uint32_t>(state.elapsed_ms)+static_cast<std::uint32_t>(delta_ms);
    // ARM ADD wraps. Avoid both signed C++ overflow and implementation-defined
    // conversion of an out-of-range unsigned value to signed.
    state.elapsed_ms=raw<=std::uint32_t(std::numeric_limits<std::int32_t>::max()) ?
        static_cast<std::int32_t>(raw) :
        -1-static_cast<std::int32_t>(std::numeric_limits<std::uint32_t>::max()-raw);
    if(state.elapsed_ms<=1999)return SupportPresentationAction::waiting;
    ++state.slot;
    if(state.slot==support_presented_slots || state.slot>=state.helper_count) {
        state.complete=true;return SupportPresentationAction::start_main_battle;
    }
    state.elapsed_ms=0;return SupportPresentationAction::show_next_helper;
}

} // namespace g1
