#include "g1/HealAction.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace g1 {
namespace {

void validate_unit(const CombatUnitState& unit) {
    if (unit.type>=23 || unit.owner>1 || unit.form>1)
        throw std::invalid_argument("Heal unit state is outside the recovered variant");
}
void validate_actor(const CombatUnitState& actor,const MovementUnitState& motion,
                    const UnitDefinition& definition) {
    validate_unit(actor);
    if (motion.active!=actor.active || motion.form!=actor.form ||
        motion.tile_x!=actor.x || motion.tile_y!=actor.y || definition.type!=actor.type ||
        definition.forms.size()!=2 || definition.forms[0].size()!=14 || definition.forms[1].size()!=14)
        throw std::invalid_argument("Heal actor definition/state is malformed or unsynchronized");
}
bool friendly_neighbor(const CombatUnitState& actor,const CombatUnitState& target) {
    return target.active && target.owner==actor.owner &&
           std::abs(int(actor.x)-target.x)+std::abs(int(actor.y)-target.y)==1;
}

} // namespace

HealEligibility heal_eligibility(const CombatUnitState& actor,const MovementUnitState& motion,
                                 const UnitDefinition& definition) {
    validate_actor(actor,motion,definition);
    if (!actor.active) return HealEligibility::inactive;
    if (motion.flag_2c!=0) return HealEligibility::action_used;
    if (definition.forms[actor.form][7]!=1) return HealEligibility::form_cannot_heal;
    return HealEligibility::allowed;
}

std::vector<std::size_t> healing_targets(const std::vector<CombatUnitState>& units,
                                       std::size_t actor) {
    if (units.size()>23 || actor>=units.size())
        throw std::invalid_argument("Heal roster/actor is invalid");
    std::array<bool,23> seen{};
    for (const auto& unit:units) {
        validate_unit(unit);
        if (seen[unit.type]) throw std::invalid_argument("Heal roster has duplicate types");
        seen[unit.type]=true;
    }
    std::vector<std::size_t> result;
    for (std::size_t i=0;i<units.size();++i)
        if (i!=actor && friendly_neighbor(units[actor],units[i])) result.push_back(i);
    std::sort(result.begin(),result.end(),[&](auto a,auto b) {return units[a].type<units[b].type;});
    result.push_back(actor);
    return result;
}

HealResult apply_heal(const CombatUnitState& actor,const MovementUnitState& motion,
                      const UnitDefinition& definition,const CombatUnitState& target,
                      std::int32_t funds) {
    if (heal_eligibility(actor,motion,definition)!=HealEligibility::allowed)
        throw std::invalid_argument("Heal action is not eligible");
    validate_unit(target);
    const bool self=target.type==actor.type;
    if ((self && (target.owner!=actor.owner || target.x!=actor.x || target.y!=actor.y || !target.active)) ||
        (!self && !friendly_neighbor(actor,target)))
        throw std::invalid_argument("Heal target is not the actor or an active friendly orthogonal neighbor");
    HealResult result;
    result.target=target;result.actor_movement=motion;result.funds_after=funds;
    const int missing=100-int(target.health);
    if (missing<=0) {
        result.message_id=missing==0 ? 0x21ff04c4U : 0x21ff01e2U;
        return result;
    }
    if (funds<=0) {
        result.outcome=HealOutcome::insufficient_funds;
        result.message_id=0x21ff01e2U;
        return result;
    }
    // Original loop stops only after the next10HP exceeds the missing amount,
    // or after the number of increments equals all available funds.
    result.spent=std::min<std::int32_t>(funds,missing/10+1);
    result.requested_health=result.spent*10;
    const auto wrapped=static_cast<std::uint8_t>(int(target.health)+result.requested_health);
    result.target.health=std::min<std::uint8_t>(wrapped,100);
    result.target.timer_30_ms=-1; //0x431be does not rewrite preset34 or immunity.
    result.funds_after=funds-result.spent;
    result.actor_movement.flag_2c=1;
    result.outcome=HealOutcome::healed;
    result.audio_event_in_group5=15;
    return result;
}

} // namespace g1
