#pragma once

#include "g1/TransformAction.hpp"

namespace g1 {

// Original0x50d42, interpreted as a nonzero availability result. It checks
// action-used, remaining-vs-base and flags2d/39/3a. It does NOT independently
// authorize ownership, active state, stationary phase or native UI input.
bool undo_available(const MovementUnitState&, const UnitDefinition&);

// Original friendly-selection branch0x54dcc..0x54e14. Call only when changing
// from this existing selection to a DIFFERENT current-player unit. Clearing
// selection, selecting the same unit, or the original enemy-selection branch
// do not perform these writes. No action, spent budget or world state is reset.
void lock_undo_on_friendly_selection_change(MovementUnitState&,
                                           const UnitDefinition&);

enum class UndoRangeKind { signed_base_with_attack_envelope, remaining_budget };

struct UndoResult {
    MovementUnitState movement;
    CombatUnitState combat;
    std::uint8_t animation_pending_2e{};
    bool moving_after{};
    bool restored_anchor{};
    bool reversed_transform{};
    // Original continuation5a3c2 keeps selected3e4 only when remaining==base
    // and flag2c==0. The special3a branch leaves spent=1 and clears selection.
    bool retain_selection{};
    WorldCoordinate cursor;
    UndoRangeKind range_kind{UndoRangeKind::remaining_budget};
    std::int32_t requested_selection_mode{2}; // Original5494c, before UI reset.
    bool pending_move_after{}; // map+e0=0 at5a3b2.
    std::int32_t audio_operation{5};
    std::int32_t audio_argument{17};
};

// Pure copy of original5a2ce..5a330 and its result-dependent requests. With
// flag3a==0 it restores anchor position/facing and stops motion, then430fc
// resets actions. With flag3a!=0 it keeps position/facing/motion, resets actions,
// and reinstates spent=1 and3a=1. Prior2d reverses form through43a54(record=false).
// HP, immunity, funds, ownership, capture, objectives, offsets and animation2e
// are not reversed. The caller owns the stationary/current-player command
// gate, renderer/range refresh, audio, and returned selection policy. This is
// not a whole-world transaction rollback. Invalid/ineligible inputs throw
// before changing caller-owned state. moving describes original unit byte8==1.
UndoResult undo_unit_action(const MovementUnitState&, const CombatUnitState&,
                           const UnitDefinition&, std::uint8_t animation_pending_2e,
                           bool moving);

} // namespace g1
