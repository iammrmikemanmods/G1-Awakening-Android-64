# Tactical support presentation: verified recovery, not integrated

This research package recovers the original tactical support phase, its
separate 2D animation clock, and the percentage-label image's palette keying.
**It does not change the build 005 runtime or claim that the tactical overlay
is displayed by that IPA.** No production source, CMake target, game state,
combat calculation, RNG, or presentation acknowledgement is changed here.

The portable phase implementation and its test are deliberately stored under
`fixtures/`. They are a tested candidate for later integration, not installed
runtime headers. Original executables, the archive, and bitmap pixels are
explicit local reproduction inputs and are not included in this package.

## Evidence and results

| Component | Original instructions executed | Result |
| --- | --- | --- |
| Support entry and handoff | Entry branches `0x586aa..0x58796` and `0x586aa..0x5885c` | Seven prepared helper counts recorded, including direct handoff for zero helpers. Stops before the separate tutorial branch. |
| Support phase update | Complete `0x583f0`, including actual `0x5d176` | 1,280 original cases across counts, indices, timing boundaries, signed overflow and large/negative deltas. Native test compares 5,120 state/action/completion fields. |
| Complete native phase sequences | Fixture implementation | Counts 0 through 6; 67 simulation ticks per visible cue, at most five cues, exactly one nonempty-sequence handoff. |
| Separate 2D player | `0x5e238` and `0x5ea80` | Existing `SpriteAnimation` matches 57 cases, 684 states and 4,104 scalar fields. Four decoded original sequences plus three targeted synthetic sequences. |
| Original map effect scheduler | `0x5ca44` calling the actual 2D player | All 67 cue ticks recorded; it stops updating the player after completion at tick 56. |
| Property 9 and palette | Derived-constructor body, original switch helper/property branch, load-info stores and `0x1caa8..0x1cb0e` | Five property dispatch cases and 101 palette-word comparisons pass, including actual palette, disabled keying and synthetic high-byte cases. |

`results/` contains the exact inputs, captured original outputs, boundary
substitutions and hashes. Every executed original instruction range remains
read-only and is checked unchanged. These are bounded original-code
comparisons, not a full original-app, GPU, iPhone or playthrough test.

## Recovered phase contract

The map constructor allocates six helper pointers (`0x4bc22`) and stores
capacity six (`0x4bc30`). Combat may use all six. The presentation update
stops when the incremented index equals `capacity - 1` (`0x58466`), so it
shows indices 0 through 4 at most. Preserve this presentation limit without
truncating `SessionBattle.exchange.support_types` or recalculating damage.

For a nonempty helper list, the entry branch sets index and elapsed time to
zero and immediately prepares helper 0. For an empty list, the original
branch calls the main battle handoff directly; no support clock is active.
The fixture's empty state is a host sentinel, not a claim that the original
rewrites its otherwise-unused index/elapsed fields.

`0x5d176` is exactly `movs r0, #1; bx lr`. It is **not a renderer readiness
test**. Each update adds its signed 32-bit delta with ARM wrapping semantics.
Elapsed values at or below 1,999 wait. A value above 1,999 advances one helper
at most. It discards excess elapsed time when showing another helper and
retains the elapsed value on final handoff. At the native 30 ms simulation
step, each visible cue therefore lasts 67 ticks, or 2,010 ms. An independent
native requirement that a cue first reach a presented drawable would be a
host policy, not recovered original readiness behavior.

For each helper, the original does the following:

1. Starts animation 2 once, forward, in the helper's **separate 2D effect
   object**: `0x54502 -> 0x5ca2c -> 0x5e288 -> 0x5e238`.
2. Assigns the helper's primary name, a literal space and text resource
   `0x01f7` (503) to the support banner.
3. Focuses the helper through `0x5cb5a` and sets the tactical cursor to its
   tile; cursor appearance selector is zero.

The first entry has additional selection bookkeeping; the exact event order
is retained in the original entry records. Subsequent updates are recorded
separately. The primary name is the first raw definition text field (original
map vector `+0x12c`, populated at `0x5a7d6..0x5a7fe`), adjusted by the existing
`unit_text_index`/`script_text_id` helpers. It is not selected by the unit's
current transformation form. The suffix is direct text-table entry 503.

At completion the original resets interaction UI, clears the selected
renderer unit (`0x5c9a8`), restores cursor appearance zero and the saved
cursor tile, then calls main battle handoff `0x58172`. The phase update does
not emit a support-shot audio request.

### First-use tutorial boundary

The separately inspected block `0x58796..0x5884e` checks all six helper slots
for a helper owned by the current player, a stored app flag at configuration
`+0x0e`, and a human player controller. It clears the flag and shows resource
`0x21ff04a8`. This block is not executed by the phase oracle. Its persistent
flag and presentation are not integrated; do not silently replace them with
a new per-battle popup or claim tutorial parity.

## The effect is a percentage label, not a unit firing animation

Map load phase 3 (`0x56e90..0x56ea2`) creates an effect bank at renderer
`+0x28`. Its first constructor call is
`0x5fa34(new_object, 1000, 0x03000100)`; further entries clone that object.
The actual unit sprites use a different M3G bank and animation mapping.
Assigning animation 2 to the unit sprites would select unrelated content.

The separate descriptor recovery records the mapping:

| Resource | Role |
| --- | --- |
| 256, tag `0x03000100` | Version 1 descriptor, 546 bytes, 13 rectangles, 13 variants, 27 frames and four animations. |
| 1000 | Three image properties `(6, -1)`, `(9, 0x00ff00ff)`, `(14, 0x020003e5)`; this is not a PNG ID. |
| 997, key `0xb717945f` | Uncompressed indexed BMP, 69 x 63, 8 bits per pixel and 46 palette entries. |

Descriptor 256's animation 2 has 20 steps. Frames 16 through 20 show variant
11, rectangle `(0, 0, 32, 14)`, at vertical offsets -59, -63, -65, -67 and
-69 respectively; frame 21 is empty and produces the blink intervals. The
crop contains the original percentage-number label. Its all-zero variant
orientations require no guessed flip or version-2 UV adjustment. Refer to
the separate descriptor recovery's unchanged-parser comparison for those
metadata fields; this package's animation oracle consumes its decoded JSON
and records its exact hash.

### Animation clock

The 2D object uses `+0x7c` for reverse, rather than the 3D player's `+0xc4`.
It has no geometry-refresh result. Its start and advance state is otherwise
exactly matched by the existing portable `SpriteAnimation` on the tested
valid nonnegative clock inputs. Forward playback uses the entry step's
duration for all transitions in one call; reverse playback reloads duration
after each transition. Finite completion is sticky and subsequent calls
retain the original update behavior.

With the actual 20-step animation, repeat count one, forward playback and
30 ms updates, completion first occurs at tick 56 (1,680 ms). The support
phase still waits until tick 67 (2,010 ms). The effect's completed byte
suppresses its drawing; sprite completion must not become the battle handoff
condition. The final 2D state at the phase handoff is step 19, empty frame 21,
elapsed zero, repeat count one, completed true. The original map wrapper
`0x5ca44` stops advancing the player once it completes. A separate direct
player continuation case verifies the player's defined post-completion
behavior; that case must not be substituted for the actual map scheduler.

### Palette and transparency

The surface subclass installs vtable `0xd3f48`; its `+0x14` initializer is
`0x2058d`, which dispatches the `+0x40` parser at `0x205b7`. Original property
9 dispatch enters `0x20676`, enabling a color key and retaining its value.
`0x2076a..0x2077e` propagates them to BMP load-info byte `+0` and word `+0x10`.

The original palette loop compares the low 24 bits of each palette entry
with the low 24 bits of the key. A match becomes word `0x00ff00ff` (magenta
RGB with zero alpha); other entries receive alpha `0xff`. Key-disabled
indexed loading sets all palette alphas to `0xff`. This is proven with the
actual palette, with source-key conflicts and with synthetic differing high
bytes. The high byte is not part of the color comparison.

Losslessly expanding those keyed entries into top-down RGBA8888 produces
1,498 transparent pixels and 2,849 opaque pixels. Its SHA-256 is
`3df200499e9a45936a3d7ae57d1a16fd847083e5366540e9387ad449c4c51e05`.
This is a derived source-image hash. Original hardware texture-format
selection/conversion, GPU texels, blend state and final on-screen pixels are
outside this proof. The package does not include the bitmap or derived image.

## Recovered draw anchor and remaining native work

`0x5cd14` checks the unit slot, gives active floating-text presentation
priority, and draws the 2D effect only while its completed byte `+0x75` is
zero. It calls `0x5ca94` to project a world point, then `0x5e488` to draw the
current descriptor frame at the integer screen anchor.

For tile coordinates `x, y` and pixel offsets `ox, oy`, the original anchor
arithmetic is, with separate single-precision operations:

```
world_x = ((float(x) + float_bits(0x3dcccccd)) * 32.0f + float(ox)) / 100.0f
world_y = 0.0f
world_z = float(((y + 1) << 5) + oy) / 100.0f
```

The support draw call supplies zero offsets. The point is transformed by
original projection endpoint `0x64cfc`; resulting screen coordinates are
truncated to integers. Reusing the usual unit model origin without the
`+0.1f` x offset and next-tile z boundary would change the anchor. The native
widescreen viewport, logical size and drawable scale still need explicit
mapping. This package does not claim the current tactical camera exactly
reproduces the original projection or easing.

The remaining implementation is a bounded host feature, not a combat rewrite:

1. **Native asset path.** Add the strictly scoped version-1 descriptor and
   indexed BMP/property loader, with the recovered keying. Decode/upload
   once, draw original frame parts and empty blink frames. Keep this 2D path
   separate from M3G descriptor UV adjustments.
2. **Host phase ownership.** Retain the pending `SessionBattle` token and
   its original helper list while showing the tactical map, helper banner,
   independent support focus/cursor and overlay. Defer `prepareBattle` and
   its construction audio until this phase finishes. Zero helpers bypass
   immediately. Pause, menus and session replacement must cancel/preserve
   the correct host state. Original focus easing and first-use tutorial
   persistence remain explicit recovery/integration tasks.
3. **Render-only pre-combat view.** `GameSession::begin_battle` already
   resolves damage/RNG, deaths and active flags before publishing the
   pending battle. Render its saved attacker/defender pre-combat states
   during support cues so a newly dead participant does not vanish early.
   Preserve actual core state and helpers; do not rerun combat or roll RNG.
   Normal `advanceSprites` consumes transform work and can discard a
   newly dead participant's animation, so it cannot be called blindly as
   the support-overlay clock.
4. **Separate presentation identity.** Use support token/slot/revision
   identity for support drawables if actual-presentation gating is added.
   A support drawable must not carry or acknowledge unrelated mission
   camera, world, focus or transform tokens. The current
   `recordPresentation` intentionally refuses queued tactical work while a
   battle exists; add a support-specific return path without weakening it.
   Keep `finishBattleFrame` as the sole path that calls
   `report_battle_finished` after the matching final cinematic frame has
   both successful Metal completion and a real drawable presentation.
5. **Audio and validation.** The native maintenance guard currently uses
   presence of a pending `SessionBattle` to choose the battle site. A
   deferred tactical support phase needs ownership-aware maintenance;
   do not invent support-shot audio. Verify support/no-support, helper
   counts 1/5/6, lethal exchanges, stale/asynchronous frames, pause/resume
   and handoff, then Apple compilation, an actual rendered overlay probe
   and XR/device playthrough observations. The current proof covers none
   of those integrated rendering/device paths.

## Reproduction

Run from the repository root. Use an explicit locally supplied original
binary, libgcc and Unicorn installation. Existing base oracles are only
development tools; no original runtime is added to the app.

Build and run the research fixture:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wconversion -Werror \
  -Inative/research/support_presentation_recovery/fixtures -Inative/include \
  native/research/support_presentation_recovery/fixtures/support_presentation_test.cpp \
  -o /tmp/g1_support_presentation_tests
/tmp/g1_support_presentation_tests
```

The three oracle scripts expose complete arguments with `--help`:

- `oracles/support_presentation_oracle.py` uses
  `analysis_tools/combat/oracle.py`, the original executable/libgcc and
  Unicorn. It regenerates the phase JSON and fixture header.
- `oracles/support_animation_oracle.py` uses
  `analysis_tools/sprite/animation_oracle.py`, descriptor 256's decoded JSON
  and the existing `analysis_tools/sprite/animation_probe.cpp`, compiled
  with `native/src/SpriteAnimation.cpp`. It does not emulate drawing.
- `oracles/support_palette_oracle.py` uses the same sprite base oracle and
  the locally supplied BMP 997. It executes the recorded original blocks;
  it does not emulate a whole bitmap loader or graphics stack.

`MANIFEST.json` pins the exact inspected native/base-oracle files and every
published evidence file. A passing fixture test is not a claim that this
feature is wired into the app.
