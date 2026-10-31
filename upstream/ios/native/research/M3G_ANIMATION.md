# Native rigid M3G animation sampling

`M3GAnimation.hpp/.cpp` adds native decoding and sampling of original controller, track and keyframe data, plus persistent rigid-node poses. This is the next rendering component after `M3G_RIGID_SCENE.md`. It does not yet draw a model on Metal, select the original battle-action clip, control a battle camera, or acknowledge a cinematic as completed.

The supplied Bumblebee model has 28 tracks sharing one controller: 13 translation properties and 15 orientation properties, with LINEAR and SLERP sequences. The model is the same resource 867/member 336/key `0xf4e02223` used in the rigid geometry audit, SHA-256 `aa08afabedbe636d7cedaec480070d3d051b5d2c950c5e40191d8c239f9b48a1`. Its serialized sequence durations include 667, 10,000 and 11,667. Those durations and names are data, not a recovered mapping from gameplay actions to animation intervals.

## Implemented native boundary

`M3GAnimations` reads the real Object3D track bindings, AnimationController fields, AnimationTrack references and KeyframeSequence records. It validates framing, reference type/order, component counts, valid ranges, time ordering and finite input. Float, byte-quantized and short-quantized sequence encodings are decoded. LINEAR 176, SLERP 177 and STEP 180 can be sampled; required SPLINE/SQUAD modes stop explicitly. Rigid translation 275, orientation 268 and scale 270 are supported. Other active properties stop rather than silently failing to animate a material or visibility state.

`sample(world_time, controller_overrides)` returns only active weighted assignments. A null controller, inactive interval or zero weight contributes no assignment. An override must refer to a real decoded controller. Multiple contributions to a property are added in binary32 order; orientation is normalized after accumulation. The implementation does not blend an unstated weight deficit with an invented default pose.

`M3GRigidPose` starts with the actual authored node transforms. Applying no assignment preserves the preceding value. An update validates every target/value before mutating any target. The pose retains authored ancestors and computes column-major world matrices with iterative ancestor evaluation, so a long valid chain does not consume the native call stack. `update_model()` updates the existing rigid surfaces and their bounds without rebuilding vertex or image data. Visibility, runtime alignment, texture animation, morphing and skeletal deformation remain outside this slice.

The renderer-facing flow is:

```cpp
g1::M3GScene scene(model_bytes);
g1::M3GAnimations animation(model_bytes);
g1::M3GRigidPose pose(scene);
auto model = scene.static_model(verified_root_object);

// Controller position/speed/range must come from the actual game request.
pose.apply(animation.sample(world_time, controller_overrides));
pose.update_model(model);
// model.surfaces now carry updated matrices and the unchanged real geometry.
```

Sampling the entire authored timeline is useful for the development probe. It is not permission to label that timeline an idle, attack, transformation or victory animation. The game-specific clip/event binding still has to be recovered before presentation integration.

## Original arithmetic and timing evidence

The original native engine functions were inspected and run on an external development oracle:

| Original routine | Recovered purpose |
| --- | --- |
| `0x9346c` | Keyframe segment choice, wrapping/clamping and weighted sampling |
| `0x932d8` | Valid-range checks and quaternion-key normalization |
| `0x88d0c`, `0x88d44` | Controller position, including long-running loop precision reduction |
| `0x9f03c`, `0x8d988` | Quaternion normalization and two refinement steps for inverse square root |
| `0x9f0e8` | Quaternion interpolation without sign-flipping the authored endpoint |
| `0x8da18`, `0x8ddf8`, `0x8dfcc` | Fixed-point reciprocal and angle calculations used by that interpolation |

The native code preserves the observed floating-point operation boundaries, fixed-point angle polynomial and quaternion correction. Its reciprocal values are generated mathematically: the original 513-entry table at `0xcfbd4` exactly matches a sentinel at index 0 and `floor(2^30 / i)` for indices 1–512. No executable or copied lookup blob is needed by the product.

Coincident keys, a valid range that wraps around its array, and interpolation across a loop boundary are handled explicitly. CONSTANT sampling preserves an original engine detail: at or beyond the duration, `0x9346c` first clamps to duration minus one before its final-key check. Controller mapping includes negative/paused speeds and the original large-time precision reduction; it does not repeatedly accumulate a guessed per-frame sequence delta.

These are native C++ arithmetic and data structures. The original ARM program and Unicorn are used only by `analysis_tools/m3g/animation_oracle.py` to obtain comparison data. They are not compiled into the core or app.

## Validation

The pinned result is `m3g_validation/bumblebee_animation.json`:

- 43 focused native contracts pass, including malformed data/state rejection, original duration boundary, negative loop time, coincident keys, wrapped valid ranges, authored quaternion direction, inactive-state retention and atomic rejected pose updates. A 20,000-node synthetic hierarchy verifies iterative evaluation. Negative first key times are rejected by both serialized and public-sequence inputs.
- All 28 real Bumblebee sequences are sampled at 1,982 key/boundary/intermediate times. All **7,325 sampled binary32 values match the original routine exactly** under the explicitly shared math boundary described below.
- Twelve original controller executions match native binary32 results, including pause/reverse and large positive/negative world-time cases.
- Twenty-four synthetic two-track cases compare 84 weighted scalar results exactly. The original sampler adds both contributions into the same output buffer, with total weights below, equal to and above 1; orientation then uses the original normalizer. These cases exercise weighted translation and quaternion accumulation independently of the actual model's unit-weight controller.
- Six complete hierarchy samples compare 5,088 matrix scalars against independently composed double-precision TRSM matrices using the original sample/normalizer values. Maximum absolute error is approximately `1.172e-6`, below the declared `2e-5` tolerance. This hierarchy comparison is not an original-engine matrix bit-identity claim.

The main program's instruction bytes remain read-only and unchanged in the oracle. ARMv6 libgcc arithmetic executes from the original supplied library. Its integer64 helper uses two declared local non-lazy pointers: offsets `0x9008` and `0x900c` are rebased to the library's own tables at `0x8d14` and `0x8e14`. This is data relocation, not an instruction patch. Omitting those declared bindings gives an invalid oracle and is not accepted as an engine discrepancy.

The external original `sqrtf` and `sinf` calls use the host's real single-precision libm functions. Native comparison uses the same host. Accordingly, exact sample agreement here does **not** establish identical historic iOS libSystem rounding, GPU pixels, or physical iPhone behavior. Initial exploratory double-precision host wrappers produced two one-ULP differences; the final record uses the proper single-precision boundary and pins the actual output hash.

Private original sample data and extracted assets remain outside the repository. The portable test uses synthetic fixtures and requires no copyrighted game data. The repository-eligible report contains counts, hashes and bounded scope, not the model or full sampled asset timeline.

A separate read-only review identified the public negative-time edge and recursive hierarchy-depth risk before freeze. Both were corrected, and the affected native/original and pose comparisons were rerun successfully. No previously captured Apple application source was changed by this module.

## Reproduction and build handoff

Add `src/M3GAnimation.cpp` to the native core. Link `tests/m3g_animation_test.cpp` as `g1_m3g_animation_tests` and register `m3g_animation_sampling_contract` with no arguments. The private-data tool `m3g_animation_probe` uses `tools/m3g_animation_probe.cpp` and takes `MODEL.m3g ROOT_USER_ID`.

The original-reference command requires explicitly supplied private inputs:

```sh
python3 analysis_tools/m3g/animation_oracle.py \
  --binary /private/G1awakening.armv6 \
  --libgcc /private/libgcc_s.1.dylib \
  --bindings /private/bindings.json \
  --model /private/bumblebee_tk.m3g \
  --unicorn-dir /external/unicorn-python \
  --output /private/animation-original.json

python3 analysis_tools/m3g/animation_validate.py \
  --model /private/bumblebee_tk.m3g \
  --original /private/animation-original.json \
  --native-probe /build/m3g_animation_probe \
  --native-test /build/g1_m3g_animation_tests \
  --user-id 838542512 \
  --output /private/animation-validation.json
```

The development-only oracle shares the already audited `analysis_tools/sprite/render_oracle.py` loader and independently parses original data with `analysis_tools/m3g/reference.py`. `animation_validate.py` uses the separate double-precision hierarchy implementation from `validate.py`. Their hashes are included in the result.

## Primary format references and remaining renderer work

The author-issued Nokia M3G 1.1 specification (June 22, 2005), preserved at this documentation mirror, defines the body layout and public controller/track contracts:

- [Binary file format](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/file-format.html).
- [KeyframeSequence](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/KeyframeSequence.html): key ranges, interpolation and quaternion normalization.
- [AnimationController](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/AnimationController.html): active intervals, position/speed and weighted contributions.
- [AnimationTrack](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/AnimationTrack.html): target properties and assignment behavior.

The current tactical Metal shader has different vertex, texture and compositing assumptions from the M3G assets. The next visual component must consume the real vertex colors, homogeneous texture coordinates, texture format/function/filter/wrap, alpha/depth/culling state and matrices through a dedicated renderer. The original battle clip intervals, root/form selection, camera/choreography and outcome-event sequencing must also be connected. This checkpoint makes the authentic rigid animation data usable; it does not claim that those remaining presentation boundaries are complete.
