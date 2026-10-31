# Original battle model and timeline bindings

This is a bounded source-evidence report for the native renderer work. It does
not implement or claim a working 3D battle presentation. The original program
was inspected without modification. Two small original timeline helpers were
also executed as development references, with synthetic object endpoints.
No original CPU execution belongs in the native product.

## Inputs and evidence

- Original ARMv6 image SHA256:
  0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd.
- Original universal libgcc SHA256:
  d758fbf1ac952532955d62866bd02df55315c0599f64bdd978ff941fccbdcf83.
- Existing camera decompilation:
  research/binary/ghidra_mission/ghidra_selected_pseudocode.c, sections
  0x3d9d0 and 0x3d51e. The exported decompilation omits the inline switch at
  0x3daac; the original instructions and switch words are authoritative.
- Synthetic execution fixture:
  m3g_validation/original_battle_timeline.json, 6 start and 48 update cases.
- Reproducer:
  analysis_tools/m3g/battle_timeline_oracle.py. It requires the private inputs,
  Unicorn, and the existing analysis_tools/combat/oracle.py loader.

The old Ghidra installation was unavailable during this trace. The retained
exports and Capstone were sufficient for the bindings below. A fresh Ghidra
analysis is not a prerequisite for implementing the two timeline helpers.

## Battle request and side identity

Original battle resolution reaches 0x58354 and calls 0x45b2a. The request
controller stores the two unit pointers at offsets 0x4c and 0x50. Call them
**side A** and **side B**: they must not be unconditionally named attacker and
defender. The branch at 0x582f2..0x58300 goes through 0x583d0 when the attacking
unit's faction byte 0x0b is nonzero and the two unit pointers differ. That
branch swaps the presentation sides and the accompanying damage/health/death
values. In ordinary opposing-faction combat, A is the Autobot presentation
side and B is the Decepticon side.

The copying instructions at 0x45b76..0x45bac and 0x45c6c..0x45c74 establish:

| Controller field | Content |
| --- | --- |
| 0x4c / 0x50 | Side A / side B original unit pointer |
| 0x54 | Counterattack-eligibility byte supplied by the battle caller |
| 0x55 | Whether side A was the original attacker |
| 0x58 / 0x5c | Damage applied to A / B |
| 0x60 / 0x61 | A / B death flag |
| 0x74 / 0x78 | A / B pre-exchange health values supplied by the caller |

0x45afc calls cinematic reset 0x40e38, then 0x40a98 invokes setup
0x3fadc(cinematic, true). Reset clears the phase byte at cinematic offset 0,
the completion byte at 0x0c, and the phase elapsed counter at 4. It records
the before-health values and obtains the now-current health with 0x4313c.

The exact source of the optional setup flag at cinematic offset 0xa4 was not
fully named in this slice. It must remain an explicit recovered input if the
complete phase controller is reconstructed.

## Models, forms, and the battle scene

At 0x3fd02..0x3fd18, setup reads each unit's type byte at offset 9 and stores
the results in cinematic fields 0x7c and 0x80. Calls at 0x3fe34 and 0x4000e
pass these types in R2 to 0x3e416; the resulting roots are saved at cinematic
0x84 and 0x88.

0x3e59e..0x3e5b6 reads the tagged model identifier from the u32 table at
0xceab4. The ordinary type-to-resource entries are:

| Type | M3G resource | Type | M3G resource |
| --- | ---: | --- | ---: |
| 0 | 408 | 12 | 1039 |
| 1 | 699 | 13 | 631 |
| 2 | 444 | 14 | 795 |
| 3 | 492 | 15 | 967 |
| 4 | 1300 | 16 | 564 |
| 5 | 561 | 17 | 1299 |
| 6 | 369 | 18 | 829 |
| 7 | 565 | 19 | 1201 |
| 8 | 634 | 20 | 1070 |
| 9 | 395 | 21 | 489 |
| 10 | 475 | 22 | 490 |
| 11 | 867 | Special entry 23 | 500 |

All these table values have tag 0x03000000. The special type-17 branch at
0x3e468..0x3e48c replaces index 17 with index 23 when either (a) the phase is
not 13 and the constructor's side flag is false, or (b) the phase is 13 and
side A's unit owner byte 0x0a is 1. A generic table-only implementation that
always selects entry 17 would omit that original behavior. This report does
not normalize or remove it.

The ordinary two-side calls at 0x3fe74..0x3fe8e and 0x40036..0x40050 read
unit form byte 0x0c and call 0x3e220 with robot_visible = (form == 0).
0x3e220 looks up the hashed names car and robot, sets car visibility to
robot_visible XOR 1, and sets robot visibility to robot_visible.

The model constructor's incoming R3 is an initial timeline clip index. It is
saved at stack+8 and passed to 0x3c23a at 0x3f5bc..0x3f5be. Ordinary two-side
battle setup passes clip 1 to both constructors (0x3fe34 and 0x4000e); the
single-model path passes clip 3 (0x40252). Clock selection at
0x3f5a0..0x3f5b2 compares the **effective type** after the special override
with cinematic+0x7c. Equal selects clock+0x90; unequal selects clock+0x94.
It does not test the explicit side flag. Same-type sides and the type-17
override therefore need this exact comparison.

The shared battle scene is selected separately. 0x3fb42..0x3fb50 reads map
style at map+0x22c and indexes the u32 table at 0xcea9c:

| Map style | Tagged scene resource |
| --- | --- |
| 0 | 0x030001e6, resource 486 |
| 1 | 0x03000216, resource 534 |
| 2 | 0x030004bc, resource 1212 |

The model loader also has cache, cloning, placement, and fallback paths.
The table and form bindings above do not claim those paths are implemented.

## Timeline arithmetic: original execution established

The helpers select a range on a model's existing animation timeline. They do
not choose a different AnimationController object or rewrite controller
intervals in the inspected bodies.

0x3c23a takes a root reference, clip index, and pointer to a signed integer
clock. It reads a signed 16-bit start frame from 0xceaa8, converts it to
float32, multiplies by the float32 value with bits 0x42055555
(33.333332061767578), truncates the result to signed integer, stores that
clock, and invokes the root's public virtual slot 0x24 with that time.

0x3c950 reads signed 16-bit end frames at 0xcea90 and uses the same float32
conversion. The end comparison occurs against the **float32 end**, before
truncation. The exact values are:

| Clip | Start frame | End frame | Stored start ms | Float32 end ms | End call ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 20 | 0 | 666.6666259765625 | 666 |
| 1 | 50 | 90 | 1666 | 3000 | 3000 |
| 2 | 100 | 160 | 3333 | 5333.3330078125 | 5333 |
| 3 | 200 | 250 | 6666 | 8333.3330078125 | 8333 |
| 4 | 320 | 350 | 10666 | 11666.666015625 | 11666 |
| 5 | 260 | 300 | 8666 | 10000 | 10000 |

The update order is significant:

1. Form the original signed 32-bit current+delta result, then convert it to
   float32 for comparison with the float32 end.
2. Below the end, invoke virtual slot 0x24 with the **old** clock, store
   current+delta afterward, and return 0.
3. At or beyond the end with restart disabled, invoke that slot with the
   truncated end time, leave the stored clock **unchanged**, and return 1.
4. At or beyond the end with restart enabled, call 0x3c23a to store and invoke
   the start time, discard overshoot, and return 1.

Consequently, comparing current+delta to an integer end, always storing the
clamped time, sampling the newly advanced time, or preserving loop overshoot
would each differ from these helpers.

The fixture executes the original helper instructions and the original
ARMv6 libgcc float/int conversions and comparison. The only synthetic
endpoints are object retain/release, the virtual-slot time recorder, and
nonthrowing SjLj registration/unregistration. It verifies both original code
regions remain unchanged. It does not execute a real model, evaluate its
tracks, or render a frame. Signed overflow and negative-delta boundaries are
not included in the 54-case fixture.

## Cinematic state to timeline call

The phase dispatcher is 0x413e8 with its inline table at 0x41444. Model A is
cinematic+0x84, model B is +0x88, and their normal timeline clocks are +0x90
and +0x94. The phase counter at +4 is separate from these clocks.

The descriptive role column follows the camera name and combat branch; the
numeric phase, call address, model pointer, clip, and restart flag are direct
instruction evidence.

| Phase | Role / boundary | Timeline update call | Model | Clock | Clip | Restart |
| --- | --- | --- | --- | --- | ---: | --- |
| 0 | Intro transition | No unit range update in this phase | — | — | — | — |
| 1 | A fires | 0x41e1e | A | +0x90 | 1 | false |
| 2 | B receives the hit | 0x41fa4 | B | +0x94 | 0 | false |
| 3 | B death presentation | 0x4214c | B | +0x94 | 2 | false |
| 4 | B fires | 0x42234 | B | +0x94 | 1 | **true** |
| 5 | A receives the hit | 0x423a4 | A | +0x90 | 0 | false |
| 6 | A death presentation | 0x4253a | A | +0x90 | 2 | false |
| 7 | Exit transition | No unit range update in this phase | — | — | — | — |
| 8 | Shared scene hold | No unit range update in this phase | — | — | — | — |
| 9 | A setup camera | 0x416ea | A | +0x90 | 5 | false |
| 10 | B setup camera | 0x418b6 | B | +0x94 | 5 | false |
| 11 | A zoom/car camera | 0x41a90 | A | +0x90 | 4 | false |
| 12 | B zoom camera | 0x41c34 | **A** | **+0x94** | 4 | false |
| 13 | Single-model/selection path | 0x4266c | A | +0x90 | 3 | true |

The phase-12 model A / clock B pair is what the original instructions say.
Its entry paths initialize model B's clock with clip 4. Do not silently
replace the subsequent update's model pointer with B.

All listed range-update return values are ignored by this phase controller;
phase progress uses the separate counter and combat flags. Thresholds:

| Phases | Original counter transition condition |
| --- | --- |
| 0, 7 | elapsed > 999 |
| 1, 4, 9, 10 | elapsed > 1332 |
| 2, 5 | elapsed > 731, with delta divided by two toward zero before dispatch |
| 3, 6 | elapsed > 2999 |
| 8 | elapsed > 3599 |
| 11, 12 | elapsed > 998 |

The normal firing/hit/death sequence is conditional on side A first, counter
eligibility, and death flags. In particular, phase 1 leads to phase 2; phase
2 selects B death, B's counterattack, or exit. Phase 4 leads to phase 5; phase
5 selects A death, A's counterattack, or exit. Phases 3/6 lead to exit.
Phase 7 sets the completion byte at cinematic+0x0c after its threshold.
The optional setup/hold paths and phase 13 are not a fully reconstructed
native controller in this report.

## Camera selection and limitations

0x3d9d0 dispatches its camera ID through the inline switch at 0x3dab0.
These IDs are separate from phase IDs:

| Camera ID | Scene fallback name |
| --- | --- |
| 0 | cam_auto_takehit for A form 0; cam_auto_car otherwise |
| 1 | cam_auto_shoot |
| 2 | cam_auto_death for A form 0; cam_auto_car otherwise |
| 3 | cam_decpt_takehit |
| 4 | cam_decpt_shoot |
| 5 | cam_decpt_death |
| 6 | Stat_Camera |
| 7 | d_Stat_Camera |
| 8 | cam_rot |
| 9 | cam_auto_setup |
| 10 | cam_decpt_setup |
| 11 | cam_auto_zoom for A form 0; cam_auto_car otherwise |
| 12 | cam_decpt_zoom |

Before falling back to those scene nodes, the function can prefer a camera
inside the relevant unit model: hash roboCAM, or hash vehiCAM for its vehicle
form. Camera IDs 6/7 force the roboCAM choice; ID8 bypasses the model-camera
search. The original camera is therefore not adequately represented by a
single newly invented view/projection matrix.

The saved 0x3d51e export helps explain time propagation into the scene
hierarchy, but the complete camera transform, clipping, placement, effects,
and output pixels have not been independently validated in this slice.
The peer renderer trace resolved the public interface separately:
CssGroup type getter 0x90540 returns 9; public wrapper factory 0xb0b74 selects
0xb0cdc, which constructs through 0xac2d0, 0xae220, 0xb27d2, and 0xae674.
At 0xae698..0xae69c, the wrapper installs Thumb target 0xae7cd at public
slot 0x24. That wrapper calls 0x99e3c with the internal object, time, and
visitation -1; that routine traverses tracks/controllers and reaches the
original sampler 0x9346c. This is the Object3D animate path.
The companion static-chain evidence is M3G_PUBLIC_ANIMATE.md.

The time-call fixture intentionally records at the public slot rather than
executing that path, so its claim remains timeline arithmetic only. Raw
CssGroup primary vtable offset 0x24 is a different method and must not be used
as the public wrapper target.

## Reproduction

From the source repository root, with private local reference inputs:

~~~bash
python3 analysis_tools/m3g/battle_timeline_oracle.py \
  --binary /private/G1awakening.armv6 \
  --libgcc /private/libgcc_s.1.dylib \
  --unicorn-dir /path/to/unicorn/site-packages \
  --output /tmp/original_battle_timeline.json
~~~

The output is metadata and synthetic cases only. The script has no network
access, no app dependency, and no original payload extraction step. It is not
part of any native library, application, or runtime.
