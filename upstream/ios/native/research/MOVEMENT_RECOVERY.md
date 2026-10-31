# Recovered native movement cost and range calculation

## What is implemented

`include/g1/Movement.hpp` and `src/Movement.cpp` implement the movement-cost and
remaining-budget field used by the original G1 Awakening iOS 1.1.3 movement
wrapper. This is portable C++17 compiled from source. It contains no guest CPU,
binary translator, original executable, or compatibility host.

The module accepts decoded map cells, occupancy, point records, rectangular cost
regions, and the selected unit form's movement fields. It computes a field of
remaining movement budgets and the original vacant-destination predicate. It
does **not** choose a route, animate a unit, commit a movement action, advance a
turn, or load these inputs from the game's resource files.

## Original function evidence

The original executable's SHA-256 is
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.
Addresses below are virtual addresses in that executable; entries execute in
Thumb mode. The preserved Ghidra function inventory located the functions.
Their instructions were then inspected with Capstone and executed independently
for the differential checks below. No new Ghidra decompilation is claimed for
this module.

| Original address | Recovered role | Native counterpart |
|---|---|---|
| `0x50844` | Reset the field; obtain remaining budget; call the full-fill mode; query vacant destinations | `compute_movement_range` |
| `0x49b16` | Initialize every budget cell to `-1` | Result initialization |
| `0x43724`, calling `0x436f8` | Selected form's base budget minus spent byte, truncated to eight bits | `remaining_movement_budget` |
| `0x4a29c` | Four-neighbor recursive budget expansion | Explicit native work stack |
| `0x4a16e` | Destination entry cost and blocking predicates | `movement_entry_cost` |
| `0x4a154` | Access a byte in the selected terrain definition | Supplied decoded terrain fields |
| `0x496bc` | Convert row-major map tile index through the terrain lookup table | Caller-side input contract; loader not reconstructed here |
| `0x4979c` | Membership in rectangles with exclusive right and bottom edges | Cost-region check |
| `0x4a130` | Point record's fourth integer equals zero | Point state check |
| `0x4914e`, `0x49300`, `0x492ec` | Resolve unit occupancy and test its active byte | Supplied resolved `active_owner` |
| `0x49b5a` | Check clamped neighbors, then vacant reachable cells | `has_unoccupied_destination` |

The original movement selection path also calls the wrapper at `0x54a74`.
This ties the recovered calculation to the game's selection/range handling.

### Data layout established by the instructions

| Original location | Observed representation | Native input |
|---|---|---|
| Map `+0x15`, `+0x16` | Width and height bytes | Dimensions in range 1–255 |
| Map `+0x18` | Byte vector indexed by `y * width + x` | Caller decodes map tiles |
| Map `+0x20` | Tile-index to terrain-definition lookup | Caller resolves terrain definition |
| Map `+0xb4` | Vector of columns; each is a vector of signed 32-bit budgets | Row-major native result with equivalent coordinates |
| Map `+0x174` | Type → form → byte-stat vectors | Selected profile fields |
| Map `+0x294` | Terrain-definition vector object | Terrain byte 0 and byte 2 |
| Map `+0xc`, count `+0x10` | Vectors containing `x, y, width, height` | Cost-99 regions |
| Map `+0x420`, count `+0x424` | Point records; fields 0/1 are coordinates and field 3 controls blocking | Point records |
| Unit `+9`, `+0xa`, `+0xc` | Type, owner, form bytes | Selected profile |
| Unit `+0x3b`, `+0x3c` | Tile coordinates | Origin |
| Unit `+0x59` | Spent movement byte | `spent_budget` |
| Unit `+0xd`, `+0x40`, `+0x44` | Active-byte test and two zero-field filters in unit lookup | Caller resolves occupancy before this module |

The semantic names of terrain byte 2 and unit-form stat byte 6 remain unresolved.
Their numeric tests are preserved. Type 11 retains its original explicit rule;
this report does not assign a character name to that type.

## Rules preserved

The fill visits up, down, left, and right, skipping its immediate reverse edge.
Only a strictly greater remaining budget improves an existing cell. The native
implementation uses an explicit LIFO work list with equivalent depth-first
ordering, avoiding dependence on the C++ call stack.

The cost function first rejects out-of-bounds destinations with `10000`.
An active unit of another owner blocks ordinary traversal. A positive signed
unit-form field 6 skips that owner rejection. A matching point with nonzero state
blocks either mode. The base terrain cost can then be replaced by `99` inside a
cost region. Positive field 6 changes cost to one when terrain field 2 is zero;
otherwise type 11 changes non-99 costs to one. This order is significant.

The fill separately rejects raw terrain cost 99 for nonpositive field 6. That
test uses the underlying terrain field, not a rectangular override. An expensive
step may consume the remaining budget down to zero when the current budget
equals the nonzero **signed** base-budget byte, unless its cost is 99 or 10000.
The original eight-bit subtraction of spent movement is retained, including
wraparound. No guessed clamping rule replaces it.

The vacant-destination predicate excludes remaining value 127, as the original
does. It also preserves an easily missed gate: if all four clamped adjacent
positions have negative budgets, it returns false without scanning the field.
An interior, unoccupied origin with zero budget therefore does not alone count
as a vacant destination. The first native draft's simpler full-field scan failed
the original-code comparison for that case; preserving this gate resolved the
mismatch.

## Independent original-code verification

The development reference executed the original ARM functions with synthetic
state under an external Unicorn 2.1.4 installation. It seeded the original
vector/object layouts, including the singleton data used by the budget getter.
The executable's `__TEXT` was read-only and compared byte-for-byte after each
case. There were **zero instruction patches and zero function stubs**. No iOS
framework calls or compatibility host were involved in these bounded function
executions.

The native module matches **256 cases and 9,092 scalar comparisons**: remaining
budget, every result cell, every destination cost, four out-of-bounds costs per
case, and the vacant-destination result. The corpus contains 31 named boundary
cases and 225 deterministic cases generated with seed `0x473141`.

Cases cover ordinary movement, alternative routes, zero-cost cycles, zero
budget, expensive first steps, spent movement, byte wraparound, base values
127/128/255, negative and positive field 6, raw terrain 99, rectangular overrides,
type 11, friendly/enemy occupancy, point states, reserved value 127, and absent
origin occupancy. The fixture in `tests/movement_cases.txt` contains synthetic
inputs and numerical original-function outputs only. Its C++ test has no
emulator or original-game dependency.

Additional native validation rejects four malformed-input conditions: zero
dimensions, wrong cell count, an invalid origin, and rectangle arithmetic that
exceeds the original signed range. Those checks are intentionally reported
separately from original-code equivalence.

The module and tests compile with GCC in C++17 mode using
`-Wall -Wextra -Werror -pedantic`. The same corpus also passed AddressSanitizer and
UndefinedBehaviorSanitizer. LeakSanitizer could not inspect processes in this
environment, so leak detection was disabled for that sanitizer run; no leak-test
claim is made.

## Remaining integration boundaries

There are five unresolved integration areas; none are supplied with invented
game rules in this module:

1. Resource loading must establish the actual tile-to-terrain table and terrain
   definition fields for each mission.
2. The unit database must provide the correct selected-form stats and spent
   movement state.
3. The world model must reproduce the original occupancy lookup filters before
   supplying `active_owner`.
4. Mission state must populate point states and cost regions with their original
   lifecycle and meaning.
5. Route selection, movement execution, undo, animation, and turn progression
   remain separate work.

Only the full-fill mode called by `0x50844` is implemented. Other modes of the
original recursive helper are outside this API. Host function equivalence does
not establish a complete native game, Apple ARM64 compilation, iPhone execution,
performance, combat, or a campaign playthrough.

The full private reverse-engineering checkpoint retains the reference runner,
synthetic inputs, and original-result JSON under
`research/binary/native_movement/`. The original executable and external
development tool are not part of the native source deliverable. Exact hashes and
verification boundaries are recorded in `MOVEMENT_RECOVERY.json`.
