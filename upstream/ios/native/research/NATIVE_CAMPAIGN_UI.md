# Native campaign UI checkpoint — 0.4.0 / build 4

This checkpoint connects the native iOS application to the recovered campaign, turn, action, world-presentation and save APIs. Its artifact kind is **native campaign integration test**. These sources are ready for the next real Apple compile/link. This record does not claim that this particular checkpoint has passed an Apple build, run on an iPhone, sustained 60 FPS, completed a full playthrough, or proven audible output or cold-launch persistence on a device.

The application is native C++/Objective-C++ with Metal and UIKit. Its inputs are the original resource archive and verified terrain metadata. No original executable, CPU interpreter, JIT, compatibility host or development oracle is linked into the application.

## Campaign and world presentation

The startup menu reads the durable campaign profile. Story is group 0, Showdown is group 1, and Cybertron Arena is group 2. Original localization supplies those mode names and unlock descriptions. Showdown requires `profile.completed[0]`; Arena requires `profile.completed[1]`. Story and Showdown use human Autobots versus computer Decepticons. Arena exposes explicit faction and human/computer opponent choices. These are native controls using the original mode definitions and recovered configuration fields, not a reconstruction of every legacy menu callback or its artwork.

The host calls `bootstrap_campaign(..., &terrainTables)` and installs the resulting `GameSession`. An initially empty roster is supported: some scripts request the camera before activating units. Reload uses stable roster indices, original type identity, spawn generations and the applied map/style/grid. Playback resets when the corresponding identity or generation changes.

The core applies actual world operations before exposing `world_presentation()`. The UI loads scene resources, draws sites/items/points/markers, sends recorded map audio and displays the original title. It never replays those operations as mutations. A world token is acknowledged only by a current Metal drawable actually presented, with matching scene revision, current spawn playback and a title label whose current text has been drawn. Old callbacks cannot complete a new scene or session.

Site squares, item diamonds, link indicators, points and markers are explicitly native presentation. They use recovered coordinates and current capture-network ownership; they are not claimed to be original power-station models or marker art. Original terrain, unit sprites, atlas rectangles, sprite animation and localization remain the underlying assets.

## Actions, turns and input

The UI exposes Move, Capture, Transform, Attack, Repair, Activate Unit, Activate Tape, Upgrade, Undo and End Turn through authoritative `GameSession` APIs. Eligibility comes from the core. The UI does not declare objective success, assign battle outcomes, capture a site by moving onto it, or write funds directly.

Move uses the original range/route authorities and confirms an ordinary second tap on the same destination. Occupied targets reach the core's recovered friendly boarding/transport admission; the UI no longer rejects every occupied tile. Projected native input-cell geometry covers logical tiles even when style 1 omits their ordinary top face. It is an input/selection surface, not replacement terrain artwork.

Activate Unit / Activate Tape uses `ProductionConfirmation::direct_type_list`: actual candidates, signed costs, then exact adjacent placement. It deliberately does not add an affordability filter to that original branch; the core applies its recovered debit/clamp behavior. No invented cargo count is displayed. Session and spawn generations reject stale list callbacks, and simulation pauses while that native selection sheet is open.

Undo consumes the actual `UndoResult`, including cursor, selection policy and sound. The core owns restored movement/form state and friendly-selection locks. The renderer neither fabricates a reverse route nor starts a recorded special transform merely because Undo reversed a form. A Wait button remains absent because its original menu route is not established.

End Turn follows current core authorization. The AI driver owns its actual move/attack/capture/heal/upgrade/production flow. Camera focus and transformation use separate presentation tokens. Consuming pending transform byte 2e does not signal completion: the recovered special-to-normal transition must appear in a current presented frame, with matching transform and spawn generations.

Attack retains a native battle panel for the actual before/result HP, exchange and counterattack. Initial/final drawing and real user acknowledgement precede release of the battle token. This is not original animated 3D battle presentation. The separate rigid M3G decoding slice in `M3G_RIGID_SCENE.md` is not substituted for that missing renderer.

## Timing, dialogue and display observations

Simulation uses a fixed 30 ms native update policy based on the observed original request. Movement/actions advance only in simulation. Metal targets 60 Hz and interpolates preceding/current movement state. The original one-shot timer also included processing before rearming, so this is not an exact historical wall-clock-speed claim; see `TIMER_RUNTIME_OBSERVATION.md`.

Dialogue uses real TextKit line layout and glyph drawing. Core reveal/page state and input masks 1/16/128 remain authoritative. The host reports measured geometry and only marks its current reveal drawn after the matching draw revision. Session installation clears input/dialogue, battle, camera, transform and drawable caches.

The view records positive, finite, monotonic drawable `presentedTime` observations, interval distributions and native drawable dimensions. Scene/mesh generation observations are separately labelled. Nominal display frequency or an update counter is not treated as 60 delivered, changing game frames. XR performance and long-duration game timing remain device gates.

## Terrain and shared random state

All 21 original map/style pairs now construct the portable native scene, including the 11 style 1 pairs previously blocked at animated face 14. The exact record is `scene_catalog_validation/with_animated_surface/validation.json`. All 23 descriptors and their 19,050 finite sprite vertices load; the frozen first-scene JSON is unchanged. These are data/geometry checks, not GPU or playthrough results.

Style 1 loads the separate original 256×32 resource 420 material and applies phase/8 U translation only there. Transient slots are batched before its fixed faces, matching original mesh indexing. The base atlas is unshifted.

The host reads `terrain_state()`, `terrain_layout()`, `terrain_tables()` and `terrain_generation()`, uses pure `terrain_decoration_quads()` and caches by world/terrain generation. It never creates/advances the pool or draws random values. The core initializes during opcode 9 before later random spawns, advances in simulation and serializes the pool with the shared RNG. Terrain-only changes do not request checkpoint writes or movement-range rebuilding every phase. Style 0/2 decorative sprite pools remain separately scoped recovery work; no complete RNG/presentation parity is claimed for them.

## Checkpoints and campaign handoff

Application Support uses `G1AwakeningNative/session.g1native` for the session and `campaign.g1native` for the profile. Only `checkpoint_available()` boundaries are written. Busy actions retain the old checkpoint and a pending request. The core atomic helper performs writes; identical bytes are not rewritten. Confirmed bytes/counters change only after success. Failure exposes Retry and retains the previous file.

Continue validates the archive-bound save, decodes with terrain tables, restores the core and installs through the cache-reset boundary. Restored world requests are presentation-only and never replayed as writes. A load/decode error preserves the file and offers Retry/Menu, with no silent first-mission fallback. Backgrounding requests a save only at an eligible boundary.

Campaign handling begins only at actual stopped opcode 10. The controller writes the profile first; failure blocks handoff. Groups 0/1 expose real results through a native canvas. Its actual draw token and positive height feed the recovered state; Continue queues original mask 16. Results input/time is consumed only in simulation steps. Group 2 preserves immediate handoff after successful save.

Next-campaign preparation retains cached random state. The replacement core and actual scene are installed before retirement of the old checkpoint, and retirement succeeds before handoff acknowledgement. This keeps Continue available if resource/GPU allocation throws. Retirement moves bytes to a unique same-directory `.retired` file; failure blocks and remains visible. No directory-fsync claim is made for this rename. Menu/ending likewise retire stale checkpoints only following successful profile save and require actual native canvas drawing. Ending text tags resolve their low 16 bits directly through the text table.

Stale drawable acknowledgements and command-buffer errors are rejected by scene revision. Matching current tokens and generations are required for all core presentation responses. No timeout or unsupported no-op is substituted for completion.

## Audio preferences

Recorded map/script, dialogue-skip, capture, transform, repair, production, upgrade, Undo and completion requests reach `G1AppleAudio`. Original preload groups 0/1/2 are loaded. The recovered dispatcher determines resource/order; events remain one-shot with no invented music loop.

Music and Sound are persisted native preferences, initially On. These cold defaults are native policy. The host filters categories independently because original `a3 || a4` does not itself provide independent switches. Disabling one stops its voices while retaining the other. Conditional map-stop behavior still honors its original gate. Real framework scheduling/completion/error observations appear in the report; device audibility and original mixer parity are unverified.

## Verification and remaining work

Bounded independent review checked current-session draw tokens, results input, controller ownership, cached preparation and save failure order. Final source checks resolve all 54 `GameSession` calls and 34 zero-argument Objective-C selectors, verify 0.4.0/build 4, and reject private host terrain mutation or guest-runtime references. These checks are **not an Apple SDK compilation**. `NATIVE_CAMPAIGN_UI.json` and `NATIVE_CAMPAIGN_UI_FREEZE.json` pin the source and evidence. Core/oracle and hosted Apple results are maintained separately by root.

The next gate is a real hosted Xcode build, then XR/iOS 17.5 interaction, sound, interruption, checkpoint/Continue and performance testing. Remaining visuals include original animated M3G battles, site/marker art, historical camera/UI/fonts and style 0/2 decorative pools. Full mission/campaign playthrough, every control path, original audiovisual fidelity, sustained refresh and device persistence still require validation. The app's test report states these boundaries.
