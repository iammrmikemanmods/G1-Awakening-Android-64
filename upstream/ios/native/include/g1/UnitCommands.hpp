#pragma once

#include "g1/MovementAction.hpp"

namespace g1 {

enum class UnitCommand : std::uint16_t {
    move=0x001, attack=0x002, produce=0x004, transform=0x008, heal=0x010,
    capture_item=0x020, capture_site=0x040, deploy=0x080,
    // Unconditional marker after the original active/unacted guard. A visible
    // Wait control has NOT been established from this bit's consumer.
    availability_100=0x100, upgrade=0x200,
};

struct UnitCommandPredicates {
    bool move_has_vacant_destination{}; // Original50ec8, signed form budget.
    bool upgrade{};                    // 4ec0e
    bool produce{};                    // 50236, evaluated only if !upgrade
    bool attack{};                     // 52f84
    bool transform{};                  // 4a6c0
    bool heal{};                       // 4a73c
    bool capture_item{};               // 4b050
    bool capture_site{};               // 4b394
    bool deploy{};                     // 501f2
};

// Exact control/bit composition of5300e. Predicate calculations remain with
// their independently recovered gameplay modules; passing true is not itself
// authorization to execute an action. Null/inactive/acted return0. Move also
// requires spent==0; upgrade suppresses ordinary production, not deployment.
// Current owner, controller, moving, mission and UI gates remain callers'.
std::uint16_t unit_command_mask(const MovementUnitState*,
                               const UnitCommandPredicates&) noexcept;

bool has_unit_command(std::uint16_t mask, UnitCommand command) noexcept;

struct EndUnitActionResult {
    MovementUnitState movement;
    bool clear_selection{true};
    bool close_command_menu{true};
};

// Actual operation10 body5a254..5a27c: set flag2c=1 and clear selected3e4.
// No end-turn, RNG, combat, capture, funds or undo is synthesized. Its human
// touch-menu entry was NOT found in4633c's label dispatcher, so callers must
// independently establish reachability before presenting a new Wait control.
// Requires the common active/unacted command gate; input is copied.
EndUnitActionResult end_unit_action(const MovementUnitState&);

} // namespace g1
