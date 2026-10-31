#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace g1 {

// Process-local identity for asynchronous presentation receipts. Snapshot
// current() when preparing a frame; invalidate() whenever a modal/lifecycle
// boundary hides that presentation or exposes it again. A matching epoch is
// necessary, but GPU success, actual presentation, token/revision identity
// and the current visible UI state remain separate host requirements.
class PresentationEpoch {
public:
    // Native hosts use the default. An explicit initial value permits bounded
    // lifetime/overflow tests; epochs are not a persisted save-game identity.
    explicit PresentationEpoch(std::uint64_t initial = 1) : current_(initial) {
        if (initial == 0)
            throw std::invalid_argument("presentation epoch zero is reserved");
    }

    std::uint64_t current() const noexcept { return current_; }

    bool matches(std::uint64_t receipt_epoch) const noexcept {
        return !exhausted_ && receipt_epoch != 0 && receipt_epoch == current_;
    }

    std::uint64_t invalidate() {
        if (exhausted_ || current_ == std::numeric_limits<std::uint64_t>::max()) {
            // Fail closed: catching this exception must not make a receipt
            // from before the failed invalidation eligible again.
            exhausted_ = true;
            throw std::overflow_error("presentation epoch space exhausted");
        }
        return ++current_;
    }

private:
    std::uint64_t current_;
    bool exhausted_{};
};

} // namespace g1
