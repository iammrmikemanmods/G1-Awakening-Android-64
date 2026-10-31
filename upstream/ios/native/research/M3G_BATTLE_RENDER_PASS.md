# Original ordinary battle submissions and solid-background clear

## Result and evidence boundary

The original ordinary battle uses **ordered, separately flushed submissions**:
the retained scene World, A, B, shot/hit effects0..3, then death smoke. The
retained World's background is cleared once before its geometry. The later
individual-node submissions preserve the target contents; they do not repeat
the World's background clear. A single queue merged across these instances
would change the original ordering.

`analysis_tools/m3g/battle_render_pass_oracle.py` executes unchanged `0x3cc9c`
with recorded public object/graphics boundaries for **14 visibility and
optional-object cases**. It separately executes unchanged `0xb5fe8` and
`0xb4cb4` for **44 no-image background cases**, including all three ordinary
scene colors. The latter runs the original ARM libgcc conversion instructions
and records the actual GL clear/mask/scissor arguments. No instructions are
patched, no actual GL context is used, and original executable/libgcc code
bytes remain unchanged.

The fixture is `native/research/m3g_validation/original_battle_render_pass.json`.
The adjacent JSON manifest records hashes, exact input identity, boundaries and
reproduction. The main phase/draw-visibility contract is separately frozen in
`M3G_BATTLE_CONTROLLER.md`; the per-draw depth state is in `M3G_DEPTH_STATE.md`.

## Submission sequence from `0x3cc9c`

The original begins its target with `0x271d0(graphics,target,viewport,1,0)` at
`0x3cd10`. The viewport comes from cinematic `+0x64`. The recorder substitutes
the target binding itself, so framebuffer format, target allocation and
platform presentation are not inferred from the synthetic target.

| Order | Original call sites | Operation |
| --- | --- | --- |
| 1 | `0x3cd14..0x3ce96` | Read and save render-enable for A, optional B, each existing effect, and optional smoke. Set those roots disabled. |
| 2 | `0x3cea4` → `0x429ec` | Render the retained scene World while every attached participant/effect root is disabled. |
| 3 | `0x3ceb0..0x3cf16` | Create temporary Transform; obtain the scene's active Camera; compute its transform relative to the scene World; set that Camera and transform for subsequent individual submissions. |
| 4 | `0x3cf44..0x3cf6e` | Restore A's saved render-enable, get A's transform to scene, submit A. |
| 5 | `0x3cfb6..0x3cfde` | Restore/transform/submit B if its pointer exists. |
| 6 | `0x3d030..0x3d08e` | For effect indices0,1,2,3 in order, restore/transform/submit each existing wrapper. |
| 7 | `0x3d100..0x3d140` | Restore/transform/submit smoke if its pointer exists. |
| 8 | `0x3d14a`, `0x3d1d6` | Finish target and finish surrounding context. |

The individual submit calls still occur when a saved enabled flag isfalse;
the renderer's normal traversal then handles that disabled root. The reference
asserts that all saved visibility is restored after the function returns.
A null effect array, individual missing effects, absent B and absent smoke
are covered. A is required by this ordinary helper and is not synthesized.

The branch at `0x3d14e` can render a second offset viewport when cinematic
`+0x74` is non-null, using `+0x5c` rather than the ordinary retained World.
The creator of that sprite is the setup-false branch at `0x3fe0c..0x3fe12`
through `0x4023e` and `0x4039e..0x403a8`; ordinary `0x40a98` supplies setup-true.
The present reference keeps this separate viewer/secondary-scene fieldnull.
It does not claim that the extra branch is part of an ordinary two-unit battle.

## Public graphics identity and actual render dispatch

This is a concrete dispatch chain, not a name inferred from virtual-slot
position. Engine factory type8 (`0x27300` / `0xb0414`) selects the public
graphics wrapper initialized by `0xab9c4`. Its relevant targets are:

| Public slot | Installed target | Internal renderer route |
| --- | --- | --- |
| `+0x58` | `0xabeac` | `0x8ebfc` → renderer `+0x28` → `0xb8bc0`, individual Node |
| `+0x5c` | `0xabf24` | `0x8f7ee` → renderer `+0x2c` → `0xb8a6c`, World |
| `+0x64` | `0xabfe8` | Camera and supplied Transform assignment |

The renderer is created through `0x8ef6a` → `0xb018a` → `0xb91fe` /
`0xb8fa0`, with installed primary vtable `0xd604c`.

World rendering at `0xb8a6c` validates the active Camera and ready target,
then calls renderer `+0x18` (`0xb5fe8`) with World background `+0xa0` at
`0xb8b5e..0xb8b6a`. It sets the Camera, submits the World's light list, enqueues
that World at `0xb8b9a` (`0xb5f94`) and flushes at `0xb8ba0` (`0xb87c8`).

Individual-node rendering at `0xb8bc0` has its own enqueue at `0xb8c5a` and
flush at `0xb8c62`, then restores temporary traversal transforms. It has no
call to `0xb5fe8`. Its input Transform is the instance-to-scene transform
recorded above. Consequently the scene depth/color results remain available
to each following participant/effect submission.

This proves the submission/flush boundary. It does not introduce a new
cross-instance sorting rule or replace the already recovered within-submission
Mesh/Sprite3D queue policy.

## Background object and actual scene values

For a non-null Background, `0xb5fe8` reads:

| Internal offset | Meaning |
| --- | --- |
| `+0x20` | Packed ARGB color, with red in bits16..23 |
| `+0x24` | Optional image pointer |
| `+0x40` | Color-clear enable byte |
| `+0x41` | Depth-clear enable byte |

The actual scene inventory was independently decoded in
`research/native_m3g/render_scene_v4/render_pass_inventory.json`. All three
ordinary scene Worlds refer to a solid Background with no image and both
clear flags enabled:

| Scene resource | Background object | RGBA bytes | Original `glClearColorx` RGBA integers |
| --- | --- | --- | --- |
| 486 | 6 | 96,156,255,255 | 24672,40093,65536,65536 |
| 534 | 6 | 168,0,216,255 | 43177,0,55513,65536 |
| 1212 | 6 | 96,156,255,255 | 24672,40093,65536,65536 |

Each output integer represents a16.16 value. A native clear color can preserve
the original requested value by dividing these integers by65536. The conversion
is not simply an unrounded channel/255 double: original `0xb608c..0xb6108`
performs float32 integer conversion, multiply by float bits `0x3b808081`
(rounded1/255), multiply by float32 65536, add float32 0.5, and truncate to a
signed integer. The recorder executes those helpers, including boundary colors.

With no Background object, the original selects transparent black and enables
both color and depth clear (`0xb6940..0xb694e`). With a non-null Background but
color-clear disabled, its stored color is not converted. Background images
take a different, substantial path at `0xb611a`; that path is explicitly
outside this solid-background contract and is not required by the three
ordinary scenes above.

## Exact GL clear and depth setup

The import identities are verified against the original Mach-O binding table:

| Stub | Symbol |
| --- | --- |
| `0xba690` | `glClear` |
| `0xba69c` | `glClearColorx` |
| `0xba6a8` | `glClearDepthx` |
| `0xba6e4` | `glColorMask` |
| `0xba744` | `glDepthMask` |
| `0xba75c`, `0xba78c` | `glDisable`, `glEnable` |

`0xb4cb4(renderer,colorClear,alphaWrite,depthClear,r,g,b,a)` performs these
operations, preserving their order:

1. If neither clear flag isenabled, return without a GL clear.
2. For color clear, establish the stored color-clear scissor state, call
   `glClearColorx`, then `glColorMask(1,1,1,alphaWrite)`.
3. For depth clear, call `glDepthMask(1)`.
4. Call `glClear` with `0x4000` for color, `0x0100` for depth, or `0x4100` for both.

The alpha-write input comes from Graphics context byte `+0x62`; it is not
implicitly the Background's alpha value. Both alpha-write values are covered
by the reference. The synthetic recorder does not claim which framebuffer
format the original iOS host selected. The three actual colors have alpha255.

For a color clear, renderer flags `+0x228` bit0 select whether scissor should be
enabled, and bit2 tracks the current GL scissor state. The helper emits
`glEnable/Disable(GL_SCISSOR_TEST=0x0c11)` only when that cached state changes.
For a depth-only clear it disables scissor if needed before clearing. The
recorded flag combinations0,1,4,5 cover these branches. Subsequent geometry
has its own scissor-state restoration via `0xb4c78`; a full-target Metal load
action is not a substitute for a required partial-viewport clear.

The renderer's target setup `0xb577c` sets
`glClearDepthx(65536)` at `0xb57ba..0xb57be`, i.e. depth1.0. It enables or
disables global `GL_DEPTH_TEST=0x0b71` from Graphics context `+0x6c` at
`0xb57f6..0xb580a`. The Graphics constructor initializes that byte totrue at
`0x8ee32..0x8ee34`. This closes the default depth-enabled context underlying
the separately recovered per-appearance `LEQUAL` / `ALWAYS` selection and
independent depth-write behavior. The ordinary target begin call uses `(1,0)`,
which skips the context-option override at `0x2726c..0x27282`.

These are original-state calls and requested values. They do not establish
bit-identical hardware blending, sRGB/framebuffer conversion, antialiasing,
or depth precision on Metal; those remain native renderer/device validation.

## Reproduction and delivery boundary

Required: Python3, Unicorn2.1.4, the exact audited ARMv6 game image and fat
ARM libgcc inputs, and the repository's shared
`analysis_tools/sprite/render_oracle.py`. No downloaded payload or executable
is included with this report. The runner is development-only and is never
built into or executed by the native app.

```sh
python3 analysis_tools/m3g/battle_render_pass_oracle.py \
  --binary /path/to/G1awakening.armv6 \
  --libgcc /path/to/libgcc_s.1.dylib \
  --unicorn-dir /path/to/unicorn-python \
  --output /tmp/original_battle_render_pass.json
```

The synthetic pass recorder supplies successful transform queries without
calculating matrices and records actual public render submission arguments.
The separate background recorder executes original channel conversion and
clear branching but replaces GL with an event recorder. Neither experiment
claims actual asset loading, a native rendered battle, or device performance.
