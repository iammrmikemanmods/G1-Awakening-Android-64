# Native Undo and unit command boundaries

The new `UndoAction` module reconstructs the original unit Undo decision,
mutation and selection-lock rules. `UnitCommands` separately preserves the
original command bit composition and the operation10 unit-action transition.
Neither module executes the old executable. The development reference runs
unchanged original ARM instructions using synthetic states, then compares the
results with the independently compiled C++ implementation.

## Verification and scope

The strict C++17 direct-source build passes `-Wall -Wextra -Wconversion -Werror
-pedantic`. The frozen fixture contains **1,100 cases**, with **11,436 matching
scalar outputs** and **17 native rejection/shape checks**:

| Slice | Cases | Original code executed |
|---|---:|---|
| Undo availability | 220 | Complete `0x50d42`, including real remaining/base helpers |
| Undo mutation and continuation | 192 | `0x5a2ce..0x5a4b2`, including real reset, reverse-transform and stat helpers |
| Previous-selection locks | 96 | `0x54dcc..0x54e14`, including real remaining/base helpers |
| End-unit-action body | 32 | `0x5a254..0x5a4b2` |
| Command mask composition | 560 | Complete `0x5300e` with nine explicitly supplied predicate-return inputs |

The last row verifies the original common gates, bit arithmetic, priority and
short-circuit call order. It **does not execute or revalidate the nine underlying
world predicates**; those remain the responsibility of the existing movement,
upgrade, production, combat, transform, heal and capture modules. The 540 other
cases execute the relevant original unit decisions and writes. This distinction
is recorded in `undo_validation/original_unit_commands_reference.json`.

Every case checks the original executable text remains unchanged and that unit
writes stay inside the recovered offset whitelist. Undo's renderer, range,
cursor, audio and menu calls are observed as requests at explicit boundaries.
They are not claimed to have been drawn, heard or accepted by a host. There is
no iOS/device or complete-campaign assertion from this test.

The source image is the audited iOS1.1.3 ARMv6 executable, SHA256
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.
The fixture contains synthetic state and numeric results; it contains no
executable or original resource payload.

## Undo availability

`0x50d42` returns false for no selection or action-used byte `+0x2c != 0`.
For a selected, unacted unit it returns true when either:

1. The original byte-wrapped remaining budget differs from the selected form's
   base, and `+0x3a == 0`.
2. `+0x2d != 0` and the result of `byte(+0x39) XOR 1` is nonzero.

The XOR is retained, rather than replacing it with logical negation. Original
flag values2 and255 are included in the tests. Base0/1/3/127/128/255, spent
0/1/127/128/255 and byte subtraction wrap are covered.

This helper does not itself check owner, active state or motion. The original
caller authorizes its command phase. Native `undo_unit_action` rechecks the
predicate and requires synchronized movement/combat inputs, while the session
must separately authorize current player, active/stationary state, mission and
presentation gates.

## Exact writes

The dispatcher saves the original `+0x2d` before changing any action flags.

| Condition | Original sequence | Native result |
|---|---|---|
| `+0x3a == 0` | `0x4324a`, then `0x430fc` | Restore anchor tile/facing, stop motion, reset action flags and spent budget |
| `+0x3a != 0` | `0x430fc`, then byte writes at `0x5a30c/0x5a314` | Keep tile/facing/motion, reset action flags, reinstate spent1 and flag3a1 |
| Saved `+0x2d != 0` | `0x43a54(unit,false)` | Reverse form and recompute stats at the unchanged level |

`0x430fc` clears spent and bytes2c/2d/2f/38/39/3a and records the current tile as
the new anchor. It does not change anchor facing, offsets, timer, health or form.
Reverse transformation with `record=false` leaves animation byte2e unchanged.
It does not emit an ordinary transformation sound or set a new transformation
animation request. HP, immunity, support, preset34 and all unrelated fields stay
unchanged in the original vectors, including nonzero offsets and timer extremes.

After mutation the original requests cursor placement at the resulting unit
tile, refreshes the range, requests selection mode2, clears pending-move bytee0,
and requests audio operation5/argument17. The range helper is `0x50ec8` if the
new form's **signed byte3 is positive**, otherwise `0x50844`. The returned
`UndoRangeKind` preserves that choice; it does not fabricate an attack envelope.
The later `0x5a3c2` continuation retains selected3e4 only if remaining equals
base and flag2c is zero. Thus the locked3a branch, which keeps spent1, clears
selection. Repeated requests to the original UI setup are not a promise that
the native host has performed them; the session owns its corresponding refresh.

This action does not roll back combat, captured sites, currency, objectives,
production or campaign state. It only translates the proven unit transition.

## Selection locks are part of Undo's lifecycle

In `0x54d10`, the new unit's owner is compared with the current player at
`0x54d5e..0x54d66`. Only the friendly branch reaches `0x54dcc`. When the old
selection exists and differs from the new one:

- If remaining differs from base, `0x54df6` writes old flag3a1.
- If old flag2d is nonzero, `0x54e12` writes old flag391.

The native session must call `lock_undo_on_friendly_selection_change` only for
that transition. Clearing selection, reselecting the same unit and the original
enemy-selection branch do not execute these writes. The helper does not require
the old unit to be active and does not mark it acted.

## Command mask and the separate end-action body

`0x5300e` rejects null, inactive or acted units. The remaining bits are:

| Bit | Exact source predicate |
|---:|---|
| `0x001` | Spent byte59 is zero and `0x50ec8` finds a vacant reachable destination |
| `0x002` | Attack predicate `0x52f84` |
| `0x004` | Ordinary production `0x50236`, only if upgrade failed |
| `0x008` | Transformation `0x4a6c0` |
| `0x010` | Healing `0x4a73c` |
| `0x020` | Capture-item coordinate/ownership predicate `0x4b050` |
| `0x040` | Capture-site coordinate/ownership predicate `0x4b394` |
| `0x080` | Deployment `0x501f2` |
| `0x100` | Unconditional marker after the common active/unacted gate |
| `0x200` | Upgrade `0x4ec0e`; suppresses bit4, not bit128 |

The human move entry and destination checks remain distinct. The common Move
command needs an unoccupied reachable cell. Once phase2 is already admitted,
touch `0x58bec..0x58c0c` checks the chosen range cell is nonnegative and not127;
`0x54ebe` then checks carrier/occupant compatibility and a real first route word.
It does not repeat the global vacancy check at destination execution. Carrier
occupancy does not by itself satisfy the initial Move-command vacancy gate.

The switch in `0x59e46` has an operation10 body at `0x5a254`: close menu, call
`0x430f4` to set selected flag2c1, and clear selected3e4. This is implemented as
`end_unit_action`, subject to the common active/unacted gate. It does not call
end-turn `0x55d50`. However the inspected touch-menu label dispatcher
`0x4633c..0x4675a` has no operation10 branch, and the side-button setup does not
consume bit0x100. **Human touch reachability is unresolved.** The enum therefore
names the bit `availability_100`; this report does not authorize a new visible
Wait button based on that bit alone.

## Frame timer finding supplied to the session owner

`0x59a7e` visits owner0 then owner1, in packed owner-slot order. At
`0x59ab2..0x59ab6`, **every active unit** is passed to `0x4351c`, including
stationary units. Both moving and stationary paths join at `0x43646`: timer30
is untouched only when it is -1, otherwise delta is subtracted and a negative
signed result is clamped to0. The motion action already includes that timer
update; the session must not tick the moving actor twice.

Inactive units do not use timer30 here. They advance separate disappearance
counters24/28: elapsed24 must exceed159 to reset and increment28; after28>6,
elapsed24 becomes-1. After all unit updates, map phase2, a stationary actor and
map28==-1 admit arrival `0x59798` at `0x59b2a`. This static source finding does not
authorize ticking while a native presentation gate intentionally pauses the
simulation. Integration and its session regression belong to the session owner.

## Reproduction

Repository development scripts are under `analysis_tools/undo/`; their README
lists explicit local original-image, reference-support and Unicorn inputs.
No Apple SDK, archive, original libgcc or game runtime is needed for this bounded
reference. Imported signed division is supplied as exact truncating arithmetic.

CMake integration needs `src/UndoAction.cpp`, `src/UnitCommands.cpp`, and a
`g1_unit_commands_tests` target using `tests/unit_commands_test.cpp` with
`tests/unit_commands_original_vectors.txt` as its only runtime argument.
