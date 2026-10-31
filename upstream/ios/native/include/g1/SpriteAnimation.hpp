#pragma once

#include "g1/Atlas.hpp"

namespace g1 {

struct SpriteAnimationState {
    std::size_t step_index{};
    std::int32_t elapsed_ms{};
    std::int32_t repeat_count{};
    bool reverse{};
    bool completed{};
};

// Native transcription of original start 0x72d80 and advance 0x72cc0.
// The clock reaching that routine is measured in integer milliseconds.
// The sequence is owned; AtlasDescriptor need not outlive the player.
class SpriteAnimation {
public:
    explicit SpriteAnimation(std::vector<AtlasStep> steps,
                             std::int32_t repeat_count = -1, bool reverse = false);
    // Native checkpoint restoration, without restarting the sequence. The
    // caller supplies the same decoded sequence used when saving the state.
    // An index outside that sequence and a negative elapsed time are rejected.
    // Elapsed time may exceed the selected duration: original select_step()
    // preserves it. Sticky completion likewise does not constrain the index.
    static SpriteAnimation restore(std::vector<AtlasStep> steps,
                                   const SpriteAnimationState& state);
    void reset(std::int32_t repeat_count = -1, bool reverse = false) noexcept;

    // Original 0x72cb4 changes only the selected step when it is in range.
    // Used by the action-used current-player pose; elapsed/repeats/completion
    // are preserved, unlike reset(). Out-of-range requests do nothing.
    void select_step(std::uint8_t step) noexcept;

    std::uint8_t current_frame() const noexcept { return steps_[state_.step_index].frame_index; }
    const SpriteAnimationState& state() const noexcept { return state_; }

    // Returns whether the final step differs from its entry value, matching
    // the original geometry-refresh decision. It does not equate a full
    // animation cycle with a changed final frame. Completion is sticky until
    // reset, but subsequent calls retain the original advance behavior.
    //
    // Forward playback deliberately uses the entry step's duration for all
    // transitions within one tick. Reverse playback reads each new duration.
    // Repeat 0 loops forward but completes reverse; negative repeats loop.
    // Invalid/overflowing signed-32-bit deltas are rejected before mutation.
    bool advance_ms(std::uint32_t delta_ms);

private:
    std::vector<AtlasStep> steps_;
    std::int64_t cycle_duration_ms_{};
    SpriteAnimationState state_{};
};

} // namespace g1
