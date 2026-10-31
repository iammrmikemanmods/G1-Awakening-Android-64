# Native animated node and camera state

This component extends the prepared M3G sampler with persistent alpha,
visibility and camera field-of-view state. It is a portable native C++ state
layer. It does not select the battle camera, run the battle phase controller,
submit Metal draws or acknowledge gameplay presentation completion.

## Native contract

`M3GScenePose` takes a validated `M3GScene` and the decoded camera map from
the same file. Its byte-input convenience constructor performs those reads.
All serialized Camera objects must be represented in the map; an unrelated,
missing or malformed camera fails. Runtime alignment remains unsupported and
fails explicitly.

`apply(samples)` accepts the recovered properties:

| Property | Shape | Assignment |
| --- | --- | --- |
| ALPHA 256 | one float | Local float alpha, ordered finite clamp to `[0,1]`; negative zero survives |
| FIELD_OF_VIEW 264 | one float on Camera | Original camera-mode-dependent assignment below |
| ORIENTATION 268 | four floats | Existing rigid quaternion state |
| SCALE 270 | three floats | Existing rigid scale state; the sampler expands a one-component scale |
| TRANSLATION 275 | three floats | Existing rigid translation state |
| VISIBILITY 276 | one float | Local enable is `value >= 0.5f` |

Every target, width and finite value is validated before any assignment.
An unsupported item therefore cannot partially change a frame. The native
transaction is an implementation property, not a claim that the original
dispatcher provided transactions. An empty sample list retains the preceding
state.

The sampler accumulates each active scalar contribution in original binary32
order, without normalization by total weight or contribution from the previous
value. The scalar setter runs after that sum. Inactive, zero-weight and
null-controller tracks generate no sample. Existing native validation continues
to reject negative or nonfinite controller weights; original diagnostic cases
with those inputs are not promoted to accepted native behavior.

### Camera field assignment

PARALLEL 49 updates its view height. GENERIC 48 and PERSPECTIVE 50 update the
stored field of view. All modes replace a value at or below zero with float
bits `0x377ba882`. Only non-PARALLEL modes replace a value at or above 180 with
`0x4333ffff`. A strictly positive `1e-6f` stays `1e-6f`: this is not an epsilon
minimum clamp.

`M3GCameraProjection::parameters[0]` contains the updated field. In GENERIC
mode it is retained metadata; its existing generic matrix and projection type
are unchanged. Aspect, near and far remain unchanged. Original dirty bytes are
cache mechanisms, so this component recomputes evaluated state instead of
exposing invented game fields corresponding to those bytes.

`evaluated_nodes()` supplies the current model matrix, inherited enable,
inherited float alpha and the node's own scope. It traverses ancestor chains
iteratively. Parent enable and alpha are inherited; scope is tested at the
drawable and is not implicitly intersected with ancestors. This distinction
comes from the original renderer traversal `0xb5d78`, documented in
`m3g_validation/SPRITE3D_RECOVERY.md`. The method rejects nonfinite evaluated
matrices. Camera projection/view validation remains in `M3GCamera`.

## Original comparison

The source evidence and original instruction addresses are in
`M3G_SCALAR_PROPERTIES.md`. Its fixture
`m3g_validation/original_scalar_properties.json` has SHA-256
`5c19ffbd5705f3020cc0fc418f915c60fbe3b1baf3dc2461b95e9e3359af6252`.

The native test passes **60 finite direct-setter cases and 10 accepted
dispatcher cases**, making **365 exact observable-field comparisons**. These
include weighted sums, positive sub-epsilon FOV, the inclusive visibility
threshold, camera type preservation, unchanged clipping/aspect fields and
retention when no track contributes. The synthetic serialized STEP tracks in
the dispatcher test pass through `M3GAnimations::sample` before `apply`.

The remaining **20 original diagnostic vectors** are separate native rejection
checks: 18 nonfinite direct values and two invalid controller weights. They
are not counted as successful native reproduction of the original diagnostic
assignments. Original cache flags and validity return values are not represented
or claimed by this state component.

The complete focused test reports **590 checks**, including rejection atomicity,
camera-set identity, unsupported shapes/properties, iterative 20,000-Group
inheritance, hiding/revealing descendants and local drawable scope. The existing
43 animation contracts also pass. Their unsupported material fixture now uses
AMBIENT_COLOR 257 because ALPHA 256 is implemented; all previous supported
interpolation and rigid assignment checks are unchanged.

`analysis_tools/m3g/property_fixture.py` deterministically converts the captured
numeric observations to `tests/m3g_property_fixture.hpp`. No original ARM code,
private resource bytes or development emulator is linked by these tests or by
the native product.

## Real asset integration boundary

The native pose probe evaluates resources **486, 534, 1212 and 867**: all three
battle scenes and Bumblebee. All **24 poses** at times
`0, 666, 1666, 3333, 6666, 11666` milliseconds pass, covering **263 nodes and
38 cameras**. Every resulting camera view/projection product is finite.
The actual asset input hashes and per-time property counts are retained in
`m3g_validation/scene_pose_actual.json`.

Scene 534 now applies its two alpha and two visibility bindings as well as
five active field-of-view targets; each other battle scene has five active
field-of-view targets. This removes the prior active-scalar-property rejection
from native state evaluation. The Sprite3D geometry and shared draw queue are
separate components; successful pose evaluation does not prove that those
nodes have been drawn. The times here are explicit sampler inputs, not a claim
that the original battle controller selected these views at those moments.

This check uses the corrected prepared SPLINE/SQUAD sampler. Its earlier
original arithmetic comparisons remain in `M3G_SPLINE_SAMPLER.md`. This new
real-asset probe is not another original-code execution or historical iOS
libm/GPU comparison.

## Build and reproduction

Add `src/M3GScenePose.cpp` to the native core. Add test
`tests/m3g_scene_pose_test.cpp` (no arguments) and optional tool
`tools/m3g_scene_pose_probe.cpp`, linked to the same native core and zlib as the
existing M3G tools. The focused local checks used C++17, `-O2 -Wall -Wextra
-Wconversion -Werror`.

```sh
python3 analysis_tools/m3g/property_fixture.py \
  --input native/research/m3g_validation/original_scalar_properties.json \
  --output native/tests/m3g_property_fixture.hpp

python3 analysis_tools/m3g/scene_pose_validate.py \
  --manifest /private/asset_manifest.json \
  --extracted /private/extracted \
  --probe /build/g1_m3g_scene_pose_probe \
  --resource 486 --resource 534 --resource 1212 --resource 867 \
  --output /private/scene-pose-actual.json
```

The immutable v3 patch is based on sampler v2 and is applied after that patch.
Existing Apple source, the shipped scene integration and frozen live sampler
files were not modified during this isolated implementation. A peer read-only
review found no concrete assignment, atomicity or hierarchy blocker. Apple
compilation, GPU output, XR timing and complete cinematic behavior remain
unverified by this slice.
