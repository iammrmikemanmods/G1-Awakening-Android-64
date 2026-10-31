# Native style1 transient terrain decorations

`TerrainDecorations` adds the original style1 transient pool to the fixed
animated surfaces documented in `TERRAIN_ANIMATION.md`. It uses the same
resource420 material, original integer geometry and the session's shared
`GameRandom`. This is native C++17 source. The original ARM execution harness is
development evidence and is never linked into the application.

The supported construction profile is the existing interior terrain mesh:
zero outside camera padding and one original column batch. All 11 decoded
style1 campaign map/style pairs fit this profile. Other terrain styles,
outside padding and multiple column batches are separate recovery work.

## Recovered construction and selection

Original `0x6a3cc..0x6a782` counts candidates over the real columns and
height+3 rows, with additional rows clamped to the final source row. Capacity
divided by seven gives the number of transient slots. Actual candidate
registration at `0x6b1e8..0x6b28c` visits only real interior cells; top14 routing
suppresses registration for that cell. Unused capacity retains bytes127.

Each candidate is three raw bytes: signed status, tile X and tile Y. Free
records have status255. The native state deliberately retains this byte layout
because original `0x64e64` has an unusual retry operation: the initial offset is
`3 * (abs(signed_random_word) % candidate_count)`, but retries use
`(offset + 3) % candidate_count`, not modulo three times the count. Retries can
therefore inspect coordinate bytes. The reconstruction preserves this behavior
instead of changing the random distribution or choosing a conventional free
record scan.

Original `0x6b606..0x6b660` initializes slot phase to `slot_index % 4` and calls
the placement function once per slot. Each call consumes exactly one shared
random word, even when no candidate is found. Original `0x64ebc` leaves prior
geometry unchanged on failed selection. In style1 it also skips the branch
that releases the previous candidate mark, so occupied records accumulate.
Neither failed selection nor an exhausted pool permits skipping a random draw.

Assigned slots use original32-unit top quads at the selected visual height and
atlas cell0. Unassigned slots preserve the initial `0x71248` subdivision strips:
their X coordinates use the full original overallocated mesh-quad count,
their Y is zero, and their Z spans the grid height. Some strips are degenerate;
they are preserved rather than replaced with guessed placement. Initial cells
cycle through0..3. Position scale remains0.01. UV quantization and mode1
normalization match the fixed animated material.

## Simulation and checkpoint contract

The public types are in `include/g1/TerrainDecorations.hpp`:

| API/type | Responsibility |
|---|---|
| `TerrainDecorationLayout` | Immutable dimensions, original candidate bytes, registered count and mesh capacity; rebuild from the same resources without RNG. |
| `TerrainDecorationState` | Complete mutable clock, candidate bytes and each slot's phase/selected byte offset. |
| `build_terrain_decoration_layout` | Validate data and derive the recovered construction layout. |
| `create_terrain_decorations` | Construct a new map's pool using the caller's existing shared RNG. |
| `advance_terrain_decorations` | Advance one simulation update and report phase/placements/draw count. |
| `validate_terrain_decorations` | Validate a restored state against its immutable layout without draws. |
| `terrain_decoration_quads` | Derive geometry without changing state or consuming RNG. |

Save `TerrainDecorationState` together with the same session's complete
`GameRandom::State`. On restore, rebuild the immutable layout and validate the
saved pool. **Do not call `create_terrain_decorations` during restore.** Doing so
would consume extra game random words and change later gameplay outcomes.

The pool owns the same clock as the fixed420 surface. One update adds elapsed
milliseconds; at250 or more it subtracts250 once. Each slot phase advances;
slots reaching4 reset to0 and reselect in ascending index order. The global
phase also advances modulo4. Preserve backlog rather than looping through all
elapsed phases. Both fixed and transient420 quads use
`terrain_animation_u_offset(state.animation)`. Do not advance a second clock
for the fixed surface. Rendering is read-only and must not draw random words at
the display refresh rate.

Static original call order identifies terrain update `0x6d624` inside owner
update `0x5d5bc`, called from the map update at `0x5b618`. AI/human handling
(`0x58dcc`/`0x5932c`) and the combat/mission phase branches precede that call.
Owner update calls `0x666a8`, `0x5cc70`, terrain `0x6d624`, then `0x5cbaa`,
`0x65bd0`, `0x66730`, `0x6526c`, `0x5ca44`, `0x66578`. It skips when map byte
`+0x225` is nonzero. The outer call also requires map byte`+0x14` and excludes
the controller state6/substate5 branch. These are static boundaries, not proof
that the native application's complete scheduler currently matches every
original scene and pause transition.

Construction/updates validate before committing caller state or RNG. Unsupported
signed coordinate ranges, malformed candidate/slot records, multi-batch
construction and signed32 clock overflow raise `FormatError`. The original
wrapped `abs(INT_MIN)` can produce a before-allocation offset; that access is
rejected without committing a draw. A zero remainder for the same word is valid
and remains supported. No bad access is converted into a fabricated position.

## Original-function evidence

The unchanged original executes candidate registration, complete initial mesh
construction, actual seeded generator/draw functions, selection, placement and
clock callbacks. The harness records bounded allocation/memory operations,
M3G array/object APIs, engine and seeded RNG singleton addresses, nonthrowing
unwind registration and the triangle-strip factory. It does not replace random
words, candidate selection, geometry arithmetic or timing instructions. Original
main/libgcc code remains read-only and its bytes are checked after execution.

| Check | Result |
|---|---:|
| Synthetic worlds |24 |
| Actual style1 map/style pairs |11 |
| Total original/native state snapshots |595 |
| State, geometry, UV and RNG scalar checks |521,220 |
| Portable synthetic fixture snapshots |408 |
| C++ fixture/restore/malformed-state checks |38,389 |

All comparisons passed. Every snapshot compares the full624-word RNG state
through a hash, as well as draw count and generator index. The portable fixture
contains synthetic inputs/results only. Its native test also restores a copied
mid-run pool and RNG, then checks continuation against the remaining original
snapshots. Actual grids/config/tables remain local; their report contains hashes
and counts, not original payloads. The selected seed is an explicit experiment
input, not a claim about a historical wall-clock seed.

| Grid resource | Candidate capacity / registered | Slots | Draws across17 snapshots |
|---:|---:|---:|---:|
|258|117 /102|16|72|
|259|77 /65|11|49|
|260|123 /93|17|76|
|261|102 /81|14|62|
|262|86 /65|12|54|
|265|107 /62|15|67|
|266|84 /60|12|54|
|267|78 /60|11|49|
|269|79 /52|11|49|
|275|75 /60|10|44|
|279|102 /75|14|62|

Reports are `terrain_validation/original_decorations.json` and
`terrain_validation/native_decorations.json`. This closes the style1 transient
pool omission in the earlier fixed-surface checkpoint. It does not close the
style0/2 sprite pool, complete scheduler, outer padding, GPU/device or full
campaign testing gaps. The first mission uses style0, so its full original
terrain RNG history is not established by this style1-only implementation.

## Reproduction and integration

From the directory containing `native/`, build the dependency-free fixture test:

```sh
g++ -std=c++17 -O2 -ffunction-sections -fdata-sections \
  -Wall -Wextra -Wconversion -Wpedantic -Werror -I native/include \
  native/src/Terrain.cpp native/src/TerrainDecorations.cpp native/src/Combat.cpp \
  native/tests/terrain_decorations_test.cpp -Wl,--gc-sections \
  -o LOCAL_VALIDATION/terrain_decorations_test
LOCAL_VALIDATION/terrain_decorations_test native/tests/terrain_decorations_cases.txt
```

Add `src/TerrainDecorations.cpp` to the ordinary native library. The CMake test
executable needs `tests/terrain_decorations_test.cpp`, links `g1_resources`, and
takes the absolute path to `tests/terrain_decorations_cases.txt` as its only
argument. No original executable or emulator is needed for this fixture test.

To regenerate independent evidence, build the same sources with
`analysis_tools/terrain/decorations_probe.cpp` in place of the test source, then
run `analysis_tools/terrain/validate_decorations.py --help` for its explicit
local executable/libgcc/manifest/table/probe paths. The validator checks pinned
input hashes, executes24 synthetic cases and11 actual style1 maps, and writes
the synthetic fixture and metadata-only report. Its full invocation is recorded
in `terrain_validation/native_decorations.json`. Original payloads, raw private
snapshots and compiled local binaries are excluded from the source checkpoint.
