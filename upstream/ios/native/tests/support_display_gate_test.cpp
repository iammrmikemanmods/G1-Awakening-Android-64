#include "g1/SupportDisplayGate.hpp"

#include <iostream>
#include <stdexcept>

namespace {

std::size_t checks{};

void require(bool value,const char* message) {
    ++checks;
    if(!value)throw std::runtime_error(message);
}

g1::SupportPresentationStatus current(std::uint64_t token=41,std::uint8_t slot=0,
                                      std::uint64_t revision=0) {
    g1::SupportPresentationStatus status;
    status.token=token;
    status.revision=revision;
    status.helper_count=5;
    status.slot=slot;
    status.complete=false;
    status.helper_unit=std::size_t(slot)+2U;
    status.effect_complete=false;
    return status;
}

} // namespace

int main() {
    try {
        // Synthetic successful-frame receipts exercise the same production
        // predicate used by the Apple host; no UIKit/GPU test is claimed.
        g1::SupportDisplayGate gate;
        auto status=current();
        require(!gate.can_advance(status),"support advanced before its first frame");
        const auto first=gate.receipt(status,true);
        require(first.token==41 && first.slot==0 && first.revision==0 && first.banner_drawn,
                "support receipt lost its submitted identity");
        require(!gate.observe(status,gate.receipt(status,false)) && !gate.can_advance(status),
                "support advanced before its banner had drawn");

        for(unsigned field=0;field<4;++field) {
            auto wrong=first;
            switch(field) {
            case 0:++wrong.token;break;
            case 1:++wrong.slot;break;
            case 2:++wrong.revision;break;
            default:++wrong.display_epoch;break;
            }
            require(!gate.observe(status,wrong) && !gate.can_advance(status),
                    "mismatched asynchronous frame released support clock");
        }
        require(gate.observe(status,first) && gate.can_advance(status),
                "matching first frame did not release support clock");
        require(gate.observe(status,first) && gate.can_advance(status),
                "duplicate matching receipt changed the gate");
        status.revision=19;
        status.elapsed_ms=570;
        require(gate.can_advance(status),"later animation revision waited for another GPU callback");
        require(!gate.observe(status,first) && gate.can_advance(status),
                "late old revision revoked a previously presented helper");

        // A queued callback from before pause cannot release the resumed cue.
        const auto before_pause=gate.receipt(status,true);
        gate.invalidate();
        require(!gate.can_advance(status),"pause retained a display latch");
        require(!gate.observe(status,before_pause) && !gate.can_advance(status),
                "pre-pause callback released the resumed support clock");
        const auto resumed=gate.receipt(status,true);
        require(resumed.display_epoch==before_pause.display_epoch+1 &&
                gate.observe(status,resumed) && gate.can_advance(status),
                "fresh resumed frame did not release unchanged support progress");

        // Changing helper requires a new first frame even before explicit
        // invalidation. Reordered old-helper callbacks cannot select it.
        const auto previous_helper=gate.receipt(status,true);
        status.slot=1;
        ++status.revision;
        status.elapsed_ms=0;
        require(!gate.can_advance(status),"new helper inherited old helper's first frame");
        require(!gate.observe(status,previous_helper) && !gate.can_advance(status),
                "old helper callback released the next helper");
        gate.invalidate();
        require(gate.observe(status,gate.receipt(status,true)) && gate.can_advance(status),
                "next helper's fresh frame did not release it");

        // A new session may reuse the same gameplay token and slot. Its host
        // invalidation must distinguish these otherwise-identical receipts.
        const auto old_session=gate.receipt(status,true);
        gate.invalidate();
        require(!gate.observe(status,old_session) && !gate.can_advance(status),
                "old session frame released a reused battle token");
        require(gate.observe(status,gate.receipt(status,true)),"new session frame was not accepted");
        const auto old_battle=gate.receipt(status,true);
        ++status.token;
        require(!gate.can_advance(status) && !gate.observe(status,old_battle),
                "different battle token reused prior display proof");
        require(gate.observe(status,gate.receipt(status,true)),"new battle frame was rejected");

        status.complete=true;
        const auto completed=gate.receipt(status,true);
        require(!gate.can_advance(status) && !gate.observe(status,completed),
                "completed support phase remained eligible to advance");
        gate.invalidate();
        require(!gate.can_advance(status) && !gate.observe(status,completed),
                "final queued frame restarted a completed phase");

        // Completion does not admit a zero-helper sentinel, including at
        // token zero; identity is not inferred from a nonzero-token sentinel.
        g1::SupportPresentationStatus empty;
        require(!gate.observe(empty,gate.receipt(empty,true)) && !gate.can_advance(empty),
                "empty support bypass acquired an active clock");
        status=current(0);
        require(gate.observe(status,gate.receipt(status,true)) && gate.can_advance(status),
                "gate unexpectedly used token zero as its private state");

        std::cout<<checks<<" native support display-gate checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"support display gate: "<<error.what()<<'\n';
        return 1;
    }
}
