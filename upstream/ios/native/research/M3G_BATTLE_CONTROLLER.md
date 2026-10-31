# Original ordinary battle controller and draw ordering

## Scope and result

This report recovers the ordinary two-participant battle controller in the
audited ARMv6 G1 Awakening executable. It is a contract for a native implementation,
not a claim that an original battle has rendered on an Apple device.

The development recorder executes the unchanged complete controller `0x413e8`,
the original timeline helpers `0x3c23a` / `0x3c950`, original ARM libgcc arithmetic,
and the unchanged draw-decision function `0x40f0c`. It records **295 isolated
updates, ten complete ordinary sequences comprising 930 updates, and 159 draw
cases**. All complete sequences reach the real completion byte. Original code
and libgcc read-only bytes remain unchanged; there are no instruction patches.

The reproducible runner is `analysis_tools/m3g/battle_controller_oracle.py`.
Its preserved observations are
`native/research/m3g_validation/original_battle_controller.json`. The adjacent
JSON manifest records input and tool hashes. The recorder supplies synthetic
objects and records public animation, camera, placement, text, audio and drawing
boundaries; it does not execute a complete asset loader, GPU, combat resolver or
the separate unit-viewer phase13. Some isolated transition inputs intentionally
combine states that ordinary setup would not produce.

The previously frozen `M3G_BATTLE_TIMELINE`, `M3G_BATTLE_CAMERA`,
`M3G_MODEL_ATTACHMENT`, `M3G_BINDING_CORRECTION`, and
`M3G_CAMERA_TRANSLATE_SLOT` reports remain authoritative for their respective
helpers. The render-pass/background implementation below the recorded
`0x3cc9c` boundary is documented separately.

## Actual combat-result inputs

The resolver `0x58172` completes attack and optional counterattack damage before
calling request setter `0x45b2a` at `0x58354`. This is presentation of resolved
results; the cinematic does not reroll damage.

The map's attacker / defender pointers are `+0x1c0` / `+0x1c4`. At
`0x582ec..0x58358`, attacker faction byte `+0x0b == 0`, or identical attacker and
defender pointers, selects **A=attacker, B=defender**. Otherwise the nonzero
attacker-faction branch at `0x583d0` selects **A=defender, B=attacker** and swaps
health, damage and death inputs. A/B are presentation sides, not simply the
attacker/defender array order.

| Request-controller offset | Meaning |
| --- | --- |
| `+0x4c`, `+0x50` | A and B unit objects |
| `+0x54` | A counterattack actually took place |
| `+0x55` | A was the original attacker |
| `+0x58`, `+0x5c` | Resolved damage assigned to A and B |
| `+0x60`, `+0x61` | A and B destruction flags |
| `+0x74`, `+0x78` | A and B health before the exchange |

Use the actual optional counterattack, not merely pre-attack eligibility: the
resolver clears the counter flag when the defender dies (`0x58260`). The
original pre-health bytes are captured from both units at `0x58610..0x5862a`.
Unit form is read from byte `+0x0c`; immunity is byte `+0x19`.

The ordinary request has only two unit pointers. The separate support sequence
at `0x583f0` iterates map `+0x3c` / index `+0x4c`, waits for tactical-renderer
`0x5d176` and a greater-than-1999 elapsed gate, updates tactical-unit state and
camera, then invokes the main exchange at `0x584c2`. Those support units are not
extra models in the two-participant `0x413e8` / `0x3cc9c` sequence. This report
does not implement that earlier tactical support presentation.

## Initialization and persistent state

`0x45afc` resets via `0x40e38`, prepares via `0x40a98` / `0x3fadc`, sets the
outer controller state to1, and clears input. The global skip-battle option
instead takes the separate bypass path.

| Cinematic offset | Initial value or purpose |
| --- | --- |
| `+0x00` | Phase byte, initially0 |
| `+0x04` | Signed elapsed milliseconds in the current phase, initially0 |
| `+0x0c` | Completion byte, initiallyfalse |
| `+0x18`, `+0x1c` | Final A/B health read from the already-resolved unit objects |
| `+0x20`, `+0x28` | Health text, initially formatted from pre-exchange health |
| `+0x30`, `+0x34` | Displayed health cache, initially pre-exchange health |
| `+0x40` | Signed16.16 HUD slide, initially `-(screenWidth << 17)` |
| `+0x44` | Twice the logical screen width |
| `+0x60` | Retained scene World |
| `+0x6c`, `+0x6d` | A/B camera head adjustment flags |
| `+0x84`, `+0x88` | A/B attached model roots |
| `+0x8c` | Selected embedded camera presence, if any |
| `+0x90`, `+0x94` | Independent A/B model timeline clocks |
| `+0x9c` | Current camera's side flag; true means A |
| `+0xa0` | Four shot/hit wrapper Groups |
| `+0xa4` | Intro predicate |
| `+0xac` | Attached death-smoke Group |

The ordinary intro predicate at `0x3fc56..0x3fcc0` is exactly
`beforeHealthA == 100 && beforeHealthB == 100 && formB == 0`. The setup audio
request `(category5,event23)` at `0x3fba8..0x3fc42` depends on both pre-health
values being100, independently of B's form.

The initial camera branch `0x40408..0x40462` is evaluated in this order:

1. If A form is1, or the combined little-endian halfword at request `+0x54` is0,
   choose camera10.
2. Otherwise, if A attacked first and no counter occurred, choose camera9.
3. Otherwise, if the intro predicate istrue, choose camera8.
4. Otherwise choose camera1 when A attacked first, or camera4 when B did.

The model selection, form visibility, scene-style resource mapping, anchor
placement and effective type17→23 substitution are in `M3G_MODEL_ATTACHMENT`.
This report does not replace those checks with a guessed facing or origin.

### Head-height matrix conversion

The head check obtains `HeadTransform` relative to the scene World, then calls
the public Transform `+0x10` float-array getter. Its installed target is
`0xb210c` (`0xb1fe4..0xb1fe8`); that wrapper calls `0xa66c8`.
The loop `0xa66e4..0xa6700` transposes the internal column-major matrix to the
public row-major array: `out[row*4+col] = internal[col*4+row]`.
The A caller reads public element11 at `0x3ffc4`, which is **column-major
element14**, adds float32 bits `0x3ed682b4` (approximately0.41896594) using
original `addsf3`, then compares the float32 result with2.0 via `gtsf2`.
The nonzero comparison result sets `+0x6c`; the corresponding B block at
`0x4018c` onward sets `+0x6d`. Missing head nodes leave the setup default.
Do not read element11 of a native column-major matrix.

## Update rules and exact phase order

`0x413e8(cinematic, signed32 deltaMs)` executes one selected phase per call.
There is no loop to spend a large delta across several phases. A transition
discards overshoot by zeroing phase elapsed, except completion. Phases2 and5
first divide the supplied delta by2 with truncation toward zero. All tests use
nonnegative time; the instructions use ordinary signed32 arithmetic with ARM
wrapping rather than a saturating counter.

`old` below means elapsed at entry, and `next = old + effectiveDelta`.
All transition comparisons are **strict greater-than**, not greater-or-equal.
Camera updates and model timeline sampling must remain in the listed order.

| Phase | Ordered actions before the gate | Gate and next action |
| --- | --- | --- |
| 0, enter | Camera `(0,adjust0)`; slide from old elapsed; advance elapsed | `next > 999`: elapsed0, slide0. No intro → Afirst: phase1/camera1/startA clip1/placeA shot; Bfirst: phase4/camera4/startB clip1/placeB shot. Intro → A form1 or combined request54==0: phase10/startB clip5/camera10; otherwise Afirst without counter: phase9/startA clip5/camera9; otherwise phase8. |
| 8, establishing | Camera `(old,adjust0)`; advance | `next > 3599`: Afirst → phase9/startA clip5/camera9; otherwise phase10/startB clip5/camera10. |
| 9, A setup | Set camera-side=true; **tick A clip5 first**; camera at updated A clock if embedded, otherwise old elapsed, with A adjustment; advance | `next > 1332`: Afirst → phase10/startB clip5/camera10; otherwise B form0 → phase12/startB clip4/camera12; otherwise phase11/startA clip4/camera11. |
| 10, B setup | Set side=false; **tick B clip5 first**; camera at updated B clock if embedded, otherwise old, with B adjustment; advance | `next > 1332`: Bfirst → phase9/startA clip5/camera9; otherwise A form0 → phase11/startA clip4/camera11; otherwise phase12/startB clip4/camera12. |
| 11, A close-up | Camera at embedded A clock or old, A adjustment; tick A clip4; advance | `next > 998`: Afirst and B form0 → phase12/startB clip4/camera12. Otherwise enter phase1 when Afirst, or phase4 when Bfirst, with its camera/shoot clip/effect placement. |
| 12, B close-up | Camera at embedded B clock or old, B adjustment; **tick model A with B's clock, clip4**; advance | `next > 998`: Bfirst and A form0 → phase11/startA clip4/camera11. Otherwise enter phase4 when Bfirst, or phase1 when Afirst. The A-target/B-clock behavior is present in original code and recorded. |
| 1, A shoots | Animate effect0 at `old-466`; shot-audio crossing; camera at embedded A clock or `3*old`, A adjustment; tick A clip1 without restart; advance | `next > 1332`: phase2, elapsed0; damage text is B immune ?0:damageB; camera3; place B-hit effect; start B clip0. |
| 2, B hit | Effective delta is raw/2; animate effect1 at `max(0,2*(old-66))`; hit-audio crossing; camera at embedded B clock or old, B adjustment; tick B clip0; update displayed B health to final B health; advance | `next > 731`: B death → camera5/phase3/startB clip2. Otherwise Afirst and actual counter → camera4/phase4/startB clip1/placeB shot. Otherwise phase7. |
| 3, B death | Camera at embedded B clock or `old/2`, B adjustment; animate smoke at old; tick B clip2; advance | `next > 2999`: phase7, elapsed0. |
| 4, B shoots | Animate effect2 at `old-466`; shot-audio crossing; camera at embedded B clock or `3*old`, B adjustment; tick B clip1 **with restart**; advance | `next > 1332`: phase5, elapsed0; damage text is A immune ?0:damageA; camera0; place A-hit effect; start A clip0. |
| 5, A hit | Effective delta is raw/2; animate effect3 at `max(0,2*(old-66))`; hit-audio crossing; camera at embedded A clock or old, A adjustment; tick A clip0; update displayed A health to final A health; advance | `next > 731`: A death → camera2/phase6/startA clip2. Otherwise Bfirst and actual counter → camera1/phase1/startA clip1/placeA shot. Otherwise phase7. |
| 6, A death | Camera at embedded A clock or `old/2`, A adjustment; animate smoke at old; tick A clip2; advance | `next > 2999`: phase7, elapsed0. |
| 7, exit | No camera/model animation; slide from old elapsed; advance | `next > 999`: set completion byte; retain phase7 and elapsed. |

For non-setup phases, camera propagation precedes model sampling. A start-clip
operation both initializes its clock and samples the start time. The ordinary
timeline helper samples its old clock, then stores the advanced clock when
below its float end. At a non-restarting end it samples the truncated float end
without storing the advanced clock. Restarting discards overshoot. Use the
separate exact `M3GBattleTimeline` contract instead of replacing this with
`clock += delta` followed by one generic clamp.

The two slide expressions retain original float32 operation order and
truncate-to-integer conversion:

- Phase0: `-trunc(float32(2W << 16) * (1 + float32(old)/-1000))`.
- Phase7: `-trunc(float32(2W << 16) * float32(old)/1000)`.

### Audio and health events

| Phase | Exact crossing condition on effective delta | Request |
| --- | --- | --- |
| 1 | `old <= 465 && next > 466` | `(5,8)` |
| 4 | `old <= 465 && next > 466` | `(5,9)` |
| 2 or5 | `old <= 332 && next > 333` | `(5,11)` |

The unequal edge conditions are intentional transcription of the original.
For example old466→467 does not emit a shot sound. The reference includes
both sides of these gaps. The displayed health does not interpolate here:
the hit phase updates the decimal string and cache directly to the resolved
final health when they differ. A native health tween is an additional UI
choice and must not be described as this original behavior.

No RNG call occurs in the ordinary controller. A common tail checks the
global music-enabled flag; when set it queries audio category6 and can call
map `0x4e7b8` to maintain music. The recorder leaves that separate flagfalse;
music status/track scheduling is not validated by these phase cases.

## Required effect objects and placement

| Array position | Resource | Meaning |
| --- | --- | --- |
| 0 | 1050 (`0x0300041a`) | A shot |
| 1 | 511 (`0x030001ff`) | B hit |
| 2 | 382 (`0x0300017e`) | B shot |
| 3 | 969 (`0x030003c9`) | A hit |
| Separate `+0xac` | 920 (`0x03000398`) | Death smoke |

The four array objects use loader helper `0x3f866`. It finds
`shot_posTransform` in each loaded World, obtains the actual parent, detaches
that named Group, constructs a **new Group wrapper**, adds the detached Group
to the wrapper, and appends the wrapper to the retained scene World. Their
root selection is not an unconditional serialized child0 lookup.

Smoke uses the separately recovered Group-interface child0/child1 fallback:
`0x4075e` gets child0, `0x4076a` queries Group, `0x407c2` compares the result
with null, and `0x407ee` / `0x407fa` try child1. It is detached at `0x40862`,
attached to the scene at `0x40870`, and initially disabled at `0x4087e`.
Actual resource920 has Light child0 and Group child1, so omitting this fallback
would reject or misattach the real smoke effect.

`0x3c74a(cine,sideA,hit)` finds `shot_posTransform` for a shot, or
`hit_pos_robotTransform` / `hit_pos_carTransform` according to the struck
model's form. It obtains the attachment transform relative to the scene and
assigns the wrapper's generic transform. Its hit branch reverses the side
selector when choosing the array slot, yielding the table above. A missing
attachment performs no assignment; it does not synthesize an origin.

Smoke placement `0x3caa0` runs **once during setup** (`0x408d8`), choosing A
when A's destruction flag istrue and otherwise B. It uses
`smoke_pos_robotTransform` / `smoke_pos_carTransform`, assigns the relative
generic transform, and uses an identity temporary if the named attachment is
absent. The per-frame death phases animate the stored smoke; they do not
recompute its attachment every update.

These five effect resources have been independently inventoried by the
rendering agent as rigid/Sprite3D data with alpha, visibility, scale and
translation tracks. That asset audit is separate from the synthetic controller
recorder and does not turn the latter into full-loader execution evidence.

## Draw decisions are separate from simulation

`0x40f0c(cine,canvas,refresh)` does not advance phase or time. The recorder
asserts that its captured state is unchanged in every draw case. Public
render-enable setters run in original order before `0x3cc9c`, then the HUD is
drawn. The controller's visibility flags can override authored enabled state
for these attached root objects; descendant animation still has its own state.

| Phase | A root enabled | B root enabled | Effect enable |
| --- | --- | --- | --- |
| 0 with refresh | A form !=1 | true | All false, including smoke |
| 0 without refresh | Unchanged; no 3D call | Unchanged | HUD only |
| 1..6 or8..12 | `sideA || phase==1 || phase==8 || (phase==4 && Aform!=1)` | `!sideA || phase==1 || phase==4 || phase==8` | First disable all four. A/B shot selected by side only in phase1/4 and elapsed>466. Opposite-side hit selected by side in phase2/5. Smoke only phase3/6. |
| 7 | sideA | !sideA | All false, including smoke |

The shot slot is `sideA ?0:2`; the hit slot is `sideA ?3:1`. Refresh is ignored
for the other ordinary phases. After 3D rendering and HUD, phases2/5 also draw
damage text unless cinematic `+0x0d` is nonzero. Phase2 uses A's faction,
phase5 B's: x is width/4 for nonzero faction and 3width/4 otherwise; y is
height/4, font3, flags20. The fixture records these requested draw parameters,
not rendered glyph pixels.

The scene render helper has a separately verified sequence: hide participant
and effect roots, render the retained World, restore and submit A, then B,
then effects0..3, then smoke. A global merged queue across all instances is
not equivalent. Its exact background and per-submission queue policy is a
separate report rather than an unrecorded assumption in this fixture.

## Completion, skip, and host acknowledgements

`0x3c0d0` returns cinematic `+0x0c`. The outer controller checks it at
`0x47d58` **before** calling `0x413e8` at `0x47e4e`. Therefore the final phase7
update sets completion, and the next outer update consumes it and tears down
through `0x45aa8`. There is no additional original Continue button required
after natural ordinary completion. Input masks0x40,0x10,0x80,0x8000 can take
the separate ordinary skip path; phase13 has different handling and is
outside this recovery.

A native host may require a token matching a truly presented frame before
releasing a battle session. That is an honest host-integrity gate, separate
from the recovered game timer and completion byte. It must not acknowledge an
undrawn battle or manufacture completion to bypass unsupported assets.

## Supported boundary and remaining checks

The frozen phase reference covers ordinary phases0..12, both attack orders,
counter/no-counter, intro/no-intro, both deaths, form-dependent transition
branches, embedded/fallback camera timing, audio edge cases, immunity text,
and draw visibility. It does not cover viewer phase13, the global music
rescheduler, the earlier tactical support presentation, malformed asset
recovery, actual Metal execution, or device frame pacing. Full-sequence tests
use delta100; isolated cases include exact boundaries and large deltas.

Reproduction requires the exact original ARMv6 image and original ARM libgcc
named in the JSON manifest, Python3, Unicorn2.1.4, and the unchanged shared
`analysis_tools/sprite/render_oracle.py`. Neither proprietary input belongs in
the source repository or the native application.

```sh
python3 analysis_tools/m3g/battle_controller_oracle.py \
  --binary /path/to/G1awakening.armv6 \
  --libgcc /path/to/libgcc_s.1.dylib \
  --unicorn-dir /path/to/unicorn-python \
  --output /tmp/original_battle_controller.json
```
