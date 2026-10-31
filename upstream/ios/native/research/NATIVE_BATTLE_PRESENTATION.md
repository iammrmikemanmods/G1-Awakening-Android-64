# Native battle interaction and presentation

This checkpoint adds native attack selection and a UIKit battle-result view over the recovered combat/session core. It does not implement or claim the original 3D battle cinematic. No original executable, emulator, screenshot or fabricated combat result is used by this view.

## Actual action and result sources

The Attack button is enabled only when `GameSession::selected_attack_targets()` returns a current valid target. Highlight meshes use the actual terrain top faces at those units' coordinates. A tap is resolved against the current native sprite geometry, then sent to `attack_selected()` with that real unit index. The UI adds no range, damage, random, ownership or action-spent rule.

`GameSession::battle()` supplies a token, attacker/defender indices, both before states and the actual `CombatExchange`. The core has already calculated the attack, optional counterattack, health, immunity/evasion outcome, support list, destruction records and shared RNG use. Those values are preserved in the test report. Native UI animation does not recalculate them.

`NativeBattle.hpp` defines `G1BattleCanvas`, included in `NativeApp.mm` as one translation unit. Original localized unit names come from the same recovered form/name conversion as the gameplay HUD. The panel shows the actual before and final health, attack HP change and optional counterattack HP change. Intermediate health-bar positions are visual interpolation only; they never write gameplay health.

## Completion gate

The native result panel follows an explicit presentation policy:

1. A new battle token resets its view and starts at the actual before state.
2. Its `drawRect:` must execute for that initial state before the native animation timer advances.
3. Attack health interpolation runs after a 400 ms initial hold. A counterattack, when present, starts later. The native timeline lasts 1,500 ms without a counterattack and 2,100 ms with one. These are native UI timings, not recovered cinematic timings.
4. `drawRect:` must execute for the final health/outcome state before Continue is enabled.
5. Only an explicit user Continue action with the matching pending token calls `report_battle_finished(token)`. Time passage alone never acknowledges a battle.

While `battle()` exists, map selection, movement, capture, transform, camera gestures and reset-view input are blocked by UI/core gates. The session's mission update is not advanced during presentation. Sprite drawing can continue independently. Once the user acknowledges the actual final view, the ordinary session tick can observe the resulting real world state and continue the mission as authorized by the recovered rules.

The original map's unit destruction has already been applied by the core, so a destroyed map sprite can disappear behind the native result panel. This is not asserted as original cinematic sequencing. The result panel presents the before/result data explicitly.

## Evidence and limits

The application report records attack requests started, acknowledged battle presentations, native presentation ticks, pending battle data and up to 64 acknowledged exchange records. Each record includes actual type/index identities, before health, attack/counterattack health changes, rolled damage, immunity/evasion, support types, destruction records and random word counts. It identifies the native presentation policy and lack of original cinematic parity.

This is source integration awaiting an Apple compile and device interaction check. Portable combat/session original-function checks are owned and documented by their modules; they are not tests of this UIView. Device acceptance must verify initial/final text visibility, actual target taps, blocked input, counterattack/destruction display, stale-token handling and the Continue gate. No native full-playthrough or sustained-FPS claim follows from this source checkpoint.
