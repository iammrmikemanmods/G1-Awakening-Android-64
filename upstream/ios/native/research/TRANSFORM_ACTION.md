# Native form transformation

`TransformAction` translates original unit transition `0x43a54` and command
eligibility predicate `0x4a6c0` into portable C++. It reuses native
`Combat::combat_stats`. No original code, CPU runtime or guest pointers are
needed by the product.

## Admission and original data

The common command gate `0x5300e` rejects a null/inactive unit and any nonzero
action-used flag at unit+`0x2c`. `transform_eligibility` includes the active and
flag checks; the caller must own current-player selection and the stationary
command phase. The original transform bit is `0x8`.

Unit loader `0x5a8f0..0x5a900` maps raw definition scalar field 2 to map+`0x154`,
normalizing any nonzero byte to one. The transform predicate uses that flag.
Type 11's value one permits transformation; type 19's zero does not. A second
form record alone is not permission to transform.

Next, selected-form signed byte 8 must not equal one. The current tile's terrain
category comes from `0x496bc`. Terrain field 0 equal to 99 blocks transformation
only when field 2 is zero. Finally flag `0x2d` must be zero. There is no spent-
movement check here: transformation after movement can be allowed. Move
admission independently requires spent movement zero, so changing form does not
grant a second move.

## Unit transition

`transform_unit` expects the caller to authorize the ordinary action or an
independently established unit undo transition. It copies input, so malformed
new-form data fails before caller state changes.

| State | Original behavior |
|---|---|
| Form `0x0c` | Zero becomes one; any nonzero value becomes zero |
| Recorded transition: `0x2d` | Set to one |
| Recorded transition: `0x2e` | Set to one; pending renderer animation flag |
| Recorded transition: `0x2f` | Cleared to zero |
| Recorded transition: facing `0x4b` | Set to down (one) only when new form byte 13 equals one |
| Level `0x1a` | Preserved |
| Attack low/high and defense | Recomputed from the new form via `0x4328a` semantics |
| Spent movement, action-used flag, tile, offsets | Preserved |

For a recorded transition, global app flag `0xa3` gates the audio request.
Original request `0x2ad12(app,5,7)` is used when the new form is one; event six
is used when it is zero. The native result exposes that optional event without
playing audio. An unrecorded transition changes form/stats but preserves facing
and all transition flags and requests no audio.

Stat calculation reads signed form bytes 3, 4 and 5. Each result is
`base + level * trunc(base/5)`, with original signed-halfword storage semantics.
The implementation calls the independently validated `combat_stats` function.

## Session and rendering boundary

The human dispatcher closes its command UI, calls `0x43abc` (recorded
`0x43a54`), writes selected type to map+`0x28`, and clears flag `0xc7` at
`0x59f86..0x59fa2`. Those map/UI effects remain the session adapter's work.

Renderer `0x6cdae` consumes flag `0x2e` to select animation table row byte 8,
one repeat, with reverse playback when the new form is zero. Ordinary animation
uses `form*4 + facing` instead. While the special animation is unfinished,
`0x6ce0e` doubles animation delta. `SPRITE_RENDERING.md` records the original
shared-loop delta nuance. The session must implement that event/feedback path;
an immediate geometry swap does not establish correct transformation animation.

This component does not implement UI, AI scheduling, transport, whole-action
undo, saves or audio playback. Its unrecorded unit operation does not promise
reversal of later capture, combat, currency or script effects.

## Validation

Strict C++17 compilation with `-O2 -Wall -Wextra -Wpedantic -Werror` passes 17
focused contracts: each eligibility gate, already-spent movement, terrain
exception, nonzero raw flag, recorded/unrecorded state changes, audio selection,
position/action preservation, new-form level stats and atomic malformed-data
failure.

The development-only original comparison contains 128 complete original
`0x43a54` calls with audio disabled, 128 original active/action/`0x4a6c0` gate
cases, and 64 enabled-audio prefixes stopped before the `0x43aae` audio call.
All compared eligibility, form, flags, facing, spent movement, stats and requested
audio values match exactly.

Full transitions execute original ARMv6 `___divsi3` 384 times. It is bound by
changing only the existing lazy-symbol **data** pointer at `0xd26a4`. Original
executable instructions stay read-only and are verified unchanged. There are
zero instruction patches and zero function stubs. Audio playback is neither
executed nor simulated; its enabled prefixes verify the real receiver and
group/event arguments and the same unit changes before the call.

Inputs are explicit synthetic fixtures, covering signed stat-byte boundaries,
levels through 255, both forms, record flags, active/used gates, blocked terrain,
facing reset and stale transition flags. Original executable SHA-256:
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.
Original universal libgcc SHA-256:
`d758fbf1ac952532955d62866bd02df55315c0599f64bdd978ff941fccbdcf83`, using
ARMv6 subtype six.

## Build integration

Add `src/TransformAction.cpp` to the core alongside dependency `src/Combat.cpp`.
Add `tests/transform_action_test.cpp` as `g1_transform_action_tests`, linked to
the core, and register `transform_action_contract` without fixture arguments.
Parent owns CMake. The analysis-tool README provides reproduction commands.
Reference output is `transform_action_validation/original_transform.json`;
source and report hashes are in `TRANSFORM_ACTION.json`.
