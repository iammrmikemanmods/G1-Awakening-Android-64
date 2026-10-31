# Native first-mission UI integration

> Historical checkpoint: `NATIVE_CAMPAIGN_UI.md` and its pinned 0.4.0/build 4 manifest supersede the earlier campaign, turn, save, production/boarding, Undo and style 1 terrain limitations below. This record retains the claims of its own earlier snapshot.

This checkpoint connects the native Metal scene to `GameSession`. It is source ready for an Apple build; this record does **not** claim successful Apple compilation, an iPhone run, sustained 60 FPS, a completed mission, or save persistence. No original executable, interpreter, JIT, compatibility host or development oracle is part of the application.

## Implemented application path

`platform/apple/NativeApp.mm` loads the supplied archive and extracted terrain data after checking their pinned SHA-256 values. The original terrain, sprite descriptors, image resources, animation table and localization supply the scene. `MissionScene` accepts the actual applied `WorldBootstrap`, and the recovered native gameplay modules remain the data/rule authorities. The legacy `FirstMissionScene`/`InitialPresentation` tools remain available for the frozen first-scene reference.

The view controller now owns one `GameSession`, seeded with the current Unix seconds. The session's actual unit coordinates, forms, movement offsets, facing, action flags, combat stats and separate site/item owners feed the renderer and report. No UI action directly declares an objective or campaign successful.

The interface includes:

- Original mission title, dialogue text/headings, unit names and objective text.
- Native UIKit point-sized controls for Move, Capture, Transform and Attack. End Turn is deliberately not exposed in this checkpoint because the AI driver is still being integrated separately.
- A movement overlay built from the actual terrain top faces and `selected_movement_range()`. It shows valid empty destinations. The user selects the unit, chooses Move, selects a destination, then taps the same destination again to request the recovered route.
- Actual `capture_selected()` and `transform_selected()` calls. Capture changes the real nested item/site state according to `CaptureAction`; movement alone does not capture anything. The wrong-form capture branch displays original localized message 1145. Transform uses the original eligibility and state transition, including the pending renderer request.
- Attack selection uses `selected_attack_targets()` and `attack_selected()` with current native state. The returned `SessionBattle` drives a real native before/result HP panel, including a counterattack when the core produced one. Final draw plus explicit user acknowledgement is required before releasing the battle token; details are in `NATIVE_BATTLE_PRESENTATION.md`.
- A test report containing real session state, action counts and presentation measurements. It explicitly identifies unfinished features.

Selection hit testing uses the current sprite frame and native model matrix. Destination hit testing projects the terrain's recovered top-face triangles with the same view matrix used for drawing. Touch targets and button layout are native UI policy, not a recovered copy of the old pixel-coordinate UI.

## Dialogue uses measured and drawn text

`NativeDialogue.hpp` defines a single-translation-unit Objective-C++ `UIView` implementation. It constructs explicit TextKit 1 storage, layout manager and text container, rather than inferring line count from a character count. The layout manager measures line fragments at the actual available point width. Glyph ranges determine the page and visible text. Drawing is performed with `drawGlyphsForGlyphRange:atPoint:`.

The renderer supplies `DialogueLayout` with the measured line count, line height, native ink metric and actual box height. The native system font is 14-point medium; these metrics are native font/layout choices and are **not claimed to match the original font**. `dialogue_character_budget()` controls the original reveal progression. The final reveal flag remains false until `drawRect:` has drawn the current full visible range. A changed text, page, layout or glyph range invalidates that feedback.

Back, Continue and Skip queue the original input masks 1, 16 and 128. Inputs are associated with the current command index; the native mission controller performs the original consume-on-first-match behavior. A new dialogue command clears stale edges. There is no automatic acknowledgement based on a guessed text duration or guessed page count.

A bounded independent review found no blocker in this first-session contract. The explicit `installSession` boundary now invalidates old drawable callbacks, clears queued input and camera/transform state, and resets both dialogue and battle canvases before building a new scene. It is the only assignment to the session pointer. Numeric command indices and battle tokens may repeat in a new session without reusing a completed presentation. The application still starts the first session; this helper does not itself advance a campaign or load a save.

Primary Apple API references:

- [Ensure layout for a text container](https://developer.apple.com/documentation/uikit/nslayoutmanager/ensurelayout%28for%3A%29?language=objc).
- [Enumerate line fragments](https://developer.apple.com/documentation/uikit/nslayoutmanager/enumeratelinefragments%28forglyphrange%3Ausing%3A%29).
- [Character range for glyph range](https://developer.apple.com/documentation/uikit/nslayoutmanager/characterrange%28forglyphrange%3Aactualglyphrange%3A%29).
- [Draw glyphs](https://developer.apple.com/documentation/uikit/nslayoutmanager/drawglyphs%28forglyphrange%3Aat%3A%29).

## Camera acknowledgements follow real presentation

The application still uses its explicitly identified orthographic inspection projection. A script camera request now moves that camera to the requested real grid tile. Immediate requests apply immediately; other requests use a 360 ms native eased pan. This duration, projection and tile-focus policy are not claimed as original camera parity.

The request is acknowledged only when a Metal drawable containing the reached target is reported presented with a positive finite timestamp. The command token must still match the active camera gate. `SessionFocusRequest` uses the same presentation path with its separate token for real turn/AI focus requests. The scene revision must also match; a stale request cannot complete a different command, map or session. Pan/pinch/reset controls cannot move the camera away during a pending script camera or session focus request. These acknowledgements are native host presentation policy; the original `0x5c9ae` and `0x5d176` helpers themselves return true unconditionally.

This uses Apple's [MTLDrawable presented handler](https://developer.apple.com/documentation/metal/mtldrawable/addpresentedhandler%28_%3A%29), not command submission or a display callback as presentation evidence.

## Simulation and animation scheduling

`SimulationClock` accumulates integer wall-clock milliseconds into 30 ms native simulation updates. This is based on the actual original first-mission 30 ms one-shot timer request documented in `TIMER_RUNTIME_OBSERVATION.md`. It is a native scheduling policy; the original callback's processing overhead means it is not proof of exact historical 33.333 Hz wall-clock delivery. The original default 1,000 ms delta clamp is retained and counted. Background/resume resets the accumulator instead of catching up while the app is away.

Movement state changes only through simulation updates. Metal drawing interpolates the previous/current exact model translation using the accumulator remainder. Form changes use the new form's exact placement immediately. Display callbacks do not perform extra movement updates.

The sprite loop uses ascending original type order, the recovered form/facing table, immediate pending-transform flag consumption, one-repeat special animation and reverse behavior for new form zero. It preserves the original cumulative doubled delta while an incomplete special animation is active, and skips advancement on a newly selected sequence. A used-action unit selects step zero without resetting elapsed/repeat/reverse/completion state. That required the small `SpriteAnimation::select_step(uint8_t)` extension, reviewed against original `0x72cb4` and covered by two new focused contracts. All 26 animation contracts pass; unchanged prior original start/advance oracle counts are documented in `SPRITE_RENDERING.md`.

Consuming the pending transform flag does not finish a transformation. Original `0x6ce42..0x6ce50` clears global map+0x28 when the **old** animation is the special animation and a new animation has been selected; it makes no comparison with the current unit type. The app records the corresponding special-to-normal transition and reports it only after an actual presented frame, with matching transform and spawn generations. A completed animation state alone, an old drawable, or a new activation of the same type cannot satisfy that response.

## Generic map and roster assets

`MissionScene` supports an empty roster, any unique subset of the 23 original gameplay types, retained inactive entries and subsequent append/update activation. Native playback is optional for an inactive entry. A changed map resource, style, dimensions or tile content rebuilds terrain; a changed type/index roster loads the newly needed descriptors. Textures are cached by original resource ID. Repeated activation of an already-active type resets its playback through `unit_spawn_generation`, and resets movement interpolation to its actual new state.

The real-archive probe loads all 23 mapped descriptors and 19,050 sprite vertices across their frames, checks finite native positions/UVs and animation-table references, and agrees with the independently recorded descriptor metadata. Its first-scene output is identical to the frozen prior reference. Across scripts 310–339 there are 21 distinct opcode9 map/style pairs: ten currently construct native geometry, and eleven stop at the existing explicit style1 animated-terrain error. This is an identified remaining renderer path, not a full-campaign rendering result. Exact pairs and reproduction commands are in `SCENE_ASSET_RELOAD.md` and `scene_catalog_validation/validation.json`.

## Measurements and unresolved effects

The MTKView requests up to 60 Hz and allocates drawables at the attached screen's native scale. The FPS window records only positive, monotonic `presentedTime` values. It reports the actual sample count, frame interval median and p95, native drawable dimensions, and total presented frames. These are scene presentation measurements, not evidence of a full campaign running at 60 FPS.

The report also counts scene/mesh generation changes observed in presented frames. This is explicitly a state/mesh counter, not framebuffer pixel comparison and not a claim of 60 unique animation frames per second.

Audio requests from dialogue skip `{5,18}`, capture `{5,12}`, the recovered transform result and completion are forwarded to the native `G1AppleAudio` host. Startup loads original preload groups 0, 1 and 2. This selects no invented soundtrack. The audio host reports actual scheduling/cache/engine state and errors; output audibility is unverified. Completion text requests remain explicitly pending, while forwarded audio requests are separately identified. Neither forwarding nor a framework callback invents persistence, victory or campaign unlock. The host implementation and its validation boundary are in `NATIVE_AUDIO_OUTPUT.md`.

The report lists pending style1 animated terrain, original camera/font/UI and battle-cinematic fidelity, site visual updates, full sound-event coverage and device audio validation, complete AI/campaign/save integration and iPhone XR validation. The original image assets remain used; no screenshot or fabricated gameplay frame is rendered as the game.

## Validation performed and next gate

- The affected SpriteAnimation source compiled locally with GCC, C++17, optimization and strict warnings. All 26 contracts passed.
- The dialogue/session/camera contract received a bounded independent source review, including the active-gate and token behavior.
- Source-level checks confirmed current `GameSession`, `MissionExecution`, `MissionProgression` and renderer API use.
- The generic portable scene source and real-asset probe compiled with C++17 and strict warnings. The first-scene regression, 23 descriptor metadata comparisons, live-roster identity/error contracts and referenced map/style inventory passed within their stated boundaries.
- Apple Objective-C++ compilation and link are the next required gate. UIKit/TextKit interaction, audio, device behavior and full campaign validation remain separate gates.

`NativeDialogue.hpp` and `NativeBattle.hpp` are included by `NativeApp.mm` and require no additional translation units or CMake targets. The audio integration adds `AppleAudio.mm` and `AVFAudio.framework` to the application target. The app forwards foreground/background state to the host. `SpriteAnimation.hpp/.cpp` and its existing test target changed. `NATIVE_GAMEPLAY_UI.json` records the exact source hashes and this validation boundary.
