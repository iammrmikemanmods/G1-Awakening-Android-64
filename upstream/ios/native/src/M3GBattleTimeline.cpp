#include "g1/M3GBattleTimeline.hpp"

#include <array>
#include <cstring>
#include <limits>

namespace g1 {
namespace {
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,
              "G1 timeline requires IEEE754 binary32");

constexpr std::array<std::int16_t,6> starts{{0,50,100,200,320,260}};
constexpr std::array<std::int16_t,6> ends{{20,90,160,250,350,300}};

float frame_scale() noexcept {
    const std::uint32_t bits=0x42055555U;
    float result;std::memcpy(&result,&bits,sizeof(result));return result;
}

std::int32_t add32(std::int32_t a,std::int32_t b) noexcept {
    const auto bits=static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b);
    std::int32_t result;std::memcpy(&result,&bits,sizeof(result));return result;
}
} // namespace

M3GBattleClipRange m3g_battle_clip_range(std::uint8_t clip) {
    if(clip>=starts.size())throw FormatError("M3G battle clip index is outside the six original ranges");
    const float start=static_cast<float>(starts[clip])*frame_scale();
    const float end=static_cast<float>(ends[clip])*frame_scale();
    return {starts[clip],ends[clip],static_cast<std::int32_t>(start),
            static_cast<std::int32_t>(end),end};
}

M3GBattleTimelineStep start_m3g_battle_clip(std::uint8_t clip) {
    const auto range=m3g_battle_clip_range(clip);
    return {range.start_ms,range.start_ms,false,false};
}

M3GBattleTimelineStep advance_m3g_battle_clip(std::uint8_t clip,
    std::int32_t stored_clock_ms,std::int32_t delta_ms,bool restart) {
    const auto range=m3g_battle_clip_range(clip);
    const auto next=add32(stored_clock_ms,delta_ms);
    if(static_cast<float>(next)<range.end_threshold_ms)
        return {stored_clock_ms,next,false,false};
    if(restart)return {range.start_ms,range.start_ms,true,true};
    return {range.end_call_ms,stored_clock_ms,true,false};
}

} // namespace g1
