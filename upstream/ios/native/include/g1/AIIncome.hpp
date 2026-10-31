#pragma once

#include "g1/CaptureAction.hpp"

namespace g1 {

// Original site4b56c / item4b452 potential. The baseline numbers are explicit
// call arguments: do not silently recompute them, since current counters can
// predate a capture. The hypothetical ownership/income calculation never
// changes the actual network, its link bytes, or the caller's funds.
std::int32_t ai_capture_income_potential(const CaptureNetwork&,
    std::array<std::int8_t,2> player_factions, std::uint8_t owner,
    const CaptureTarget&, std::int32_t own_income_before,
    std::int32_t other_income_before);

} // namespace g1
