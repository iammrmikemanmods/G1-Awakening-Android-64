# Native command admission boundaries

These are original gameplay checks separate from verified range, route, motion
and arrival-facing code. The session must own current-player selection and the
stationary command phase. This report does not authorize AI or transport through
the ordinary human adapter.

## Movement

Command mask routine `0x5300e` rejects a null/inactive unit and nonzero action-used
flag `0x2c`. Move bit one additionally requires spent movement `0x59 == 0` and
an unoccupied reachable destination from `0x50ec8`. A spent budget blocks a
second move even when a new form's total budget would be larger.

`0x50ec8 -> 0x50d96` begins with signed selected-form stat byte 0, clears the
range grid and uses recovered flood fill `0x4a29c`. It decorates unreachable cells
with marker 127 when selected-form minimum/maximum attack ranges are equal:
`0x49d58` marks the exact Manhattan-distance shell around each reachable cell,
without overwriting a valid move cell. The final `0x49b5a` excludes the origin,
occupied cells and marker 127. Existing native MovementRange therefore provides
the same usable ordinary move cells for the original small positive budgets;
the red attack envelope is a separate presentation layer. Do not reinterpret
negative signed budgets as large unsigned values outside that data scope.

Destination execution `0x54ebe` rejects motion mode one. Choosing the same unit
opens its command UI. An active enemy occupant rejects. A different friendly
occupant is allowed only when target selected-form byte 9 and actor selected-
form byte 8 are both nonzero. That path leads to later transport/co-location
and actor deactivation, which ordinary native movement does not implement.
An empty destination requires a nonempty ready route before pending-move and
renderer-follow setup.

For the initial supported path, require an active current-player actor,
`flag_2c == 0`, `spent_budget == 0`, stationary phase, ready route and empty
destination. Report friendly destination transport as unsupported. Friendly
cells may still lie along the route under recovered movement-cost rules;
destination admission is separate from traversal.

Each MovementAction call is one original simulation update, not one display
refresh. First-mission observation recorded 600 consecutive approximately 30ms
timer requests with one-shot rearm after callback work. This proves request
delay, not exact delivered 33.333Hz or device performance. A separate 30ms
simulation step plus display interpolation avoids doubling movement speed at
60Hz. See TIMER_RUNTIME_OBSERVATION, MOVEMENT_ACTION and MOVEMENT_ARRIVAL reports.

## Capture, transformation and undo

Capture command bits `0x20`/`0x40` depend on different ownership at matching item/
site coordinates. The invoked command separately requires selected-form byte 10
equal to one. Success marks action-used flag `0x2c`, writes real ownership and
derived links, and must precede objective observation. It does not set a guessed
mission-success Boolean. See CAPTURE_ACTION.

Transform bit `0x8` requires normalized per-type scalar 2, selected-form byte 8
not one, the recovered terrain exception and flag `0x2d == 0`, plus the common
active/action gate. It preserves spent movement and does not mark action-used.
See TRANSFORM_ACTION.

Movement's unit-only undo helper does not restore ownership, combat, currency,
script effects or rendering. The session must authorize ordinary undo and not
expose it as a general reversal after capture or another world-changing action.
Transform-aware whole-action undo and transport are separate boundaries.
