# Native gameplay input pilot — frozen checkpoint

## Result

The final source-native host run completed **seven consecutive Story missions, indices 0–6**, through real gameplay objectives, checked profile saves and native next-mission installations. It then lost index 7, resource 338, after 51 human turns in that mission. The persisted Story unlock index is 7 and both campaign-completed flags remain false.

The separate final matrix started all 30 missions independently with a fresh profile and fixed seed. Every run reached an actual native campaign handoff: **13 success branches and 17 losses**, with zero native exceptions, replay mismatches or test bounds. An isolated success or final-mission ending does not establish a complete campaign.

This is **headless native integration evidence**. World, camera, dialogue, transform, battle, results and menu feedback is synthetic. No original ARM executable, interpreter or compatibility host is involved. UIKit/Metal/audio, physical input, iPhone XR/iOS 17.5 performance and full rendered playthrough remain outside this result.

Machine-readable summary: [PLAYTHROUGH_PILOT.json](PLAYTHROUGH_PILOT.json). Complete final command/input/output records are in [FINAL_CONTINUOUS.json](playthrough_validation/FINAL_CONTINUOUS.json), [continuous_events.jsonl](playthrough_validation/continuous_events.jsonl) and [FINAL_INDIVIDUAL.json](playthrough_validation/FINAL_INDIVIDUAL.json).

## Continuous result and actual state changes

| Item | Final recorded result |
|---|---:|
| Seed | `0x4741` / 18241 |
| Native ticks | 5,920 × 30 ms |
| Human moves | 87 |
| Human attacks | 90 |
| Human captures | 11 |
| Human transformations | 47 |
| Human production actions | 9 |
| Human end-turn commands | 100 |
| Real opponent AI turn entries | 99 |
| Actual checked profile writes | 8, including the loss result |
| Actual next-session installations | 7 |
| Native exceptions / replay mismatches | 0 / 0 |

Each human action uses the public `GameSession` API. The probe never edits the live roster, HP, ownership, objective state, command cursor, counts, controller flags or RNG. Production and capture occur through their real action methods; enemy deaths result from native combat. Mission progression is observed from the actual objective evaluator and `CampaignController`, including a successful real filesystem write. A presentation response does not satisfy a gameplay objective.

The final continuous result was reproduced from the frozen source after adding reporting counters. Native outcomes, final state, action counts and mission records matched the preceding successful seven-mission run; only wall-clock timing and reporting fields were excluded from that comparison.

## Escape puzzle and bounded input search

Story index 6 switches to an unarmed escape objective at script command 37. Initially occupied narrow corridors defeated the first simple navigation policies. The final test strategy searches exact public checkpoint copies in separate native simulation instances. Only `select`, `transform_selected`, `move_selected_to` and `end_turn`, followed by real native opponent turns, change those simulations.

The search never installs a simulation's resulting state into the live game. It replays the chosen command sequence on the original live session. Each completed intermediate step must equal the entire encoded checkpoint predicted by the simulation, including unit state, RNG and terrain state. The final movement has no objective checkpoint because the actual game has already advanced out of that gate.

In the continuous run, all **6,481 branch command evaluations were accepted**, none hit the settle bound, and **22 live checkpoint comparisons matched**. The isolated escape run independently evaluated 6,395 accepted branch commands and matched 23 live checkpoints. Both reached actual next-mission handoff and profile persistence.

Bounds are 30,000 command evaluations, ten complete turns and ten seconds per search. The method is a beam search with deliberate pruning, so a failed search would be inconclusive. It applies only to the recovered unarmed wildcard-position objective with at most two active human units.

Other strategies use geometric distance, capture/production priorities, formation ordering or read-only advice from the native AI planner. The AI planner's proposed temporary form/stat changes are not applied directly. Human admission remains authoritative. A temporary geometry-only board copy helps estimate a future path around moving enemies; it is never used as the live movement board or installed as ownership state.

## Final isolated matrix

Indices below are zero-based. Each row is a separate start, not a continuation of the preceding row. `Next` is a successful native next-mission handoff followed by the requested one-mission test limit; `Ending` is an isolated final-mission ending; `Loss` is the real loss/menu branch.

| Group | Index | Resource | Result | Human turn counter |
|---|---:|---:|---|---:|
| Story | 0 | 315 | Next | 3 |
| Story | 1 | 326 | Next | 4 |
| Story | 2 | 333 | Next | 9 |
| Story | 3 | 334 | Next | 8 |
| Story | 4 | 335 | Next | 5 |
| Story | 5 | 336 | Next | 15 |
| Story | 6 | 337 | Next | 11 |
| Story | 7 | 338 | Loss | 51 |
| Story | 8 | 339 | Next | 29 |
| Story | 9 | 316 | Next | 5 |
| Story | 10 | 317 | Next | 6 |
| Story | 11 | 318 | Loss | 22 |
| Story | 12 | 319 | Loss | 17 |
| Story | 13 | 320 | Next | 36 |
| Story | 14 | 321 | Next | 20 |
| Story | 15 | 322 | Loss | 15 |
| Story | 16 | 323 | Ending | 5 |
| Showdown | 0 | 324 | Loss | 19 |
| Showdown | 1 | 325 | Loss | 7 |
| Showdown | 2 | 327 | Loss | 23 |
| Showdown | 3 | 328 | Loss | 4 |
| Showdown | 4 | 329 | Loss | 12 |
| Showdown | 5 | 330 | Loss | 12 |
| Showdown | 6 | 331 | Loss | 3 |
| Showdown | 7 | 332 | Loss | 7 |
| Arena | 0 | 310 | Loss | 8 |
| Arena | 1 | 311 | Loss | 4 |
| Arena | 2 | 312 | Loss | 8 |
| Arena | 3 | 313 | Loss | 10 |
| Arena | 4 | 314 | Loss | 7 |

All 30 final runs used the same frozen probe executable and each had zero reported errors. Losses describe the test strategy's actual outcome. They do not by themselves prove a native implementation defect. Conversely, reaching those outcome branches does not establish that every successful path, mechanic or human presentation is complete.

## Earlier attempts and corrected test-input bug

The first attempt is preserved unchanged in [BASELINE_PILOT01_RESULT.json](playthrough_validation/BASELINE_PILOT01_RESULT.json) and [BASELINE_PILOT01_PROVENANCE.json](playthrough_validation/BASELINE_PILOT01_PROVENANCE.json): five consecutive wins, then a loss in Story index 5. Later legal input ordering extended that to six; the escape search extended it to seven.

During broader economy-policy exploration, the probe treated a nonempty `heal_selected` result as success even when its typed outcome was `insufficient_funds`. This produced repeated requests at zero funds and inflated old heal attempt counters. The final source checks `HealOutcome::healed` and records before/after HP and actual funds spent. No game-core rule changed for that correction.

Three affected cases were rerun with the corrected probe. One reached a real loss, one a successful next-mission handoff, and one continued for 101 human turns until its explicit turn bound. The latter is a strategy-bound result, not a verified victory or a stationary native stall. See [CORRECTED_HEAL_CASES.json](playthrough_validation/CORRECTED_HEAL_CASES.json). Historical heal counts are not used as evidence of restored HP.

## Build and reproduction

Strict C++17 compilation passed with `-Wall -Wextra -Wconversion -Werror -pedantic -O2`. The tool source and all linked static libraries were hashed before and after compilation and were unchanged. Compiler identity, exact build arguments and hashes are in [FINAL_BUILD.json](playthrough_validation/FINAL_BUILD.json).

- Probe source SHA256: `099b0964bec4efd50b60136e16db1f08419a661dd835fd8a3b21d14fe797b1b6`.
- Linux host executable SHA256: `7db60e84f3f268d7fdd04bb1fecaac8aff126daa7dc110b174410e6bc3a4a2cf`.
- Original archive SHA256: `663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4`.
- Linked native core SHA256: `ad01504b64cc5ac421f409bce3ef08797e53b64174359b9bb1bcfc99cb246c0c`.

The host-only source is `native/tools/session_playthrough_probe.cpp`. The build owner can add `g1_session_playthrough_probe` linked privately to `g1_resources`; it must remain outside the iOS application target. Reproduction instructions and the standard-library-only metadata wrapper are in `analysis_tools/playthrough/README.md` and `run_probe.py`. The wrapper's own one-mission smoke run passed and confirmed unchanged source/executable hashes: [WRAPPER_SMOKE.json](playthrough_validation/WRAPPER_SMOKE.json).

The fixed 30 ms tick schedule and immediate synthetic presentation responses are part of the test input. Different real presentation pacing can change shared RNG timing and outcomes. Wall-clock runtime here is not a graphics, FPS or original-speed measurement.

Commit the probe source, reports and JSON/JSONL metadata only. Original resources, 480-byte table payloads, compiled executables and `*.g1native` save files remain local. No remote actions were performed by this probe.

## Remaining acceptance gates

- Complete all 17 Story missions continuously with actual wins and campaign completion.
- Complete the eight Showdown missions and five Arena scenarios through successful gameplay outcomes.
- Exercise real UIKit/Metal/audio and touch presentation on the intended iPhone XR running iOS 17.5.
- Verify device save/resume, lifecycle, sustained 60 FPS/pacing and original gameplay-time behavior.

The source-native gameplay path has progressed beyond initial mission prefixes, but those remaining gates are still unverified.

