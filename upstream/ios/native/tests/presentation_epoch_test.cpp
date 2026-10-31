#include "g1/PresentationEpoch.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
std::size_t checks = 0;

void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

// A portable callback-order model, not UIKit or Metal execution. The host
// has two independent signals: successful GPU completion and a positively
// presented drawable. Either can arrive first or be repeated. Modal state
// is checked at final delivery as well as the recorded frame epoch.
struct QueuedFrame {
    std::uint64_t epoch{};
    bool gpu_completed{};
    bool drawable_presented{};
    bool failed{};
    bool delivered{};
};

struct DeliveryModel {
    g1::PresentationEpoch epochs;
    bool modal_open{};
    std::size_t acknowledgements{};
    std::size_t current_failures{};

    QueuedFrame frame() const { return {epochs.current()}; }

    void deliver(QueuedFrame& frame) {
        if (frame.delivered || frame.failed || !frame.gpu_completed || !frame.drawable_presented)
            return;
        frame.delivered = true;
        if (!modal_open && epochs.matches(frame.epoch)) ++acknowledgements;
    }

    void signal(QueuedFrame& frame, bool gpu) {
        if (gpu) frame.gpu_completed = true;
        else frame.drawable_presented = true;
        deliver(frame);
    }

    void fail(QueuedFrame& frame) {
        frame.failed = true;
        if (epochs.matches(frame.epoch)) ++current_failures;
    }

    void open_modal() { epochs.invalidate(); modal_open = true; }
    void close_modal() { epochs.invalidate(); modal_open = false; }
};

void callback_cases() {
    for (const bool gpu_first : {false, true}) {
        {
            DeliveryModel host;
            auto frame = host.frame();
            host.signal(frame, gpu_first);
            require(host.acknowledgements == 0, "one signal acknowledged a frame");
            host.signal(frame, !gpu_first);
            require(host.acknowledgements == 1, "current completed and presented frame was rejected");
            host.signal(frame, gpu_first);
            host.signal(frame, !gpu_first);
            require(host.acknowledgements == 1, "duplicate callbacks acknowledged twice");
        }
        {
            DeliveryModel host;
            auto before_modal = host.frame();
            host.signal(before_modal, gpu_first);
            host.open_modal();
            auto during_modal = host.frame();
            host.signal(during_modal, gpu_first);
            host.close_modal();
            host.signal(before_modal, !gpu_first);
            host.signal(during_modal, !gpu_first);
            require(host.acknowledgements == 0,
                    "late pre-modal or during-modal frame acknowledged after Resume");
            auto after_resume = host.frame();
            host.signal(after_resume, !gpu_first);
            host.signal(after_resume, gpu_first);
            require(host.acknowledgements == 1, "fresh post-Resume frame did not acknowledge");
        }
        {
            DeliveryModel host;
            host.open_modal();
            auto behind_modal = host.frame();
            host.signal(behind_modal, gpu_first);
            host.signal(behind_modal, !gpu_first);
            require(host.acknowledgements == 0, "frame behind an open modal acknowledged");
            host.close_modal();
            host.signal(behind_modal, gpu_first);
            host.signal(behind_modal, !gpu_first);
            require(host.acknowledgements == 0, "completed hidden frame acknowledged on repeated callbacks");
        }
        {
            DeliveryModel host;
            auto before_background = host.frame();
            host.signal(before_background, gpu_first);
            host.epochs.invalidate(); // App becomes inactive.
            host.epochs.invalidate(); // A different session is installed.
            host.open_modal();        // Startup/menu surface for that session.
            auto in_new_menu = host.frame();
            host.signal(in_new_menu, !gpu_first);
            host.close_modal();
            host.signal(before_background, !gpu_first);
            host.signal(in_new_menu, gpu_first);
            require(host.acknowledgements == 0, "old lifecycle/session receipt crossed current menu");
            auto current = host.frame();
            host.signal(current, gpu_first);
            host.signal(current, !gpu_first);
            require(host.acknowledgements == 1, "current session receipt was rejected");
        }
        {
            DeliveryModel host;
            auto failed = host.frame();
            host.signal(failed, gpu_first);
            host.fail(failed);
            host.signal(failed, !gpu_first);
            require(host.acknowledgements == 0, "failed GPU work acknowledged a current epoch");
            require(host.current_failures == 1, "current-epoch GPU failure was lost");
        }
        {
            DeliveryModel host;
            auto old = host.frame();
            host.signal(old, gpu_first);
            host.open_modal();
            host.close_modal();
            auto current = host.frame();
            host.fail(old);
            require(host.current_failures == 0, "stale GPU error failed a resumed presentation");
            host.signal(old, !gpu_first);
            host.signal(current, gpu_first);
            host.signal(current, !gpu_first);
            require(host.acknowledgements == 1, "stale GPU error prevented fresh completion");
        }
    }
}

void identity_cases() {
    g1::PresentationEpoch epoch;
    require(epoch.current() == 1, "default presentation epoch is not one");
    require(epoch.matches(epoch.current()), "current epoch does not match itself");
    require(!epoch.matches(0), "zero receipt epoch accepted");
    require(!epoch.matches(2), "future receipt epoch accepted");
    for (std::uint64_t value = 1; value != 17; ++value) {
        const auto previous = epoch.current();
        require(epoch.invalidate() == previous + 1, "invalidation is not strictly monotonic");
        require(!epoch.matches(previous), "prior receipt survived invalidation");
        require(epoch.matches(previous + 1), "new receipt rejected after invalidation");
        require(!epoch.matches(0), "zero receipt accepted after invalidation");
    }
    bool rejected_zero = false;
    try { g1::PresentationEpoch invalid(0); }
    catch (const std::invalid_argument&) { rejected_zero = true; }
    require(rejected_zero, "zero initial epoch was accepted");

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    g1::PresentationEpoch last(maximum - 1);
    require(last.invalidate() == maximum, "last valid epoch was skipped");
    require(last.matches(maximum), "last valid epoch does not match");
    bool overflow = false;
    try { last.invalidate(); }
    catch (const std::overflow_error&) { overflow = true; }
    require(overflow, "epoch exhaustion did not throw");
    require(last.current() == maximum, "epoch exhaustion wrapped identity");
    require(!last.matches(maximum), "receipt survived failed invalidation at exhaustion");
    require(!last.matches(0), "zero receipt accepted after exhaustion");
    require(!last.matches(1), "initial receipt accepted after exhaustion");
    overflow = false;
    try { last.invalidate(); }
    catch (const std::overflow_error&) { overflow = true; }
    require(overflow && !last.matches(maximum), "exhausted epoch recovered an old identity");
}
} // namespace

int main() {
    try {
        identity_cases();
        callback_cases();
        std::cout << checks << " presentation epoch and queued callback checks passed; "
                  << "portable model only, no UIKit or Metal execution\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
