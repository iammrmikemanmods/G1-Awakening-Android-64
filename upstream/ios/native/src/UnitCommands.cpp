#include "g1/UnitCommands.hpp"

#include <stdexcept>

namespace g1 {

std::uint16_t unit_command_mask(const MovementUnitState* unit,
                               const UnitCommandPredicates& predicates) noexcept {
    if (!unit || !unit->active || unit->flag_2c!=0) return 0;
    std::uint16_t result=static_cast<std::uint16_t>(UnitCommand::availability_100);
    const auto add=[&result](UnitCommand command) {
        result=static_cast<std::uint16_t>(result|static_cast<std::uint16_t>(command));
    };
    if (unit->spent_budget==0 && predicates.move_has_vacant_destination)
        add(UnitCommand::move);
    if (predicates.upgrade) add(UnitCommand::upgrade);
    else if (predicates.produce) add(UnitCommand::produce);
    if (predicates.attack) add(UnitCommand::attack);
    if (predicates.transform) add(UnitCommand::transform);
    if (predicates.heal) add(UnitCommand::heal);
    if (predicates.capture_item) add(UnitCommand::capture_item);
    if (predicates.capture_site) add(UnitCommand::capture_site);
    if (predicates.deploy) add(UnitCommand::deploy);
    return result;
}

bool has_unit_command(std::uint16_t mask,UnitCommand command) noexcept {
    return (mask&static_cast<std::uint16_t>(command))!=0;
}

EndUnitActionResult end_unit_action(const MovementUnitState& unit) {
    if (!unit.active || unit.flag_2c!=0)
        throw std::invalid_argument("Unit action needs the original active/unacted gate");
    EndUnitActionResult result;
    result.movement=unit;
    result.movement.flag_2c=1;
    return result;
}

} // namespace g1
