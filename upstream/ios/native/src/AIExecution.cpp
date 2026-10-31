#include "g1/AIExecution.hpp"

namespace g1 {
namespace {
bool matches(WorldCoordinate a,WorldCoordinate b) noexcept {
    const auto sx=a.x<128U ? int(a.x) : int(a.x)-256;
    const auto sy=a.y<128U ? int(a.y) : int(a.y)-256;
    return sx==b.x && sy==b.y;
}
}

AIPlanPreparation prepare_ai_plan(const AIPlan& plan,const MovementUnitState& unit,
                                  const std::vector<WorldSite>& sites) {
    if (plan.action>4 || plan.movement_form>1 || unit.form>1)
        throw FormatError("invalid native AI plan discriminator/form");
    AIPlanPreparation result;
    result.transform_to_movement_form=plan.movement_form!=unit.form;
    const bool same=plan.destination.x==unit.tile_x && plan.destination.y==unit.tile_y;
    if (plan.action==0 || !same) {
        result.phase_requests.push_back(AICommandPhase::move);
        return result;
    }
    if (plan.action==1) {
        result.transform_again=plan.transform_on_arrival;
        result.second_transform_sets_attack_gate=result.transform_again;
        result.phase_requests.push_back(AICommandPhase::attack);
    } else if (plan.action==2) {
        // The initial transition has already selected movement_form. Original
        //57ff2 checks it again before its secondary capture transform.
        result.transform_again=plan.transform_on_arrival;
        for (const auto& site:sites) {
            if (matches(site.position,plan.destination))
                result.phase_requests.push_back(AICommandPhase::capture_site);
            for (const auto& group:site.coordinate_groups)
                for (auto item:group)
                    if (matches(item,plan.destination))
                        result.phase_requests.push_back(AICommandPhase::capture_item);
        }
    } else if (plan.action==3) {
        result.phase_requests.push_back(AICommandPhase::heal);
    } else {
        result.upgrade_immediately=true;
    }
    return result;
}

std::int32_t advance_ai_delay(std::int32_t elapsed,std::uint32_t delta_ms) noexcept {
    const auto word=static_cast<std::uint32_t>(elapsed)+delta_ms;
    return word<=0x7fffffffU ? static_cast<std::int32_t>(word) :
        static_cast<std::int32_t>(static_cast<std::int64_t>(word)-0x100000000LL);
}

bool ai_action_ready(AICommandPhase phase,std::int32_t elapsed,bool camera_at_target,
                     bool camera_moving,bool transform_pending) noexcept {
    if (elapsed<100) return false;
    if (phase==AICommandPhase::production) return true;
    if (phase==AICommandPhase::idle || !camera_at_target || camera_moving) return false;
    return phase!=AICommandPhase::attack || !transform_pending;
}
} // namespace g1
