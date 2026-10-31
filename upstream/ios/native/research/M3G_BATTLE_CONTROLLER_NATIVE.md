# Native ordinary battle controller

This component implements the recovered ordinary battle phase machine in C++.
It is consumed by `M3GBattlePresentation` and the Apple app. The product does not
load or execute the original ARM executable, use a CPU interpreter, or translate
instructions at runtime.

## Evidence and scope

The reference is the unchanged-original execution recorded in
`M3G_BATTLE_CONTROLLER.md` and
`m3g_validation/original_battle_controller.json` (SHA256
`2a0d24678c03d9a88942e021a8cf8eeafb0577e9dc2c8923c2ac9d368e35cca2`).
The development-only recorder executes original 0x413e8 and the original clip
helpers with original ARMv6 libgcc; public graphics, audio and text calls are
recording boundaries. It is not part of the native build or app.

`analysis_tools/m3g/battle_controller_fixture.py` verifies that JSON hash before
producing the compact native test protocol. The native test compares all 295
isolated updates, all 930 updates in 10 complete ordinary sequences, and 159 draw
plans: **1,225 updates, 159 draw plans, 82,600 field checks, zero differences**.
The combined host CTest run also passed all 53 registered suites. These numbers
are arithmetic/event/visibility evidence, not rendered-output or device evidence.

The public API is `M3GBattleControllerConfig`, `M3GBattleControllerState`,
`advance_m3g_battle_controller` and `m3g_battle_draw_plan`. It covers ordinary
phases 0–12. Viewer phase 13 and the outer tactical-support sequence are not
silently treated as ordinary combat. Negative native elapsed time, invalid
forms/viewport, and unrepresentable HUD conversion are explicitly rejected.

## Preserved behavior

The controller retains exact phase thresholds, old-time/new-time audio
crossings, the half-delta hit phases, discarded phase overshoot, clip restart
policy and ordered public effects. Model clip arithmetic uses
`M3GBattleTimeline`: below the original float endpoint it samples the old clock,
then stores the sum; nonrestarting clamp does not overwrite the stored clock;
restart samples/stores the clip start and discards overshoot.

The caller must replay clock changes and pose/camera effects in event order.
`M3GBattlePresentation` does this. Using only the final clocks to replay earlier
camera selections would change the original result. The observed phase 12
behavior is preserved literally: model A is advanced with clock B and clip 4.
Health text changes directly to the real outcome at its recovered event; there
is no added HP tween. Death and actual-counterattack flags come from the resolved
exchange, not guessed attack eligibility.

The draw plan retains per-phase A/B/shot/hit/death visibility. The World and
participant calls remain separate submissions as established by
`M3G_BATTLE_RENDER_PASS.md`; no global all-assets sort replaces them.

## Two explicit corrections to the frozen prose

The original JSON and instructions were retained unchanged. During native parity
checking, two prose descriptions in the earlier frozen controller report were
found to be inaccurate:

1. **Phase 7 HUD slide:** original 0x42588 divides `float(old_elapsed)` by 1000 first;
   original 0x42590 then multiplies by `float(screen_width <<17)`. The result is
   truncated and negated. Multiplying before dividing changes rounding. For
   width 320/old 998, the original result is −41,859,156; the reordered expression
   gives −41,859,152. The native code follows the recorded instruction order.
2. **Phase 5 A-hit sound:** original 0x42326 supplies event 10, so the request is
   `(operation 5, argument 10)`. Phase 2 B-hit supplies event 11. The earlier prose
   described both as 11; the recorded events and native implementation distinguish
   them.

Independent source review by the original-reference author found no remaining
concrete discrepancy in this bounded phase/event/draw implementation. That review
is separate from the executed comparisons.

## Scheduling and completion

The app uses the existing 30 ms `SimulationClock`, independently of its requested
60 Hz display cadence. This is the declared native scheduling policy based on the
observed original 30 ms timer request; the original one-shot callback overhead is
not reconstructed as an exact wall-clock guarantee.

The controller's `complete` state only states that its natural sequence ended.
It does not acknowledge `GameSession`. The Apple host separately requires a
successful Metal command, positive matching drawable presentation timestamp,
current battle token/revision, and a drawn final HUD revision. There is no
invented Continue step or fallback acknowledgement for unsupported assets.

## Reproduction

Configure/build the native CMake project, then run:

```sh
ctest --test-dir BUILD_DIR -R '^m3g_battle_controller_original_vectors$' --output-on-failure
```

The project propagates `-ffp-contract=off` for supported C++ compilers, keeping
separate float32 operations from being contracted into ARM64 FMA. This does not
turn host trigonometric results into a claim of historical iOS bit identity;
those older sampler limitations remain documented in their own reports.
