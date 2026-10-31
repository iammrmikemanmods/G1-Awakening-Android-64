#include "g1/UnitSpawn.hpp"

namespace g1 {

UnitActivation activate_unit(const UnitDefinition& definition, const WorldUnit& identity,
    WorldCoordinate destination, std::uint8_t terrain0, std::uint8_t terrain2,
    const MovementUnitState& previous_movement,
    const CombatUnitState& previous_combat, UnitSpawnModifiers modifiers) {
    if (definition.type != identity.type || identity.type >= 23U ||
        identity.owner >= 2U || identity.faction >= 2U || definition.forms.size() != 2U ||
        definition.forms[0].size() != 14U || definition.forms[1].size() != 14U)
        throw FormatError("unit activation requires matching original type/owner and two decoded forms");

    UnitActivation result{identity, previous_movement, previous_combat,
                          unit_spawn_cost(definition), true};
    auto& motion = result.movement;
    motion.active = true;
    motion.form = 0;
    motion.tile_x = destination.x;
    motion.tile_y = destination.y;
    motion.offset_x = 0;
    motion.offset_y = 0;
    motion.facing = 1;
    motion.anchor_facing = 1;
    motion.timer_ms = -1;
    reset_movement_turn(motion);

    auto& combat = result.combat;
    combat.type = identity.type;
    combat.owner = identity.owner;
    combat.faction = identity.faction;
    combat.form = 0;
    combat.x = destination.x;
    combat.y = destination.y;
    combat.level = 0;
    combat.health = 100;
    combat.active = true;
    combat.damage_immune = false;
    combat.flag_38 = 0;
    combat.timer_30_ms = -1;
    combat.stats = combat_stats(definition, 0, 0);

    // Unlike ordinary transform eligibility, this tests form1 field6 !=0.
    // 0x43a54(record=false) changes form/stats, then0x430fc resets turn fields.
    if (definition.forms[1][6] != 0U && terrain0 == 99U && terrain2 == 0U) {
        motion.form = 1;
        combat.form = 1;
        combat.stats = combat_stats(definition, 1, 0);
        reset_movement_turn(motion);
    }
    if (modifiers.script_special_placement) {
        set_combat_health(combat, 9);
        motion.timer_ms = combat.timer_30_ms;
        combat.damage_immune = true;
    }
    if (modifiers.grant_damage_immunity) combat.damage_immune = true;

    auto& world = result.world;
    world.form = motion.form;
    world.x = destination.x;
    world.y = destination.y;
    world.spent_movement = 0;
    world.active = true;
    world.spawn = {combat.health, modifiers.script_special_placement, combat.damage_immune};
    return result;
}

} // namespace g1
