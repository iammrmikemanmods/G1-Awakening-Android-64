#include "g1/SpriteAnimation.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace g1 {

SpriteAnimation::SpriteAnimation(std::vector<AtlasStep> steps,
                                 std::int32_t repeat_count, bool reverse)
    : steps_(std::move(steps)) {
    if (steps_.empty() || steps_.size() > 255U)
        throw std::invalid_argument("sprite animation requires 1..255 steps");
    for (const auto& step : steps_) {
        if (step.duration_units == 0U || step.duration_units > 32767U)
            throw std::invalid_argument("sprite duration must be a positive signed 16-bit millisecond value");
        cycle_duration_ms_ += step.duration_units;
    }
    reset(repeat_count, reverse);
}

SpriteAnimation SpriteAnimation::restore(std::vector<AtlasStep> steps,
                                         const SpriteAnimationState& state) {
    SpriteAnimation result(std::move(steps), state.repeat_count, state.reverse);
    if (state.step_index >= result.steps_.size())
        throw std::out_of_range("saved sprite step is outside its decoded sequence");
    if (state.elapsed_ms < 0)
        throw std::invalid_argument("saved sprite elapsed time is negative");
    result.state_ = state;
    return result;
}

void SpriteAnimation::reset(std::int32_t repeat_count, bool reverse) noexcept {
    state_ = {};
    state_.repeat_count = repeat_count;
    state_.reverse = reverse;
    if (reverse) {
        state_.step_index = steps_.size() - 1U;
        state_.elapsed_ms = steps_.back().duration_units;
    }
}

void SpriteAnimation::select_step(std::uint8_t step) noexcept {
    if (step < steps_.size()) state_.step_index = step;
}

bool SpriteAnimation::advance_ms(std::uint32_t delta_ms) {
    constexpr auto signed_max = std::numeric_limits<std::int32_t>::max();
    if (delta_ms > std::uint32_t(signed_max))
        throw std::out_of_range("sprite delta exceeds the original signed 32-bit range");
    const auto entry_index = state_.step_index;
    if (!state_.reverse) {
        const auto total = std::int64_t(state_.elapsed_ms) + delta_ms;
        if (total > signed_max)
            throw std::out_of_range("sprite elapsed-time addition would overflow the original signed range");
        const auto duration = steps_[entry_index].duration_units;
        const auto transitions = std::uint64_t(total / duration);
        state_.elapsed_ms = std::int32_t(total % duration);
        const auto next = std::uint64_t(entry_index) + transitions;
        const auto wraps = next / steps_.size();
        if (state_.repeat_count > 0 && wraps >= std::uint64_t(state_.repeat_count)) {
            state_.step_index = steps_.size() - 1U;
            state_.repeat_count = 1;
            state_.completed = true;
        } else {
            state_.step_index = std::size_t(next % steps_.size());
            if (state_.repeat_count > 0) state_.repeat_count -= std::int32_t(wraps);
        }
    } else {
        auto remaining = std::int64_t(state_.elapsed_ms) - delta_ms;
        // Skip complete reverse cycles without changing the original order or
        // strict '< 0' boundary. A finite sequence keeps its last repetition.
        if (remaining < 0 && (state_.repeat_count < 0 || state_.repeat_count > 1)) {
            auto cycles = (-remaining - 1) / cycle_duration_ms_;
            if (state_.repeat_count > 1)
                cycles = std::min(cycles, std::int64_t(state_.repeat_count) - 1);
            remaining += cycles * cycle_duration_ms_;
            if (state_.repeat_count > 1) state_.repeat_count -= std::int32_t(cycles);
        }
        while (remaining < 0) {
            if (state_.step_index == 0U && state_.repeat_count >= 0 && state_.repeat_count <= 1) {
                state_.completed = true;
                const auto duration = steps_.front().duration_units;
                remaining += ((-remaining + duration - 1) / duration) * duration;
                break;
            }
            if (state_.step_index == 0U) {
                state_.step_index = steps_.size() - 1U;
                if (state_.repeat_count > 1) --state_.repeat_count;
            } else {
                --state_.step_index;
            }
            remaining += steps_[state_.step_index].duration_units;
        }
        state_.elapsed_ms = std::int32_t(remaining);
    }
    return state_.step_index != entry_index;
}

} // namespace g1
