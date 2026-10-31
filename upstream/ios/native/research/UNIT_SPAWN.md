# Native unit activation shared by script placement and production

`activate_unit` reconstructs the state transition in original0x54016. It
returns native world, movement and combat state, plus a signed funds cost and
an explicit pending-route reset request. The caller still owns admission,
funds, owner-slot arrays, active counts and renderer notification.

## Exact state boundary

The original0x4333a sets active, health100, position, form0, downward facing,
anchor position/facing, zero offsets and level0. It clears bytes18/19, sets
timer30 and route marker24 to−1, recalculates combat stats and calls0x430fc.
That last helper resets action/spent fields. Existing fields not written by
those functions remain intact. In particular, preset34, support bonus5c and
pending animation byte2e are not silently reset by this native helper.

The original0x5403e..0x54094 checks **form1 byte6 !=0**, then the destination's
terrain rule0==99 and rule2==0. When all hold it switches toform1 without
recording an ordinary transform action and calls0x430fc again. This differs
from the normal transform admission rules and does not require the unit's
scalar2 transform-availability flag.

Optional opcode6 modifiers are separate from ordinary production. A nonzero
fifth script word sets HP9 through0x43184, which updates timer30/preset34 to750,
then sets bytes18/19. A caller-resolved config+d9/controller0 flag can also set
immunity. The helper never infers that configuration flag from mission number.

Resolved type/faction/owner are explicit inputs. Type selection, including
randomized script sentinels and faction inversion, is outside this helper.
Coordinates are bytes; the caller validates the map and, for ordinary
production, occupancy and affordability. Activation itself does not authorize
placing a second active instance or provide evidence that a production action
was completed. `signed_cost` uses the separately verified loader override and
`apply_spawn_funds_debit` for exact wrapped subtraction and clamp.

## Original-function reference

The development-only oracle executes full0x54016, its actual activation,
terrain, stat and reset callees, and the later opcode6 modifier fragment
0x56792..0x567f6. It records the two funds-string output boundaries and the
renderer spawn notification; it does not replace gameplay decisions. Imported
signed division is supplied as its exact integer arithmetic. The original
code text remains unchanged.

The **192 cases** vary type, form1 traversal byte0/1/255, terrain cost1/99,
terrain field2, special placement, explicit immunity, prior funds, preserved
timers/support and destination. The oracle also asserts that active count
increments once for the correct owner, the other count remains untouched,
exactly one correct renderer request occurs, and animation byte2e is retained.

The native test compares **37 output fields per case** and passes
**7,685 comparisons and guards**. Inputs and fixtures are synthetic and contain
no original resource payload. No iOS execution, production UI, campaign flow
or device playthrough is claimed by these function-level checks.

Build `src/UnitSpawn.cpp` into the native core. Run `tests/unit_spawn_test.cpp`
with `tests/unit_spawn_original_vectors.txt`. Reproduction source is preserved
under `analysis_tools/unit_spawn`; it requires the hash-checked original local
executable and the unchanged `analysis_tools/world/world_oracle.py` support.
