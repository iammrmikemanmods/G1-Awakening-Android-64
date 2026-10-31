# Native route reconstruction and unit travel

This milestone implements the original route choice, unit travel updates, deferred budget charge and ordinary undo branch as portable C++17. It is **not yet connected to the native iOS app**. The module stops at the unit's completed-travel event; arrival effects, occupancy changes, transport, capture, combat, mission progression and command-menu eligibility remain separate work.

## Verified result

`MovementAction.cpp` and its test/probe compile with `-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror`. The focused contract executable reports **36 checks passed**.

The development-only reference executed unmodified original ARM functions against synthetic decoded state, then compared native output:

| Comparison | Result |
| --- | --- |
| Cases | 136 exact matches |
| Unit-state snapshots, including ordinary undo | 4,820 |
| State scalar comparisons | 91,580 exact matches |
| Route coordinate comparisons | 690 exact matches |
| Original instruction patches | 0 |
| Original function stubs | 0 |
| Original text protection | Read-only and verified unchanged |

Cases cover all four directions and corner routes, uniform and weighted terrain, types 0/11/19, forms 0/1/2, signed form-zero stat values, existing auxiliary flags, ordinary undo resets, eight-bit budget wraparound, and simulation deltas from zero through `INT32_MAX`. They exercise original movement-range generation before route reconstruction. The report is `movement_action_validation/original_actions.json`; it contains the inputs, routes, and every compared state. This is function-level evidence with synthetic data, not a completed level or a phone performance test.

## Public API and integration boundary

`choose_movement_route(range, origin, destination)` produces forward tile waypoints, excluding the origin and including the destination. Its status distinguishes a ready route, an origin click, unreachable destinations, a disconnected field, a cyclic field and the original storage limit. Malformed dimensions or out-of-bounds coordinates throw before a result is used.

`MovementAction` takes an already admitted move, decoded unit state, the selected-form movement profile, its original budget field, the chosen route and signed byte 2 from **form zero's** stat record. It does not decide whether the player may command that unit, whether an occupied destination permits transport, or whether the current turn/menu state admits movement. The caller owns those checks.

`advance_tick(delta_ms)` advances exactly **one original simulation update**. The returned state supplies the existing `SpritePlacement` inputs: `tile_x`, `tile_y`, `offset_x`, `offset_y` and `form`. It also supplies facing/action column, pending/spent budget, route cursor and an independent countdown. A completed result occurs once; later calls remain stationary and do not charge again. The generic sprite animation player and the world arrival callback must be coordinated separately.

`reset_movement_turn(state)` reconstructs unit-only `0x430fc`. This helper is available to the separately recovered objective/turn executor. It clears spent movement and original flags `+2c`, `+2d`, `+2f`, `+38`, `+39`, `+3a`, then saves the current tile into the anchor. It does **not** change facing, saved facing, form, sub-tile offsets, movement mode, route state or timer.

`undo_ordinary()` reconstructs only `0x4324a` followed by `0x430fc`, for a completed move with `+2d == 0` and `+3a == 0`. It restores the saved anchor tile/facing and resets the proven fields. A moving action and the special/transform-related branches return explicit unsupported results without mutation. The caller must still verify command eligibility and update world occupancy/presentation. The module does not promise rollback of world effects that happened after arrival. Selection cancellation and interruption of an in-flight move are not implemented.

## Recovered route algorithm

Original `0x49e90` starts at the destination and compares remaining movement values in the adjacent up, down, left and right cells. Out-of-bounds neighbors and reserved value 127 are treated as -1. It selects the largest remaining value, resolving ties in exactly **UP, DOWN, LEFT, RIGHT** order, and recurses until reaching the origin. Unwinding writes each forward step. The base case writes the origin without incrementing the route count, so the first unwound step overwrites it; a normal route excludes the origin. A click on the origin has count zero and the game treats selecting the occupied unit separately in `0x54ef2..0x54f0c`.

The algorithm does not use a generic shortest-path library or a new tie policy. It also does not require a selected neighbor to have a strictly larger budget. A zero-cost plateau or inconsistent supplied field can therefore cycle in the original recursive code. Native code detects that condition and reports `cyclic_field`; it does not invent a replacement route.

`0x49618` returns twice the sum of map width and height. Unit begin `0x43e72..0x43ea6` allocates that many two-integer coordinate records when necessary. Route entries after the final step have `(-1,-1)` sentinel values; reset `0x4a044` writes them. The next-entry peek in `0x43616..0x43638` requires room for the trailing sentinel. The unit route index at `+58` is an eight-bit value. Native code enforces both the storage capacity and a maximum of 256 travel steps. It does not reproduce an out-of-bounds write or index wrap from an oversized route.

## Move begin, travel and charge

The ordinary destination flow is:

1. Map click confirmation `0x54ebe` checks the selected unit and destination occupant. An empty destination can set map `+e0` after checking that the first route coordinate is not -1. Friendly transport is a separate stat-dependent branch.
2. Human input update `0x5940c..0x59468` consumes the pending request and calls `0x51364`.
3. `0x51364` evaluates `0x434e4`, setting unit `+38` when signed **form-zero** stat byte 2 is greater than 1. It copies the range/route and calls `0x43e2e`. The native oracle executes the actual predicate and unit-begin function, while reproducing the wrapper's literal flag store; vector-copy allocation is not claimed under test.
4. `0x43e2e` sets unit mode `+8` to 1, computes pending cost as eight-bit `remaining_budget - destination_remaining`, copies route coordinates and resets the route index. Spent movement `+59` is unchanged here.
5. Map `0x59a7e` calls `0x4351c` once for each relevant active unit on an update.

The original travel constants are deliberately preserved:

| Unit form | Horizontal increment per update | Vertical increment per update |
| --- | ---: | ---: |
| 0 | 3 logical pixels | 3 logical pixels |
| Any nonzero form | 6 logical pixels | 9 logical pixels |

A tile is 32 logical pixels. Horizontal motion is considered first, then vertical motion; the valid recovered route consists of adjacent orthogonal steps. A move clamps its final increment to the target rather than overshooting. Original helpers `0x431da` and `0x43212` normalize the sub-tile offset once it exceeds +15 or falls below -16, updating the byte tile coordinate at that crossing. This means the logical tile field changes at the halfway boundary, before arrival at its center. Consumers that require completion must check offsets as well as tile coordinates.

Direction writes are exact: up 0, down 1, left 2, right 3 in original unit `+4b`. This field is consumed as the animation lookup column by the sprite renderer. At a waypoint, the index advances at most once during the update; the next segment begins on the next update. On the final waypoint, mode becomes zero, pending cost is added to spent movement with eight-bit wraparound, and the form-zero stat predicate may set `+38` again.

## Scheduling is still an integration gate

The spatial increments in `0x4351c` do **not** multiply by its millisecond argument. A zero-ms call still advances travel. Its `delta_ms` argument only decreases a separate unit countdown at `+30`; -1 disables that countdown and other values clamp at zero.

Calling this routine at 60 display frames per second would therefore impose a new movement speed. A separate development-only observation captured 600 original SetTimer calls between two verified first-mission frame markers: all read `CCore_Cocoa+8 == 0` and requested double `0x3f9eb851e0000000`, or approximately **29.999999329447746 ms**. This is the requested one-shot delay, not a measurement of exactly 33⅓ delivered updates per second: callback work and scheduling can add time. The native app should preserve that nominal simulation step and coordinate elapsed-time consumers separately, while interpolating presentation if needed. This module does not claim 60-FPS performance. Final timer artifacts are owned by the separate `research/native_timer/` audit, outside this module's frozen validation set.

## Completion is not all arrival behavior

After unit updates, `0x59b04..0x59b2a` checks map phase 2, the selected unit's completed mode and map `+28 == -1`, then calls `0x59798`. That routine includes direction changes relative to nearby units, transport-related checks, menu/AI handling and other world transitions. None is silently replaced with a no-op by this module. The native caller receives the unit completion boundary and must execute a recovered arrival transition before claiming the full action is implemented.

Ordinary undo dispatch `0x5a2ce..0x5a330` has additional branches. When `+3a != 0`, it resets action state but then stores spent movement 1 and restores `+3a = 1`; when the saved `+2d` was nonzero, it calls `0x43a54` after the reset. Those branches are intentionally rejected by `undo_ordinary` pending their own recovery. Undo eligibility, interaction cancellation and transport cancellation remain outside the API.

## Original evidence

| Function/range | Recovered behavior |
| --- | --- |
| `0x49e90..0x4a042` | Route reconstruction and tie preference |
| `0x4a044..0x4a090` | Route count reset and sentinels |
| `0x49618..0x49622` | Unit route capacity formula |
| `0x51364..0x51446` | Begin wrapper and form-zero flag |
| `0x43e2e..0x43f0e` | Unit begin, pending charge, route copy |
| `0x4351c..0x43682` | Travel, waypoint, charge, countdown |
| `0x431da..0x43248` | Sub-tile normalization and direction |
| `0x434e4..0x4351a` | Signed form-zero stat-byte-2 predicate |
| `0x4324a..0x4326a` | Restore anchor tile/facing |
| `0x430fc..0x4312a` | Reset spent movement/action flags |
| `0x59a7e..0x59b36` | Per-unit updates and arrival callback boundary |
| `0x5a2ce..0x5a330` | Ordinary versus special undo dispatch |

The supplied original executable SHA-256 is `0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`. Analysis-only original execution uses external Unicorn 2.1.4. The native library, iOS app and CMake source list do not contain that tool, a CPU interpreter, guest runtime, or the original executable.
