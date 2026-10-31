# Connected native animated battle presentation

The native Apple app now connects real `GameSession::battle()` outcomes to
`M3GBattlePresentation` and `MetalM3GRenderer`. This replaces the temporary timed
HP panel. Original M3G models, scene cameras, clips, effects and texture
substitutions drive the battle. The product contains C++/Objective-C++/Metal code
and data assets; it contains no original executable, ARM interpreter, JIT or
runtime instruction translator.

This is an integrated source checkpoint, **not proof of successful Apple
compilation, GPU output, physical iPhone XR performance or a complete device
playthrough**. The next prepared gates are the Apple shader/host builds, offscreen
Metal tests and actual iOS execution. No remote repository write was attempted
for this checkpoint after publication authorization was declined.

## Public boundary

`M3GBattleRequest` contains the pending session token, terrain style, immutable
attacker/defender pre-exchange snapshots and the real resolved `CombatExchange`.
The adapter does not authorize an attack, recompute damage, alter health, change
mission state, or consume the game's random stream.

The host uploads `assets()` once, advances the controller with 30 ms simulation
steps, dispatches `take_audio_requests()`, and calls `prepare_frame` using the
GPU models' retained immutable asset addresses. The output contains original
clear state and eight ordered submissions: World, model A, model B, shot/hit
wrappers 0–3, and death effect. A disabled or culled node remains an identifiable
submission; no missing participant is replaced with a sprite or fabricated mesh.

`status().complete` remains only a controller condition. The Apple app releases
the actual session token only after the final frame and native HUD have drawn.

## Recovered assets and graph

The 24-entry model resource table and three style scenes 486/534/1212 come from
`M3G_BATTLE_BINDING.md`. Presentation sides follow the original faction-based
A/B mapping, rather than assuming A always attacked. The special type 17 on side B
uses the original effective type 23/resource 500. The model clock selection still
compares effective type with the saved side A type.

`M3GBindings` supplies the proven reverse-byte name hash, `Transform` suffix,
self-first child-order lookup, and exact camera child chain. A loaded model's
child 0 Group, or only its child 1 fallback, is detached from its serialized World
and logically attached under the battle World. The `autobotTransform` or
`decepticonTransform` matrix relative to that World becomes the root's generic
transform; no arbitrary model scale, side offset or axis flip is added. Form
visibility uses the actual robot/car bindings. Duplicate names preserve the
verified first-node lookup behavior. See `M3G_MODEL_ATTACHMENT.md` and
`M3G_BINDING_CORRECTION.md`.

The seven original runtime texture-override model types consume the frozen
`M3GMaterialOverrides` plan. Every step updates all retained surfaces sharing its
Texture2D object identity, in original order. A replacement receives a distinct
image identity and original Image2D format 99/RGB; filtering, wrapping, texture
function and transform remain intact. A later step targeting the same Texture2D
wins. Unrelated textures that previously shared an Image2D are not modified.
The actual original PNG decoder and native-byte comparisons are documented in
`M3G_MATERIAL_OVERRIDES.md`.

Four effects use resources 1050/511/382/969. Each effect's named
`shot_posTransform` is detached from its real parent and placed beneath a new
identity Group wrapper, as in original 0x3f866. Shot and hit wrapper placement
comes from the current model's `shot_posTransform`, `hit_pos_robotTransform` or
`hit_pos_carTransform`. Missing attachment names retain the original prior
placement branch. Smoke/death resource 920 uses the proven child 0/child 1 Group
fallback and is positioned once from the actual dying side's
`smoke_pos_robotTransform`/`smoke_pos_carTransform`.

## Animation, cameras and drawing

`M3GScenePose` retains transform/scalar state; the supported original LINEAR,
SLERP, SPLINE and SQUAD samplers update only the actual animated subtree. Camera
selection and propagation use the verified ordered operations in
`M3GBattleCamera`: old-camera propagation, selection, new-camera propagation,
and additive local `Translate(0, 0, 2)` where required. This translation is never
mislabelled as absolute SetTranslation or deduplicated when both ordered calls
reach the same parent.

The Head-height flag reads native column-major element 14, corresponding to the
original public row-major element 11, adds float bits 0x3ed682b4 and compares with 2.
The active camera can belong to the scene or an attached participant; graph
ancestry and transforms are evaluated accordingly. Actual scalar FOV, alpha and
visibility tracks remain connected.

Each original node call prepares and flushes its own mixed mesh/Sprite3D queue.
The original World clear occurs once. Actual clear colors use the recovered
float32 to 16.16 conversion: scenes 486/1212 are `[24672, 40093, 65536, 65536]/65536`,
scene 534 is `[43177, 0, 55513, 65536]/65536`; all three clear depth to 1. Participant
submissions do not clear again. The Metal host preserves individual depth test
and depth-write state, alpha tests, blend/texture functions, and the recovered
Sprite3D projection/sampler behavior from the existing renderer modules.

The native XR display policy intentionally adapts only perspective aspect or
parallel aspect to `drawableWidth/drawableHeight`. Authored vertical FOV or
parallel height remains intact. The original CPU camera arithmetic is unchanged.
This avoids stretching a 1.5-aspect authored camera across the XR's wider native
landscape display, but it is an intentional framing adaptation, not an authored
pixel-identity claim.

## 60 Hz presentation and actual completion

Controller updates remain separate from display refresh. Between adjacent 30 ms
states, `prepare_frame` can resample each already-active animation target at an
interpolated integer-millisecond time and interpolate explicit additive camera
translations. It operates on copied pose state and invokes no phase transition,
audio request, gameplay RNG or completion action. Camera, clip and placement
cuts remain discrete. A 60 Hz request is therefore not merely a counter around an
unchanged 30 ms pose, but device throughput and pacing still require measurement.

The Apple host first builds and uploads the complete asset set. It waits for the
first matching presented drawable before advancing playback, excluding loading
time from simulation catch-up. Each final acknowledgement requires all of:

- The command buffer completed successfully and its drawable supplied a finite,
  positive presentation timestamp. Both asynchronous facts are joined on the
  main queue.
- The current scene revision and pending session battle token still match.
- The adapter reached its natural completed state at the exact rendered revision.
- The native HUD actually drew that same token/revision.

There is no Continue button, private HUD timer, fake HP tween, unsupported-model
skip, or time-only acknowledgement. Battle drawables carry no tactical world,
focus or transform acknowledgements. Stale tactical frames cannot acknowledge
hidden work after a battle starts. Session replacement invalidates old scene
callbacks and clears all battle resources/caches. Background/resume resets the
scheduling accumulator without executing catch-up turns.

The natural-completion callsite's audio operation 2/argument 0 is dispatched after
the real host acknowledgement. It is not replaced with an invented operation 4
music resume. Initial (5, 23) and phase events (5, 8), (5, 9), (5, 10), (5, 11) come from the recovered
ordinary setup/controller.

The HUD itself is native UIKit point layout with original localized unit names
and exact controller health/damage values. It does not reproduce the original
HUD artwork, font metrics or signed 16.16 entry/exit slide layout.

## Validation completed

`m3g_validation/battle_presentation_validation.json` records the bounded results
and source pins. The private archive hash is
`663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4`.

| Check | Observed result | Limit |
| --- | --- | --- |
| Native phase/draw parity | 1,225 original updates +159 draw plans; 82,600 field checks, zero differences | Original CPU arithmetic and recorded public effects |
| Actual model/form/style adapter cases | 26 sequences complete; all 24 model resources, all three scenes, all seven override types represented | Resolved combat inputs are synthetic; CPU frames only |
| Actual-asset outcome cases | 21 sequences complete: death on either side, counter/no-counter, reversed attacking faction, damaged pre-health and immunity, across all three scenes | CPU preparation, no GPU image comparison |
| Real first-mission session bridge | Actual public actions reach battle at tick 81; real seeded result attacker 95/defender 50; 534 presentation updates complete | Startup dialogue/focus/transform feedback is explicitly synthetic |
| Read-only render interpolation | 217 prepared frames, 54 repeated-sampling checks, 46 distinct intermediate cases; controller/audio and full game RNG unchanged | Numerical frame data, not physical refresh measurement |
| Completion ownership | Real session token remains pending through all adapter updates and final frame preparation, until explicit synthetic CPU host report | Actual Apple callback behavior is separately source-reviewed |
| Combined native project | 53 registered CTest suites pass | Linux host; Apple compiler/GPU still pending |
| Independent reviews | Controller/event ordering, graph/camera/effects, texture aliasing and Apple token/command/presentation gates reviewed with no concrete blocker | Source review, not execution on Apple hardware |

The real-session bridge uses seed 0x4741 and the established public command route:
Story 0/type 11 transforms, moves to (3, 5), ends turn for the real native AI, moves
to (4, 7) on the next human turn, transforms back, then attacks type 19. It does not
write unit coordinates, HP, mission cursor or AI fields to manufacture a battle.
The presentation retains the session's full 624-word RNG state, index and draw
count unchanged. The final host report in this headless probe is explicitly
synthetic and is not labelled as a displayed frame.

## Tactical decoration integration in the same app checkpoint

The app now renders the frozen style 0/2 decoration draw list from the core-owned
layout, animation bank and saved state. It lazily uploads the six original
atlas descriptors on resource 961, uses their exact frame/model and attachment
order, and excludes visible but unattached nodes. Scene rendering makes no RNG
draws and advances no private decoration timer. Core `terrain_generation`
invalidates the cached list. Style 1 still draws transient 420 slots before fixed
420 faces, using the core's common UV phase.

This establishes the recovered decoration sibling order. Their global grouping
against the current native tactical overlays remains inspection-presentation
policy until the full original tactical scene tree is reproduced.

## Remaining product boundaries

- Actual Apple build, Metal shader/pixel gates, final on-device output and XR
  frame pacing remain pending. No complete physical-device playthrough is claimed.
- The earlier tactical support-attack presentation at 0x583f0 is separate from the
  ordinary two-participant cinematic; its 2-second camera/sprite sequence is not
  implemented by this adapter. Resolved support damage remains core-owned.
- Original site/marker artwork, tactical camera, HUD/font layout and original
  overall tactical draw-tree parity remain incomplete native presentation areas.
- The shared background-music maintenance path and complete game sound-event
  coverage remain separate from the ordinary battle's verified requests; actual
  audibility has not been tested on the target device.
- Missing/malformed required assets, unsupported renderer state or invalid
  ancestry stop with an error; they never release the battle token as success.
  The original load-failure fallback model is not substituted in the native app.
- Development reference programs may use unchanged original ARM execution. They
  are excluded from the product and are not a compatibility layer shipped with it.

The app metadata is 0.5.0/build 5, labelled a native campaign integration test.
Reproduction commands and private-input requirements are in
`analysis_tools/m3g/BATTLE_PRESENTATION_REFERENCE.md`. The exact connected source
freeze is `m3g_validation/M3G_BATTLE_PRESENTATION_FREEZE.json`.
