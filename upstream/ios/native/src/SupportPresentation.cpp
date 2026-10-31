#include "g1/SupportPresentation.hpp"

#include <stdexcept>
#include <utility>

namespace g1 {

SupportPresentation::SupportPresentation(std::uint64_t token,
                                         std::vector<std::size_t> helper_units,
                                         std::vector<AtlasStep> effect_steps)
    : token_(token),helper_units_(std::move(helper_units)) {
    if(helper_units_.size()>support_storage_slots)
        throw FormatError("support presentation exceeds original helper storage");
    phase_=begin_support_presentation(static_cast<std::uint8_t>(helper_units_.size()));
    if(!phase_.complete)
        effect_.emplace(std::move(effect_steps),support_effect_repetitions,false);
}

SupportPresentationStatus SupportPresentation::status() const noexcept {
    SupportPresentationStatus result;
    result.token=token_;result.revision=revision_;
    result.helper_count=phase_.helper_count;result.slot=phase_.slot;
    result.elapsed_ms=phase_.elapsed_ms;result.complete=phase_.complete;
    if(!phase_.complete)result.helper_unit=helper_units_[phase_.slot];
    if(effect_) {
        result.effect_frame=effect_->current_frame();
        result.effect_complete=effect_->state().completed;
    }
    return result;
}

std::optional<SpriteAnimationState> SupportPresentation::animation_state() const noexcept {
    if(effect_)return effect_->state();
    return std::nullopt;
}

SupportPresentationAction SupportPresentation::advance(std::uint32_t delta_ms) {
    if(phase_.complete)return SupportPresentationAction::waiting;
    if(delta_ms>std::uint32_t(std::numeric_limits<std::int32_t>::max()))
        throw std::out_of_range("support delta exceeds the original signed 32-bit range");
    // Original map wrapper 0x5ca44 does not update a completed 2D effect.
    // Effect completion at tick 56 hides it; the phase continues to tick 67.
    if(!effect_->state().completed)effect_->advance_ms(delta_ms);
    const auto action=advance_support_presentation(phase_,static_cast<std::int32_t>(delta_ms));
    if(action==SupportPresentationAction::show_next_helper)
        effect_->reset(support_effect_repetitions,false);
    ++revision_;
    return action;
}

} // namespace g1
