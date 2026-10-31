#pragma once

#include "g1/SupportPresentation.hpp"

namespace g1 {

struct SupportFrameReceipt {
    std::uint64_t token{},revision{},display_epoch{};
    std::uint8_t slot{};
    bool banner_drawn{};
};

// Native host policy: a helper's first frame must reach the display before
// its recovered clock advances. This is separate from the original phase's
// always-true helper. The host observes a receipt only after successful GPU
// completion and a positive drawable presentation, on its owning thread.
class SupportDisplayGate {
public:
    void invalidate() noexcept {
        latched_=false;
        ++display_epoch_;
    }

    SupportFrameReceipt receipt(const SupportPresentationStatus& status,
                                bool banner_drawn) const noexcept {
        return {status.token,status.revision,display_epoch_,status.slot,banner_drawn};
    }

    bool observe(const SupportPresentationStatus& current,
                 const SupportFrameReceipt& frame) noexcept {
        if(current.complete || !frame.banner_drawn || frame.display_epoch!=display_epoch_ ||
           frame.token!=current.token || frame.slot!=current.slot || frame.revision!=current.revision)
            return false;
        token_=current.token;
        slot_=current.slot;
        latched_=true;
        return true;
    }

    bool can_advance(const SupportPresentationStatus& status) const noexcept {
        // Once shown, later animation revisions do not depend on GPU latency.
        return latched_ && !status.complete && status.token==token_ && status.slot==slot_;
    }

private:
    std::uint64_t display_epoch_{},token_{};
    std::uint8_t slot_{};
    bool latched_{};
};

} // namespace g1
