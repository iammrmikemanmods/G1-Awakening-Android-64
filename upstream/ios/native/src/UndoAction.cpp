#include "g1/UndoAction.hpp"

#include <stdexcept>

namespace g1 {
namespace {
const Bytes& form_bytes(const UnitDefinition& definition, std::uint8_t form) {
    if (definition.forms.size()!=2 || definition.forms[0].size()!=14 ||
        definition.forms[1].size()!=14 || form>1)
        throw std::invalid_argument("Undo requires two decoded fourteen-byte forms");
    return definition.forms[form];
}

bool spent_differs_from_base(const MovementUnitState& unit, const Bytes& form) {
    const auto remaining=static_cast<std::uint8_t>(
        static_cast<unsigned>(form[0])-static_cast<unsigned>(unit.spent_budget));
    return remaining!=form[0];
}

void validate_pair(const MovementUnitState& movement, const CombatUnitState& combat,
                   const UnitDefinition& definition) {
    (void)form_bytes(definition,movement.form);
    if (combat.type!=definition.type || combat.form!=movement.form ||
        combat.active!=movement.active || combat.x!=movement.tile_x ||
        combat.y!=movement.tile_y || combat.flag_38!=movement.flag_38 ||
        combat.timer_30_ms!=movement.timer_ms)
        throw std::invalid_argument("Undo needs synchronized movement/combat state");
}
}

bool undo_available(const MovementUnitState& unit, const UnitDefinition& definition) {
    if (unit.flag_2c!=0) return false;
    const auto& form=form_bytes(definition,unit.form);
    if (spent_differs_from_base(unit,form) && unit.flag_3a==0) return true;
    // The original returns byte39 XOR1, not a normalized logical negation.
    return unit.flag_2d!=0 && (unit.flag_39 ^ 1U)!=0;
}

void lock_undo_on_friendly_selection_change(MovementUnitState& unit,
                                           const UnitDefinition& definition) {
    const auto& form=form_bytes(definition,unit.form);
    if (spent_differs_from_base(unit,form)) unit.flag_3a=1;
    if (unit.flag_2d!=0) unit.flag_39=1;
}

UndoResult undo_unit_action(const MovementUnitState& movement,
                           const CombatUnitState& combat,
                           const UnitDefinition& definition,
                           std::uint8_t pending, bool moving) {
    validate_pair(movement,combat,definition);
    if (!undo_available(movement,definition))
        throw std::invalid_argument("Original undo predicate rejects this unit");
    UndoResult result;
    result.movement=movement;
    result.combat=combat;
    result.animation_pending_2e=pending;
    result.moving_after=moving;
    result.restored_anchor=movement.flag_3a==0;
    result.reversed_transform=movement.flag_2d!=0;
    auto& state=result.movement;
    if (result.restored_anchor) {
        state.tile_x=state.anchor_x;
        state.tile_y=state.anchor_y;
        state.facing=state.anchor_facing;
        state.flag_2d=0;
        result.moving_after=false;
    }
    reset_movement_turn(state);
    if (!result.restored_anchor) {
        state.spent_budget=1;
        state.flag_3a=1;
    }
    if (result.reversed_transform) {
        const auto transformed=transform_unit(state,definition,combat.level,
                                              pending,false,false);
        state=transformed.unit;
        result.animation_pending_2e=transformed.animation_pending_2e;
        result.combat.stats=transformed.stats;
    }
    result.combat.form=state.form;
    result.combat.x=state.tile_x;
    result.combat.y=state.tile_y;
    result.combat.flag_38=state.flag_38;
    result.retain_selection=!spent_differs_from_base(state,form_bytes(definition,state.form)) &&
                            state.flag_2c==0;
    result.cursor={state.tile_x,state.tile_y};
    const auto attack_byte=form_bytes(definition,state.form)[3];
    result.range_kind=(attack_byte>0 && attack_byte<128)
        ? UndoRangeKind::signed_base_with_attack_envelope : UndoRangeKind::remaining_budget;
    return result;
}

} // namespace g1
