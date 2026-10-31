# Native terrain sprite decorations for styles 0 and 2

`TerrainSpriteDecorations` reconstructs the original terrain sprite allocation,
placement, animation selection and transient reservation cycle in native C++17.
It complements the style-1 material-420 pool in `TERRAIN_DECORATIONS.md`.
No original CPU, executable or compatibility runtime is linked into the product.
The unchanged-original instruction harness is development evidence only.

This checkpoint covers the same **zero camera padding, single terrain batch**
profile as the earlier terrain mesh. All ten style-0/style-2 map/style pairs
found in the decoded mission streams fit that profile. The original stage-2
front and side decoration rows are included. Camera-dependent outer terrain
padding and other scene objects remain outside this profile.

## Source-backed bindings and placement

The original constructor at `0x6980c..0x6993a` uses the existing sprite table
through `0x66d58`. These six bindings all use image resource 961 and resource
key `f4e02223`:

| Selector | Descriptor resource | Role in the recovered path | Actual animations |
|---:|---:|---|---:|
| 73 | 638 | First three front sprites | 1 |
| 74 | 825 | Style-0 fixed and side decorations | 5 |
| 75 | 581 | Style-0 transient slots | 3 |
| 76 | 1095 | Style-2 fixed decorations | 4 |
| 77 | 1132 | Style-2 side decorations | 8 |
| 78 | 982 | Style-2 transient slots | 3 |

`load_terrain_sprite_animations` reads the original descriptors through the
validated `Archive` and `AtlasDescriptor` APIs. Original `0x5d17a` receives an
animation lookup vector of 207 bytes; selectors 73 through 78 take its genuine
out-of-range path and select animation zero. The oracle executes that original
lookup rather than replacing its return value.

Candidate capacity is counted over the real columns and height plus three
rows, with additional rows clamped to the final source row. Registration visits
the actual interior grid in row-major order. For style 0, `0x45a5e` returns tile
13 for every nonempty config-row entry. For style 2, resource 307 supplies tiles
18 and 23. Unused capacity retains the original bytes 127. One transient slot is
allocated per seven candidate records.

Original sprite indices 0 through 3 belong to other renderer features. This
module begins at index 4 and does not fabricate those objects. Indices 4 through
6 are front sprites; transient slots follow, then fixed and side sprites.
Style-0 fixed capacity is twice the count of lookup-class-1 cells plus four
times the width. Style-2 fixed capacity is the count of raw tiles 10, 30 and 31,
followed by three side rows of height plus one.

The stage-2 placement loop at `0x6994c..0x6a1e8` visits columns before rows.
The native implementation preserves its conditional random draws, float32
arithmetic, signed truncation, edge selection and early branches. In particular,
the third border pass reads the right raw edge tile before randomly selecting
the side whose terrain class is tested. A special edge can skip that interior
cell as well. The style-0 paired decoration branch enables and positions both
sprites but adds only the first mesh to the parent at `0x6a070..0x6a088`.
An enabled, positioned but unattached sprite is therefore not drawn. This
behavior is retained rather than replaced by a visually plausible pair.

Every original `addChild` is recorded as a parent category and attachment-order
serial. Reattaching a node updates that serial. Native draws preserve this
ordering within each parent. The full ordering of these groups relative to
other scene passes remains the renderer's scene-graph responsibility.

The model transform comes from original `0x727a4` with scale 1 and a negative
45-degree rotation around X. The original skips its scale setter when the
scale is 1. The resulting column-major matrix is translation by raw coordinates
divided by 100, followed by the recovered X rotation. There is no guessed tile
center offset. Original matrix-core execution verifies the native float bits
for 645 unique visible positions. The reference's external trigonometric
boundary uses host `sinf`/`cosf`; this is not a claim of bit identity with a
particular historical iOS math library or of GPU pixel identity.

## Shared random sequence and update order

New-map construction performs stage-1 reservation draws before stage-2 sprite
placement and animation-selection draws. Original opcode 9 completes these
renderer stages before advancing to the following unit-spawn commands. See
`TERRAIN_RNG_ORDER.md` and `terrain_validation/map_init_rng_order.json` for the
independently checked dispatcher phase order.

**Create the terrain state before subsequent mission spawns.** Appending its
draws to an already bootstrapped world's generator gives a different game
state. The following measured draw counts use the explicit experimental seed
`0x47413133`; stage-2 counts can depend on random branches.

| Grid resource / style | Initial pool draws | Stage-2 draws | Total before later spawns |
|---|---:|---:|---:|
| 268 / 0 | 14 | 107 | 121 |
| 274 / 0 | 4 | 132 | 136 |
| 277 / 0 | 2 | 131 | 133 |
| 272 / 2 | 2 | 93 | 95 |
| 276 / 0 | 4 | 165 | 169 |
| 263 / 2 | 0 | 105 | 105 |
| 270 / 0 | 0 | 336 | 336 |
| 271 / 0 | 9 | 143 | 152 |
| 273 / 0 | 0 | 422 | 422 |
| 264 / 0 | 11 | 148 | 159 |

Slot phases begin at the slot index modulo eight. Style-0 transient animations
also receive an initial elapsed-time advance of slot index times 250 ms;
style-2 transients do not. Both begin hidden and are attached to the terrain
parent even before a visible pose exists.

In an update, all transient animations advance, including hidden sprites.
Elapsed time is then added to the terrain clock. At 250 ms or more, the clock
subtracts 250 **once**, retaining any backlog, and each slot phase advances.
Slots reaching eight reset to zero and run the original reservation/placement
operation in ascending index order. Visible front sprites are then advanced
through the corresponding `0x6526c` path. Later static decorations retain their
selected initial frame in this recovered path.

For styles 0 and 2, `0x64ebc` first finds and releases the slot's old candidate
mark, places the sprite at that old tile and makes it visible. It then consumes
one random word to reserve a candidate for the next cycle. A newly reserved
candidate does not immediately change the sprite's pose. A failed reservation
leaves the last visible pose intact. Playback is not reset by this relocation.

The selector preserves the original unusual retry from `0x64e64`: the initial
byte offset is three times the random remainder, while retries use
`(offset + 3) % candidate_count`, not modulo the triplet byte count. Coordinate
bytes can consequently be inspected. Replacing this with an ordinary free-slot
scan would change behavior and the shared random sequence.

The static owner-update ordering documented in `TERRAIN_DECORATIONS.md` applies:
mission/combat and human/AI phase handling precede terrain update `0x6d624`,
and the visible-front animation pass occurs later inside the same owner
update. The module represents this recovered slice. Complete live scheduling
relative to every scene transition, pause, effect or unrelated RNG consumer
has not been established by this standalone comparison.

## Native API and checkpoint contract

The public API is `include/g1/TerrainSpriteDecorations.hpp`:

| API/type | Responsibility |
|---|---|
| `TerrainSpriteAnimationBank` | Immutable decoded animation sequences for selectors 73 through 78. |
| `TerrainSpriteDecorationLayout` | Immutable grid, visual records, lookup, initial candidates, capacities and selectors. Rebuild without random draws. |
| `TerrainSpriteDecorationState` | Mutable clock, candidate marks, slot phases and every sprite's animation, elapsed/repeat/direction/completion state, visibility, parent, attachment order and optional original pose. |
| `create_terrain_sprite_decorations` | Execute the recovered new-map draws with the session's existing `GameRandom`. |
| `advance_terrain_sprite_decorations` | Advance exactly one simulation update and report phase, relocations and consumed random words. |
| `validate_terrain_sprite_decorations` | Check restored state against immutable resources without changing state or RNG. |
| `terrain_sprite_decoration_draws` | Return visible attached descriptor/frame/model bindings without advancing anything. |

Save the complete mutable state together with the same session's complete
`GameRandom::State`. On restore, rebuild immutable layout and animation data,
decode the saved state, then validate it. **Do not call the create function on
restore.** `SpriteAnimation::restore`, documented in
`SPRITE_ANIMATION_RESTORE.md`, preserves the exact pending elapsed time and
completion state without restarting playback.

Rendering must remain read-only; it must not consume RNG or advance playback
per display refresh. A renderer uses each returned `SpriteResource` and frame
with the existing sprite geometry APIs, then applies the returned model matrix.
The parent field preserves membership and `attachment_order` preserves sibling
order. The module does not claim that simply interleaving every parent into one
global draw list reproduces the entire original scene graph.

State construction and updates use temporary state/RNG and commit only after
validation. Invalid candidate bytes, duplicated marks, malformed playback,
invalid attachment serials, missing visible poses and signed32 time overflow
fail explicitly. Grid coordinates are bounded to positive signed-byte
dimensions, tile IDs below 32 and the single-batch profile. The conservative
face bound is `width * (height + 3) * 5 <= 2000`. Sprite count is at most 2048.
The original wrapped `abs(INT_MIN)` is preserved; a resulting negative index
is rejected without committing a random draw or partial update. A valid zero
remainder from the same word remains supported.

## Validation and its limits

The source-data harness executes the unchanged original allocation-count,
candidate-registration, constructor, placement and update blocks, including
the actual game RNG, sprite animation functions and original ARMv6 arithmetic
helpers. It records allocation/libc/unwind infrastructure, decoded-descriptor
and M3G object boundaries, ownership/enable/transform calls and the separately
validated sprite geometry refresh. It does not replace selection, placement,
random words, animation advancement or timing instructions.

| Check | Result |
|---|---:|
| Synthetic original/native cases | 12 |
| Actual style-0/style-2 map/style pairs | 10 |
| Original/native snapshots | 396 |
| State, binding, matrix and RNG scalar comparisons | 944,984 |
| Unique original matrix-core executions | 645 |
| Portable fixture snapshots | 216 |
| Native fixture/restore/guard comparisons and contracts, per build | 146,028 |

All passed. Every original/native snapshot compares the complete 624-word
generator state through a hash, plus its index and draw count. The portable
test additionally checks the final words directly and resumes a copied
mid-run state and generator against the remaining original snapshots. Both the
strict C++17 build and the undefined-behavior sanitizer build pass the same
fixture and malformed-state/transactional guards.

The committed fixture contains synthetic inputs and their original-function
results only. Actual grids, descriptors, original executables and raw local
diagnostics are excluded. The metadata report records input hashes, counts,
scope and explicit harness substitutions. This checkpoint resolves the prior
style-0/style-2 pool and stage-2 draw-count gaps for the stated profile; older
style-1 and map-order reports retain their historical scope.

Native application integration, its save codec, complete parent-group render
ordering, camera-dependent padding, complete scheduler fidelity, historical
seed identity, native GPU/device behavior and full campaign playthrough are
not certified by these module tests. They remain separate acceptance gates.

## Reproduce and integrate

From the directory containing `native/`, build the standalone synthetic test:

```sh
g++ -std=c++17 -O2 -ffunction-sections -fdata-sections \
  -Wall -Wextra -Werror -pedantic -I native/include \
  native/src/TerrainSpriteDecorations.cpp native/src/SpriteAnimation.cpp \
  native/src/SpriteGeometry.cpp native/src/Terrain.cpp native/src/Combat.cpp \
  native/tests/terrain_sprite_decorations_test.cpp -Wl,--gc-sections \
  -o LOCAL_VALIDATION/terrain_sprite_decorations_test
LOCAL_VALIDATION/terrain_sprite_decorations_test \
  native/tests/terrain_sprite_decorations_cases.txt
```

Add `src/TerrainSpriteDecorations.cpp` to the native library. A CMake test target
needs `tests/terrain_sprite_decorations_test.cpp`, links `g1_resources`, and
takes the absolute fixture path as its sole argument. Existing `SpriteAnimation`
must include the validated restore addition. No original game executable,
emulator or new external dependency is required for this test.

For independent regeneration, substitute
`analysis_tools/terrain/sprite_decorations_probe.cpp` for the test source and
run `analysis_tools/terrain/validate_sprite_decorations.py --help` for its
explicit local input paths. The exact commands, compiler flags, hashes and
results are recorded in `terrain_validation/native_sprite_decorations.json`;
the detailed original-function report is
`terrain_validation/original_sprite_decorations.json`.
