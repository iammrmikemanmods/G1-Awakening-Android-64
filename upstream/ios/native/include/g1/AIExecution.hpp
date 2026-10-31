#pragma once

#include "g1/AI.hpp"

namespace g1 {

enum class AICommandPhase : std::int32_t {
    idle=-1, production=0, move=2, attack=3, heal=4,
    capture_item=5, capture_site=6,
};

struct AIPlanPreparation {
    bool transform_to_movement_form{};
    bool transform_again{};
    // The initial transform always sets map28. The second same-tile capture
    // transform does not; the second same-tile attack transform does.
    bool second_transform_sets_attack_gate{};
    std::vector<AICommandPhase> phase_requests;
    bool upgrade_immediately{};
};

// Original57ed0 plan-consumer dispatch. Does not apply the requested transforms,
// world effects or presentation. Capture's same-tile scan preserves every
// ordered site/item match and signed coordinate comparison.
AIPlanPreparation prepare_ai_plan(const AIPlan&,const MovementUnitState&,
                                  const std::vector<WorldSite>&);

// Original58dcc's signed accumulated delay and readiness predicates. The two
// renderer readiness helpers in this binary literally return1. Camera state
// and the attack-only transform gate remain real inputs; production only
// checks its delay here. Native presented-frame feedback is an additional host
// policy supplied separately by GameSession, not an original helper result.
std::int32_t advance_ai_delay(std::int32_t elapsed,std::uint32_t delta_ms) noexcept;
bool ai_action_ready(AICommandPhase,std::int32_t elapsed,bool camera_at_target,
                     bool camera_moving,bool transform_pending) noexcept;

} // namespace g1
