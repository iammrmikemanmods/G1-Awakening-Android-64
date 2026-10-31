# Native Sprite3D arithmetic and common draw ordering

`M3GSprite3D.hpp/.cpp` implement the recovered billboard arithmetic as portable
C++17. The module executes native code only. It has no guest CPU, original
instruction, compatibility-runtime, GPU, or platform dependency.

This checkpoint adds new files only. Scene decoding/evaluation, Camera,
Animation, Metal, NativeApp and CMake were not edited. Integration is a separate
step owned by the renderer work. The original reference recovery remains frozen
in `SPRITE3D_RECOVERY.md` and `sprite3d_original_projected_draws.json`.

## Public contract

`project_m3g_sprite3d(const M3GSprite3DInput&)` takes explicit, evaluated inputs:

- Column-major model-to-world and camera-view matrices.
- The original GL projection matrix and its original type (48 generic, 49
  parallel, 50 perspective).
- Viewport and logical image dimensions in pixels.
- Crop x/y and signed width/height, plus the scaled and appearance-present flags.

The inline `project_m3g_sprite3d_from_camera` convenience overload obtains the
view matrix from an explicitly supplied camera-to-world pose through the
existing checked `m3g_camera_view_matrix`. It does not choose a camera, change
aspect, or infer a viewport. The reference comparisons use the explicit view
boundary recorded by the original-execution recorder.

The result distinguishes a draw from no appearance, zero crop, no image
intersection, near-zero homogeneous projection, rejected depth, and a rectangle
outside the viewport. Invalid or unsupported numeric inputs throw `FormatError`.
The exact rejection stage was not retained by the original numeric fixture, so
the differential test compares **draw versus no draw**; native rejection enums
are separately checked by focused contracts.

A drawn result contains:

- The original GL NDC origin, before Metal depth conversion.
- The original pre-clipping rectangle and fixed-point source crop bounds.
- Crop mirror flags.
- Four final 2D position offsets and UV pairs, in signed 16.16 form.

The strip order is top-left, bottom-left, top-right, bottom-right. Add each
position offset divided by 65536 to the NDC origin's x/y. Every vertex retains
the origin's z and has w=1. Apply Metal's `z=(z+w)/2` only after this original
geometry calculation. No replacement geometry, invented scale, Y flip or
half-texel offset is supplied.

The Scene caller remains responsible for evaluated inherited visibility and
alpha, drawable scope admission, image/appearance binding and GL-equivalent
render state. The helper neither changes those states nor selects animations or
consumes RNG. A finite zero-axis model scale produces the original degenerate
rectangle; it is not replaced with an arbitrary visible size.

### Explicit image boundary

The initial helper supports **power-of-two image dimensions from 1 through
1024** on each axis. All 54 required sprites in battle resources 486/1212 use
64×128 or 128×128 images and meet this boundary.

The original Image2D block builder `0x91004` rounds a non-power-of-two final block
up to power-of-two dimensions at `0x91056..0x910bc`, and selects padded/block
storage at `0x910bc..0x910e2`. The physical block dimensions affect UVs. This
module explicitly rejects non-power-of-two images until texture padding and
physical block dimensions are represented in the native integration. It also
rejects multi-block images. It must not silently normalize those images using
their unpadded logical dimensions.

Crop magnitudes retain the original 1024-pixel bound. Model/view inputs must be
finite affine transforms. Generic projections use the original full point
transform; typed projections retain the original optimized layout and reject
incompatible off-diagonal values. Viewport integers are positive and bounded to
the exactly representable binary32 integer range used by this API.

## Arithmetic fidelity

The source reconstructs original `0xb7520` projection and `0xb5998` single-block
rectangle clipping. The original matrix/vector helper boundaries are documented
in the recovery note.

The native implementation preserves each binary32 multiply/add/divide using
explicit rounded temporaries. In particular, it preserves the endpoint
**addition followed by subtraction** when measuring a transformed axis; using
only the model matrix column norm would lose original rounding. Homogeneous
division uses a reciprocal multiplication and the original near-zero/near-one
threshold. Magnitudes use the explicitly declared platform square-root
boundary, matching the original reference recorder's host `sqrtf` boundary.

Fixed-point conversion is the original truncation of
`float32(float32(value*65536)+0.5)`, including negative-coordinate asymmetry.
Clipping uses checked signed 64-bit products and division. Negative fixed-point
products use explicit floor division; the code does not depend on signed
right-shift behavior. Float-to-integer and intermediate arithmetic overflows
are rejected before undefined behavior. A read-only review identified a finite
input whose homogeneous w could overflow to infinity; the final source rejects
that case and includes its regression.

## Shared mesh and sprite ordering

`m3g_draw_queue_key` reproduces the signed original key from layer,
opaque/transparent classification, node world origin and camera world origin.
The caller supplies classification; this module does not replace the original
Appearance/material/vertex/image opacity tests with a guessed rule.

`m3g_draw_queue_order` returns indices into a common request vector. It
reconstructs original `0xb4f5c` linked insertion, including its last-entry and
same-node insertion-anchor behavior. It does not create separate mesh and
sprite batches. A caller supplies stable node identity and the original
grouping-key meaning. Opaque mesh entries use Appearance creation serial;
transparent entries use zero. These are not texture resource IDs. The required
ALPHA-blended sprites use the transparent path and grouping key zero.

The signed layer range is -63..63. Construction uses unsigned shifts and a bit
copy into the signed key. Transparent sorting uses the original binary32
squared-distance bits, quantized by shifting eight bits, rather than view Z.
Same-key different-object order and the original opaque submesh anchor rule are
covered by the frozen original queue fixture.

## Exact original comparison

The C++ test needs no original IPA, executable, image pixels or emulator. Its
compact text fixture is generated only from the frozen original-execution JSON.
`m3g_sprite3d_cases_provenance.json` records that source, the executable/libgcc
hashes, generator hash and fixture hash. Expected outputs are never generated
by the native implementation.

| Validation | Result |
| --- | ---: |
| Original renderer calls compared | 142 |
| Draw/no-draw outcomes | 142 exact |
| Drawn rectangles | 47 |
| Original NDC origin binary32 values | 141 exact bit patterns |
| Fixed16 rectangle, crop, final position and UV values | 1,128 exact integers |
| Mirror flags | 94 exact |
| Shared queue sequences / entries | 4 / 26 |
| Queue keys and resulting order indices | 52 exact |
| Total original numeric/boolean comparisons | **1,557 exact** |
| Separate native contract checks | **30 passed** |
| Undefined-behavior sanitizer | Same original comparisons and 30 contracts passed |

The broader JSON comparison reports 1,705 checks because it additionally checks
case/queue names and corpus counts. There are no mismatches. The 142 calls
comprise 34 synthetic cases and 108 authored-node/camera seeds covering all 54
required nodes. Those authored poses are explicit serialized inputs, not a
claim about an actual game frame's camera or animation time.

Focused contracts cover malformed/nonfinite transforms and projections, invalid
dimensions, non-power-of-two images, crop range violations, vector and
homogeneous-projection overflow, fixed-point conversion overflow, layer limits,
zero queue identities, rejection reasons, zero-axis geometry and queue ties.

Both optimized and sanitizer builds use strict C++17 warnings. The code and
fixtures are small; no full host/runtime rebuild was performed. The actual
results and source/executable hashes are saved in
`sprite3d_native_validation.json`.

## Required sampler and texture-environment state

The numeric geometry helper does not configure a GPU sampler. A narrow static
inspection closes the required sampler state for integration:

| Original call path | Exact state |
| --- | --- |
| `0xb5cfa..0xb5d18` → `0xb3e36` | Arguments wrap S=240, wrap T=240, level filter=208, image filter=210 |
| `0xb411c..0xb4140` | Wrap 240 maps to `GL_CLAMP_TO_EDGE` (`0x812f`) for S/T |
| `0xb4144..0xb4158` | Image filter 210 maps magnification to `GL_NEAREST` (`0x2600`) |
| `0xb4162..0xb41d6` | Level 208 / image 210 maps minification to `GL_NEAREST`, without a mip filter |

For required RGBA format 100, full inherited alpha uses texture environment
REPLACE (`0x1e01`), while finite alpha below 1 uses MODULATE (`0x2100`). The
branches are at `0xb7d6c..0xb7db4` and `0xb80b4..0xb80c2`. Original RGB format
99 also selects REPLACE; this does not extend the current helper's image-upload
scope. The original compact recorder deliberately prebound the texture cache
and did not retain TexEnv arguments, so these sampler/environment facts are
static original-callsite evidence, not a GPU or recorded-upload test.

## Reproduction and integration

From the `g1_port` directory, build the dependency-free focused test:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Inative/include \
  native/src/M3GSprite3D.cpp native/tests/m3g_sprite3d_test.cpp \
  -o /tmp/g1_m3g_sprite3d_tests
/tmp/g1_m3g_sprite3d_tests native/tests/m3g_sprite3d_cases.txt
```

The optional probe uses the same local input format and emits JSON:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Inative/include \
  native/src/M3GSprite3D.cpp native/tools/m3g_sprite3d_probe.cpp \
  -o /tmp/m3g_sprite3d_probe
python3 analysis_tools/m3g/sprite3d_validate.py \
  --probe /tmp/m3g_sprite3d_probe \
  --native-test /tmp/g1_m3g_sprite3d_tests \
  --output /tmp/g1_sprite3d_validation.json
```

A sanitizer build changes `-O2` to
`-O1 -fsanitize=undefined -fno-sanitize-recover=all`; pass that test executable to
the validator with `--ubsan-test` to retain its observed output. The Python
validator verifies the original reference hash before regenerating a fixture.

Suggested CMake handoff, **not applied in this checkpoint**:

- Add `src/M3GSprite3D.cpp` to `g1_resources`.
- Add `m3g_sprite3d_probe` from `tools/m3g_sprite3d_probe.cpp`.
- Add `g1_m3g_sprite3d_tests` from `tests/m3g_sprite3d_test.cpp`.
- Register `m3g_sprite3d_original_function_vectors`, passing the absolute source
  path of `tests/m3g_sprite3d_cases.txt` to the test executable.

The source-data comparison remains a development tool. Ship the native module,
not the reference executable or Unicorn package. This checkpoint does not claim
Metal rasterization, rendered-pixel parity, Apple/device execution, complete
scene traversal parity, animated crop, padded/multi-block textures, or cinematic
playthrough validation. The next renderer step is to feed evaluated scene state
into this frozen interface and compare the resulting integrated scene.
