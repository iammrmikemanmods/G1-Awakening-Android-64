# Native production, tape activation and boarding

`UnitInteractions.hpp/.cpp` reconstructs bounded original gameplay branches as
portable C++17. The product does not open or execute the original executable.
The local reference runner executes the audited original only to produce test
vectors. Production, placement and transport still require the session's
current-player, mission and presentation authorization before a UI exposes them.

## Integration contract

`UnitInteractionView` borrows the complete 23 type definitions, the current
combat/movement roster in stable session order, and the full packed owner type
slots. An inactive type can be absent from the materialized native roster and
still appear in a production list. Active objects, forms, coordinates and the
movement occupancy board must agree; mismatches are rejected before mutation.

`production_candidates` keeps ascending original owner-slot order. Ordinary
production includes available inactive types for which **neither** form has
byte8 equal to1. Tape activation includes available inactive types for which
**either** form has byte8 equal to1. Funds do not filter either list.

`production_eligibility` includes the common active/acted guard, selected form
byte12 equal to1 for ordinary production or byte9 equal to1 for tape activation,
available candidates, and the original vacant destination predicate. Ordinary
production also respects the command mask's earlier upgrade priority. It uses
the corrected effective cost through `UpgradeAction`, including type21's actual
loader override to40; it does not use an unmodified raw scalar for that type.

`production_placement` reconstructs the original full-fill call with budget1
and cost mode0. Any raw terrain cost other than99 costs one step. Site points,
special regions and normal movement costs do not apply. A raw99 origin is
seeded only when selected form signed byte6 is positive. The direction order
for the human cursor is north, south, west, east. The first reachable direction
is selected even when occupied; the later placement confirmation checks vacancy.
The range's vacancy boolean also preserves `0x49b5a`'s clamped-neighbor gate and
subsequent whole-field scan, including an unoccupied origin in synthetic state.

`produce_unit` returns the exact existing `UnitActivation` plus the producer's
updated movement state, funds after subtraction/clamp, audio request `(5,16)`,
return cursor and cleared selection flags. The session must install the
activation and its explicit active-count increment, retain stable type indices,
update its spawn generation, emit the actual presentation/audio requests, and
clear the pending production selection. Both newly activated unit and producer
are marked acted. This human wrapper must not replace the separate AI wrapper,
which marks only the newly activated unit.

Fresh unmaterialized objects use preset timer34 equal to0, as written by the
original base constructor. Existing inactive objects retain their prior preset,
support modifier and fields that activation does not write. Activation resets
health/form/actions through the existing validated `UnitSpawn` implementation.

## Two original confirmation paths

The source has distinct button and type-list confirmation routes. The public
API requires `ProductionConfirmation`; there is no implicit default.

| Path | Evidence | Funds behavior |
| --- | --- | --- |
| `funds_checked_button` | `0x555b0..0x555d4` loads owner funds and signed type cost, then uses signed less-than | Rejects funds below effective cost before placement |
| `direct_type_list` | Central menu dispatch `0x35ccc..0x35cfe` calls `0x461d2`; selected-name branch `0x462aa..0x462de` stores type and requests operation1 or6 | That callback contains no funds comparison |
| Later placement confirmation | Touch `0x58c92..0x58ccc`, button `0x59564..0x595b8` | Checks phase/camera gate, reachable cell and vacancy, then calls `0x55f8c`; no additional funds check |
| Common commit | `0x55f8c ->0x54016` | Subtracts the signed effective cost using original32-bit arithmetic, then clamps a negative result to0 |

The callback is reachable from the actual unit-list menu objects at
`app+0x26c[0/1]`. `0x45efa` sets their per-entry flags from the candidate list
without reading funds. The callback compares its title against tagged text
`0x21ff041f`, localization ID1055 (`A C T I V A T E   T A P E`). Equality uses
operation6, which enters placement mode1; otherwise it uses operation1 and
mode0. These source facts are retained explicitly rather than inventing one
shared affordability rule. There has been no new interactive device test of
the original production menu; the proof here is original-code execution of
the decision/commit slices plus static call-chain evidence.

The native type-list UI should deliberately select `direct_type_list` when
implementing that route. A UI implementing the original input-mask/button
confirmation should select `funds_checked_button`. Candidate availability and
board vacancy remain mandatory in both native entry points. Neither mode
waives the actual common funds debit.

## Friendly destination and arrival

`friendly_destination_admission` reconstructs `0x54ebe`. It rejects an actor
already in moving mode1; selecting that actor's occupied tile requests its
commands. A different active occupant must share the owner, have nonzero
selected form byte9, and the actor must have nonzero selected form byte8.
Empty or admitted friendly destinations still require a real prepared route.
The occupancy lookup is owner0 then owner1, ascending slots, and includes only
active units with both motion offsets zero. The caller supplies an already
authorized active actor and an actual route status.

`apply_transport_arrival` implements `0x59884..0x5994a`, after the separately
recovered facing prefix. It checks active same-owner carriers in slot order
whose selected form byte9 is nonzero and whose tile equals the arriving actor's
tile. This loop does not check carrier offsets. On every match it deactivates
the actor, cycles the upgrade slot through the existing original helper,
requests hiding that type, clears its original status cache/string, and
decrements the owner's active-count byte. The original has no loop break;
synthetic multiple carriers repeat the count/notification effects. Native
results retain that multiplicity rather than replacing it with one decrement.

Boarding changes only the actor's active flag in its unit object. It preserves
HP, form, coordinates, facing, action flags, movement spent, pending fields and
timers. It does not mutate the carrier or attach a new cargo record. The result
exposes repeated carrier types, count and slot changes, and explicit cache and
renderer notifications. The session owns the subsequent phase/UI reset and
must not treat mere co-location as destruction or increment a death metric.

## Original static links

`derive_unit_static_links` implements the post-allocation loop
`0x5417a..0x54238`. For each same-owner type whose either-form byte9 equals1,
the loop assigns each special type's field10 to that carrier; the carrier's
field10 gets the first special type and field14 gets the last remaining one.
The exact write order also handles synthetic dual-role/self-link definitions.

With the actual source definitions and factions `[0,1]`, the resulting links
are type5→(6,7), type6→5, type7→5, type9→(19,20), type19→9 and type20→9. Other
types have null links. Neither the inspected activation nor boarding routine
writes these fields. They can be derived from immutable definitions/owner
slots when creating or restoring a session; no mutable cargo state is added
by this module. The broader uses of these raw link fields are not renamed or
inferred.

## Verification and limits

The frozen reference JSON under `unit_interactions_validation/` and fixture
`native/tests/unit_interactions_original_vectors.txt` record exact counts,
input/output hashes and boundaries. The native test
compares original outputs for links, candidate lists, placement, move admission,
boarding, full human commits, command priority and funds comparison. Eight
additional callback-dispatch cases are reference-only evidence; no legacy UI
dispatcher is added to the native product merely to test that mapping.
The final gate passed **869 native/original cases, 20,601 scalar comparisons
and 14 native guards**. Including callback evidence, the original runner
executed877 cases. The final native executable was compiled directly from the
current seven required source files, rather than relying on an older core
library. Exact compiler flags and dependency hashes are in `native_checks.json`.

The original image SHA256 is
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.
Its code mapping is read-only and checked unchanged after every case. String,
renderer and audio boundaries are recorded rather than treated as drawn or
played. Exact imported signed division is supplied as arithmetic. The original
type21 override at `0x5a8ce..0x5a8e0` executes before the reference cost checks.

This report establishes the bounded native components. It does not establish
integrated menu rendering, Apple compilation of this new module, device input,
complete campaign play, or transport-aware Undo. Those are separate session,
UI and validation work.
