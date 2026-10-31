#pragma once

#include "g1/Archive.hpp"

namespace g1 {

// These are indices into the original six-range shared model timeline. They
// are not controller-object indices, cinematic phases, or completion tokens.
struct M3GBattleClipRange {
    std::int16_t start_frame{},end_frame{};
    std::int32_t start_ms{},end_call_ms{};
    float end_threshold_ms{};
};

struct M3GBattleTimelineStep {
    // Apply the rigid animation at this time. Below the end this is the OLD
    // stored clock, even though stored_clock_ms already gives the next clock.
    std::int32_t animation_time_ms{};
    std::int32_t stored_clock_ms{};
    // Original helper return only. The cinematic controller has its own phase
    // timer and ignores this return at its recovered call sites.
    bool reached_end{};
    bool restarted{};
};

M3GBattleClipRange m3g_battle_clip_range(std::uint8_t clip);
M3GBattleTimelineStep start_m3g_battle_clip(std::uint8_t clip);

// Recovered 0x3c950 ordering, binary32 end comparison, and 32-bit wrapping ADD.
// At a non-restarting end, the original leaves its stored clock unchanged.
// A restart discards overshoot. This computes one update, not a full battle.
M3GBattleTimelineStep advance_m3g_battle_clip(std::uint8_t clip,
    std::int32_t stored_clock_ms,std::int32_t delta_ms,bool restart);

} // namespace g1
