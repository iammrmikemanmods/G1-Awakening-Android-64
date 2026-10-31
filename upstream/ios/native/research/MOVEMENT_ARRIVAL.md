# Movement arrival boundary

`MovementArrival.cpp` implements the exact target collection and facing portion of original callback `0x59798`, ending before `0x59884`. It does **not** claim that the complete arrival callback, transport, menu restoration or a capture action is implemented.

The native module has **11 passing focused contracts** and matches **192 executions of the original prefix** exactly. The original runs use read-only executable text, no instruction patches and no function stubs. Execution stops at the prefix's natural basic-block boundary rather than replacing any callee. Reference input and output are recorded in `movement_action_validation/original_arrival_facing.json`. The original function's remaining body is not executed by that comparison.

## Facing and target collection

The selected-form stat-byte-13 predicate is `0x4347a -> 0x43440`. If its signed value is exactly 1, the prefix preserves the unit's current facing and does not refresh the target list. Other values, including negative values, execute the normal collection branch.

`0x436ec` returns unsigned selected-form stat byte 1; `0x436b2` returns unsigned selected-form byte 2. They are the inclusive minimum and maximum Manhattan distances passed to `0x4926a`.

Collector `0x4926a` clears the target list, then visits owner arrays 0 and 1, excluding the supplied actor owner. Its helper `0x491b6` excludes null pointers, the same unit object and inactive units. It tests the byte tile coordinates against the inclusive Manhattan interval. There is **no** sub-tile offset, line-of-sight, terrain, HP or target-stat test in this helper. A moving target remains eligible according to its current byte tile. The preliminary axis bounds are equivalent early rejections for the same maximum distance.

Original owner-array constructor `0x496e4` appends matching faction units in ascending type order. The native input may be in command order; `choose_movement_arrival_facing` restores that original slot order by type. It rejects duplicate or out-of-range original unit types and owners outside 0/1 when processing the collection branch.

Selection at `0x597f0..0x5987e` uses:

1. Smallest Manhattan distance.
2. Smaller absolute vertical distance when total distances tie.
3. Earlier original owner slot when both distances tie.

The resulting facing is left 2 if the target's X is smaller, right 3 if larger, and down 1 otherwise. With no candidate it is also down 1. The prefix never chooses up 0 during this normal retargeting branch. If stat byte 13 equals 1, the existing facing remains unchanged.

The output distinguishes a refreshed empty target list from an untouched prior cache using `optional<vector<uint8_t>>`. This is necessary because a unit that skips retargeting must not silently erase the earlier cache.

## First mission inputs

The privately supplied original data decodes to these selected-form-zero rows:

| Type | Resource | Fourteen stat bytes |
| --- | --- | --- |
| 11 | 284 | `2,1,1,65,75,55,0,0,0,0,1,0,0,0` |
| 19 | 286 | `3,1,1,45,55,30,0,0,1,0,1,0,0,0` |

At the initial positions type11 `(1,2)` and type19 `(4,7)`, neither is within the other's distance-one interval. A verified reference case also puts type11 at `(2,2)` after a rightward step with facing3: the arrival prefix changes it to down1 because no opposing unit is in range. This is a data/function comparison, not a phone capture.

## Ordinary phase effects after the verified prefix

The remainder is inspected original control flow; the 192-case oracle does not claim coverage of these later operations.

At `0x59884..0x5994a`, selected-form stat byte 8 controls a co-location branch. If nonzero, it scans active same-owner units other than the actor, requires the other unit's selected-form byte 9 to be nonzero and exact equality of both tile bytes, then calls `0x43bde`, `0x54244` and `0x51e46`, and decrements the owner's active-count byte. `0x43bde` reaches `0x43ac8`, which deactivates the actor and has further bookkeeping. These operations cannot be replaced by ordinary position commit. The branch remains unsupported until that complete transport/co-location transition is recovered. Type11 form0 has byte8 equal to zero, so it skips this branch.

For the normal human-controlled path (controller byte `map+98[current] == 0`) without that co-location transition:

- The original may call `0x5300e` to refresh command availability when unit flag `+2c` is zero. Command eligibility remains a separate recovered-rule surface.
- At `0x59a24`, controller state `+1c0 == 10` bypasses the normal `0x5537a` reset. The ordinary in-game path calls `0x5537a`.
- `0x5537a` closes/reset presentation state through its controller/widget calls. Its direct map writes at `0x553b0..0x553c0` set phase `map+2d8` and its timer `map+2dc` to zero unless phase is 6. A completed movement entered with phase2, so the ordinary branch returns that phase to zero.
- The same routine calls renderer/UI refresh functions `0x5cc64` conditionally, then `0x4e248`, `0x54f94`, and `0x4c584`. They are not substituted inside the portable facing helper.
- `0x2b95c` clears the shared input words at offsets4 and8. `0x5c9a8` clears the renderer-follow pointer at offset3c.
- Final call `0x54514` evaluates adjacent support-related presentation. It uses unit form byte11 and predicate `0x43738` (active same-owner other unit, byte11 nonzero, each coordinate within one tile) before requesting renderer effect3 and an optional tutorial presentation. That inspected path does not itself charge movement or write the site's capture state.

AI-controlled continuation is different: `0x59978..0x59a22` consumes an AI plan, may transform or dispatch subsequent action phases. It is outside the ordinary human transition.

These facts permit the session owner to distinguish travel completion, the ordinary idle-phase return, and the still-required presentation/command refresh. They do **not** authorize setting a capture goal satisfied. The first objective reads `map+3f8[site][group][item]`; its capture mutation belongs to a distinct action. Moving to the coordinate or returning phase to zero is insufficient.

## Reproduction and scope

Compile `analysis_tools/movement_action/arrival_probe.cpp` with `native/src/MovementArrival.cpp` and `-I native/include`. Then run `analysis_tools/movement_action/arrival_oracle.py` with the same private original executable, external Unicorn package and shared `analysis_tools/movement/oracle.py` used by the movement-action reference. Its arguments match `oracle.py` in that directory.

The original executable SHA-256 is `0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`. No original executable, CPU interpreter or guest runtime is used by the native module. The module is not a full level, full campaign, Metal performance or iPhone compatibility claim.
