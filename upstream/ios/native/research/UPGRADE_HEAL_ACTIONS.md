# Native upgrade, repair and owner-slot selection

`UpgradeAction` and `HealAction` reconstruct original action admission, unit
state and funds transitions in C++17. They operate on current native data and
return copied results. No original executable, guest CPU, original resource
payload or compatibility runtime enters the product. The original-function
oracle is development-only evidence, not a playable-game or device test.

## Upgrade

Original `0x4ec0e` permits an upgrade only when the unit is active, its action
flag `0x2c` is zero, form-zero field 12 does not equal 1, stored level is at most
24, and the actor owner's signed funds are at least its effective signed cost
from map table `+0x14c`. The loader copies scalar1 except that type21 is forced
to40 at `0x5a8ce..0x5a8e0`; its actual raw resource value is25. Both admission
and debit use the shared native `unit_spawn_cost` helper. See
`EFFECTIVE_UNIT_COST.md` for the correction and original loader evidence.
Form-zero field 12 is used even when the unit is currently in form one. The
ordinary human command does not require the actor to occupy the AI's selected
upgrade slot. Current-player ownership, stationary phase and higher UI/script
locks remain the caller's command boundary.

`apply_upgrade` requires that eligibility. Original `0x43f2c` increments the
level, recalculates the three signed combat stats through `0x4328a`, and sets
action flag `0x2c` to one. It does not heal the unit, alter movement spending,
transform it, or clear health countdowns. `0x556a8` subtracts the effective signed
cost from the actor owner's funds with 32-bit wrapping, then clamps a negative
result to zero. Negative synthetic cost bytes for other types therefore retain
their original behavior instead of becoming unsigned prices. Type21 always
uses40, including when its raw byte is negative after signed conversion.

The native result exposes the updated combat/movement copies, signed cost and
funds. The caller must apply them together with the separately computed slot
advance before publishing an updated state. Renderer effect mode 0 and the
funds/stat strings are presentation work. Human menu dispatch requests audio
group 5/event 13 at `0x59edc..0x59ef0`; `0x556a8` itself does not request that
sound, so an AI call must not gain it implicitly.

The native action accepts the authorized level range 0–24. It does not pretend
that calling the original unchecked level increment at level 255 is equivalent
to recalculating stats at stored level zero: the original passes 256 to the
stat function before storing a byte. Such an ineligible direct call is rejected.

### Persistent upgrade selection (`map+0x34`)

This state contains original owner-slot indices, not type IDs and not indices
into a shortened active-unit vector. All 23 original type objects participate
in the owner arrays in ascending type order, with unused trailing capacity.
The native view takes the same packed type order, including inactive/unspawned
types, and validates the supplied slot against it.

| Boundary | Exact operation |
|---|---|
| Map load `0x5077a` | Reset both owner selections to −1, reset active counts and deactivate original objects. Native callers initialize selection to `{-1,-1}` when loading a new map. |
| Opcode-6 post-spawn `0x567f6..0x5686c` | Scan spawned owner's slots from zero. Store the first active unit whose form-zero field 12 is not 1. If none qualifies, leave the previous stored value unchanged. |
| Upgrade `0x5572c..0x557aa` | Advance the **current player's** stored slot, wrap through the original capacity, and select the next active non-producer. This is current player, although funds were charged to actor owner. |
| Removal `0x43ac8` | Only cycle if the removed unit held its owner's current selection. Treat it as inactive. If the scan circles back without finding a replacement, retain the removed slot; do not invent a −1 write. |

Selection candidates do not check funds, level, action-used or selected form.
Thus an already upgraded/acted unit can be the next selected slot. The caller
must not substitute full upgrade eligibility for this narrower selection test.

`first_upgrade_slot`, `advance_upgrade_slot` and
`upgrade_slot_after_removal` expose these operations separately. Removal does
not itself deactivate the native world unit, decrement its count, clear
renderer bookkeeping or update objectives; those remain the destruction path's
responsibility. The helper accepts either a still-active input being removed
or its already synchronized inactive state and excludes the removed type.

The original upgrade cycle can loop forever with no eligible active object.
The native helper rejects that unsupported state before any caller mutation.
It also rejects advancement from stored slot 22: the original first increments
to 23 and reads beyond the 23-slot allocation before testing wrap. This unchecked
read is not reproduced. The actual decoded two-faction owner arrays and the
comparison corpus do not reach that boundary. Other malformed owner/slot and
unsynchronized unit inputs are rejected explicitly.

## Repair

The command bit comes from outer active/action-used gates in `0x5300e`, then
`0x4a73c`: selected-form signed field 7 must equal one. Admission does not require
funds or missing health. The native API preserves that separation.

The original target list uses `0x4926a` with minimum and maximum Manhattan
distance both one, excludes the opposing owner, and appends the actor last.
Consequently targets are active friendly orthogonal neighbors in ascending
original type order, followed by self. The list does not exclude a fully healed
unit, a unit with movement offsets, an immune unit or a unit on special terrain.
No line-of-sight test is added. `healing_targets` exposes that list; ordinary
command eligibility still must be checked before executing repair.

After a target is confirmed, `0x588e0` calculates `missing = 100 - health`.
If that is nonpositive, it performs no HP/funds/action-spent mutation. For
exactly 100 health the UI message is `0x21ff04c4`; health above 100 follows the
generic failure message `0x21ff01e2`. If missing health is positive but current
player funds are nonpositive, the latter message is used, again with no
resource or action-spent change.

Otherwise the original loop buys ten health points per unit of funds, stopping
when the requested health **exceeds** the missing amount or all funds are used:

`spent = min(current_player_funds, floor(missing / 10) + 1)`

This includes the extra increment when missing health is an exact multiple of
ten. Examples from the original comparison include 90 HP with ten funds
spending two to reach 100, 80 HP spending three, and 0 HP with eleven funds
spending eleven. With insufficient funds the repair is partial; 1 HP with
three funds reaches 31 HP. This is the original loop behavior, not a revised
pricing formula.

Original `0x431be` adds the requested amount as a byte, clamps the resulting
byte to at most 100, and sets only countdown `0x30` to −1. Preset `0x34` and the
immunity flag remain unchanged; immunity does not block healing. Successful
confirmation deducts current-player funds, requests renderer effect mode 1 for
the target and audio group 5/event 15, and marks the **healer** action-used.
Self repair applies the health and action copies to the same logical unit.

`apply_heal` requires ordinary admission and an active friendly neighbor/self
target, then returns the target copy, actor movement copy, funds, requested
health, cost, outcome, optional original message ID and audio request. The
caller owns target/camera presentation, clearing the action selection, HUD
refresh and subsequent mission evaluation. The native result is not a claim
that those UI operations happened.

## Independent original comparison

`analysis_tools/actions/oracle.py` imports the frozen shared infrastructure
oracle. It maps the pinned original image/libgcc read-only, checks both after
execution, and executes gameplay helpers unchanged. It records these explicit
presentation boundaries without supplying their gameplay results:

- `0x5ca2c`: renderer effect type/mode request.
- `0x60800`, `0x60b70`: funds/UI string operations, whose return values do not
  determine action arithmetic.
- `0x2ad12`: audio group/event request.

Allocator/byte-memory/nonthrowing unwind infrastructure is the same explicit
adapter used by the AI oracle; integer arithmetic transfers into original
ARMv6 libgcc. No eligibility, heal amount, funds, stat or slot decision is
substituted by a host hook.

The oracle executes complete `0x4ec0e`, `0x5300e` and `0x4a73c` for admission and
target order. An upgrade runs original `0x556a8` through its fund/selection writes
and stops at `0x557aa` before final UI refresh. Repair runs `0x588e0` until success
at `0x589f4`, no-missing-health at `0x58aa6`, or no-funds at `0x58ae4`, before map
selection/UI cleanup. The requested amount is observed at original `0x431be`.
Post-spawn selection executes the unchanged dispatcher fragment
`0x567f6..0x5686c` with explicit original stack locals. Removal and map reset
execute complete `0x43ac8` and `0x5077a`.

| Corpus | Coverage | Native checks |
|---|---|---|
| 201 synthetic cases | Original192 worlds plus9 type21 affordability/debit boundary cases; 71 upgrades; 78 repair confirmations: 40 success, 26 no missing health, 12 no funds; 103 death-selection changes; 11 all-inactive worlds | 3,552 passed |
| 92 actual-definition cases | All 23 original definitions, both forms, levels 0/24; 68 upgrades, 4 repairs; original first grid/rules with explicitly seeded placements, HP, funds and slots | 1,616 passed |
| Undefined-behavior sanitizer | Both corpora | Passed, no diagnostics |

Each fixture test also executes eight unsupported/malformed-input rejections.
The raw source-data fixture contains original definition bytes and stays outside
Git. Only synthetic inputs and original expected results are committed as
`tests/unit_actions_cases.txt`. The source-data comparison is a function test,
not an observed live repair/upgrade, full turn, campaign or iPhone run.

Compact hashes, commands and original boundaries are in
`unit_action_validation/original_unit_actions.json` and
`unit_action_validation/native_unit_actions.json`. Source image SHA-256 is
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`;
libgcc is `d758fbf1ac952532955d62866bd02df55315c0599f64bdd978ff941fccbdcf83`.

## Reproduction

From project root:

```sh
g++ -std=c++17 -O2 -ffunction-sections -fdata-sections \
  -Wall -Wextra -Werror -pedantic -I native/include \
  native/src/UpgradeAction.cpp native/src/HealAction.cpp native/src/Combat.cpp \
  native/src/World.cpp native/tests/unit_actions_test.cpp -Wl,--gc-sections \
  -o research/native_actions/unit_actions_test
research/native_actions/unit_actions_test native/tests/unit_actions_cases.txt

python3 analysis_tools/actions/oracle.py \
  --binary research/binary/G1awakening.armv6 \
  --libgcc ../.tools/g1_runtime/Applesauce/touchHLE_dylibs/libgcc_s.1.dylib \
  --unicorn-dir /tmp/g1-movement-oracle-tools/python \
  --output research/native_actions/original_actions.json \
  --fixture native/tests/unit_actions_cases.txt --count 192 \
  --world-json research/binary/native_world/native_world_actual.json \
  --source-fixture research/native_actions/source_actions_cases.txt
research/native_actions/unit_actions_test research/native_actions/source_actions_cases.txt
```

Omit both world-json/source-fixture options for synthetic-only reproduction.
The `--count 192` requests the original varied corpus; nine explicit type21
cost-boundary cases are appended. The original loader-prefix check also runs
161 type/raw-value combinations without replacing the loader branch.
The ordinary C++ test requires no original executable, assets or emulator.
Do not commit the local original inputs, source fixture, raw source export or
test executables. No Apple/device/full-playthrough claim is made by this slice.
