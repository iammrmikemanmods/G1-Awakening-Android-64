# Native battle camera routing and ordered pose effects

`M3GBattleCamera` implements the recovered camera-ID routing and the ordered
pose operations around a camera switch. It does not invent a view matrix,
choose a battle phase, load a substitute model, or acknowledge a rendered
presentation. The host must resolve actual Camera nodes and apply these
operations to their real native pose state.

The module is additive. It does not change the frozen Scene, sampler,
scene-pose, shared draw-queue, Metal adapter or current app files.

## Original execution boundary

The development recorder `analysis_tools/m3g/battle_camera_oracle.py` executes
unchanged original `0x3d9d0`, `0x3d51e`, the name-hash functions and their
smart-pointer helpers. The original main image and ARMv6 libgcc text remain
read-only and are byte-checked after every call. No instructions are patched.

The supplied synthetic public graph has a scene World, model A/B camera
chains, named scene cameras and an outgoing camera with zero, one or two
ancestors. Public find/query/getChild/getParent/getCamera/setCamera operations
are explicit graph callbacks. Ownership, identity, allocation and bounded C
string functions are also substitutes. Public animate and Translate endpoints
are **recorded**, not executed by this fixture. The sampler, matrix, hierarchy
binding and rendering components have their own separate evidence.

The fixture covers 208 combinations of 13 camera IDs, both forms for each
side, and independent presence/absence of both embedded model cameras. Another
13 calls cover an absent B model, a null outgoing active camera and adjustment
bytes 7/255. That is **221 original calls**, **5,192 public graph events** and
**442 actual entries into the original propagation helper**. The fixture does
not establish a full original asset load, animation subtree traversal, GPU
execution or the game's real phase-to-camera schedule.

## Exact camera routing

The route helper takes the original camera ID, two unit-form bytes and the
two supplied setup flags from cinematic `+0x6c/+0x6d`. A nonzero form selects
the vehicle camera where the table permits it. These flags come from a
separate model/head setup calculation; they must not be guessed from form or
unit name.

| Camera ID | Side used for the embedded camera and clock | Scene fallback name | Embedded choice |
| --- | --- | --- | --- |
| 0 | A | `cam_auto_takehit` for A form0; otherwise `cam_auto_car` | A form |
| 1 | A | `cam_auto_shoot` | A form |
| 2 | A | `cam_auto_death` for A form0; otherwise `cam_auto_car` | A form |
| 3 | B | `cam_decpt_takehit` | B form |
| 4 | B | `cam_decpt_shoot` | B form |
| 5 | B | `cam_decpt_death` | B form |
| 6 | A | `Stat_Camera` | Robot, regardless of form |
| 7 | **A** | `d_Stat_Camera` | **A robot**, regardless of form |
| 8 | B flag stored, no model-camera lookup | `cam_rot` | Bypassed |
| 9 | A | `cam_auto_setup` | A form |
| 10 | B | `cam_decpt_setup` | B form |
| 11 | A | `cam_auto_zoom` for A form0; otherwise `cam_auto_car` | A form |
| 12 | B | `cam_decpt_zoom` | B form |

ID7's A-side behavior is explicit in the original branch at
`0x3db52..0x3db76` and in the recorded calls. Its fallback name does not
authorize changing it to model B. ID8 skips the model lookup at
`0x3db8a..0x3db90` and clears the model-camera reference.

Embedded lookup calls `0x2701c`, which appends `Transform` to `roboCAM` or
`vehiCAM`. `M3GNodeBindings::model_camera` implements the recovered named
transform → child0 Group-compatible → child0 Camera chain. By contrast, the
scene fallback calls **raw** hash helper `0x26b94` at `0x3e048`; it does not
append `Transform`, and the found object is queried directly as a Camera.

The route supplies the relevant setup byte for IDs0–5 and9–12. IDs6–8 use
zero. The original first propagation call receives this newly requested
flag, even though it is still operating on the outgoing camera.

## Ordering and clock transfer

`m3g_battle_camera_switch` returns a plan, not a mutation or a completion:

1. Propagate the outgoing active camera. If the previous cached model-camera
   reference was non-null, use the old side's model clock; otherwise use0.
2. Set the actual newly resolved camera as active.
3. Propagate the incoming camera. If a real embedded model camera was found,
   use the new side's model clock; otherwise use0.

The original calls occur at `0x3da8c`, the World active-camera slot call at
`0x3e072`, and the final `0x3d51e` call at `0x3e11a`. The original
clock loads and bytes are preserved; the module does not clamp signed time,
advance a clock, select a clip, or substitute a nonzero camera time for a scene
fallback.

For each propagation, `0x3d51e` obtains the active camera and its parent. A
null camera or a camera without a parent produces no pose operation. If a
grandparent exists it animates that grandparent; otherwise it animates the
parent. This is a scoped Object3D animation call at the specified time, not
permission to animate every unrelated resource at that time.

### The camera adjustment is additive

When the supplied flag is nonzero and the cached model-camera reference is
null, the helper invokes the immediate parent's public slot `+0x64` with
the exact float words `(0,0,0x40000000)`, after the animation call. The slot is
**Translate**, so the native effect adds **(0,0,2)** to the current local
component translation. It does not replace the translation and does not modify
the separate generic matrix used for model placement.

The semantic linkage is independently established in
[M3G_CAMERA_TRANSLATE_SLOT.md](M3G_CAMERA_TRANSLATE_SLOT.md):
`0x4295c → public+0x64 → 0xb2e9c → 0xa7440 → 0xd2474 →
CssVector3D::operator+= (0xa76ae)`. The known delta passes the original
nontrivial-delta threshold and float conversion unchanged. An earlier draft
labelled the recorded endpoint as an absolute setter; that label was corrected
before this module was frozen. The callback's numeric arguments alone were
not used as proof of its mutation semantics.

## Host contract and validation

Node references contain a native instance namespace and object index. The
caller supplies actual camera/parent/grandparent links, including any real
cross-instance attachment. Both ancestry inputs must describe one consistent
graph: conflicting parent assignments, an explicit no-parent conflicting with
a parent, and cycles within or across the two chains are rejected before a
plan is produced. A required selected camera must exist. ID8 cannot be reported
as an embedded-model-camera success.

`m3g_battle_camera_propagation` also exposes the recovered direct helper for
callers that already know the correct time and adjustment flag. The host must
perform an additive local component translation after the scoped animation;
applying the delta to world translation or treating it as an absolute sampled
property is incorrect.
When the outgoing and incoming cameras share that parent, both ordered
Translate effects remain in the plan. They must not be merged or deduplicated;
the intervening animation may itself change the component translation.

The optimized and undefined-behavior-sanitized native tests each pass **9,668
checks: 9,645 original-field comparisons and 23 native contracts**. They compare
the same 221 original cases and their ordered effects, including route names/hashes,
model hash/bypass, side, setup byte, old/new time and state, and exact effect
targets/time/delta words. The test deliberately checks the ID7 anomaly. Native
invalid-input contracts are reported separately from original executions.
The machine-readable native result and freeze manifest give the final counts
and source hashes.

The independent actual-file inventory checks raw fallback names in battle
scenes486/534/1212: **36 lookups find Camera5 nodes**. `Stat_Camera` and
`d_Stat_Camera` are absent in all three (six missing lookups). Missing values
remain missing; no camera is fabricated. The separate 27-file model binding
audit finds each ordinary table model's robot camera, so that inventory does
not by itself prove a failure in the different single-model/stat-screen path.
It also does not establish a runtime phase selection.

## Integration and reproduction

Add `src/M3GBattleCamera.cpp` to the native core with the frozen
`M3GBindings.cpp`. The focused test is `tests/m3g_battle_camera_test.cpp` and
has no arguments. `m3g_battle_camera_fixture.hpp` is generated development test
data; it is not an original executable or an in-app CPU runtime.

```sh
python3 analysis_tools/m3g/battle_camera_oracle.py \
  --binary /private/G1awakening.armv6 \
  --libgcc /private/libgcc_s.1.dylib \
  --unicorn-dir /development/unicorn-python \
  --output /private/original-battle-camera.json

python3 analysis_tools/m3g/battle_camera_fixture.py \
  --input /private/original-battle-camera.json \
  --output native/tests/m3g_battle_camera_fixture.hpp

python3 analysis_tools/m3g/battle_camera_inventory.py \
  --manifest /private/asset_manifest.json \
  --extracted /private/extracted \
  --output /private/scene-fallback-inventory.json
```

The recorder and its Python/Unicorn dependency are development tools only.
There is no original CPU interpreter, JIT or original executable in this native
module. Actual Apple compilation, native pose-effect integration, Metal output,
physical XR testing and full battle/playthrough parity remain separate gates.
