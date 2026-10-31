# Native Showdown and Arena completion — headless evidence

A fresh continuous run completes **all eight Showdown missions**, indices 0–7,
through the native campaign-ending handoff. It starts from the actual completed
Story profile preserved by `STORY_COMPLETION_FREEZE.json`, performs eight real
profile writes, and genuinely unlocks Arena. A subsequent matrix completes
**all twenty Arena configurations**: five maps, both player factions, and human
or native AI opponents. Every match reaches its natural native menu handoff and
leaves the completed campaign profile byte-for-byte unchanged.

This is **headless native gameplay and progression evidence**. World, camera,
dialogue, focus, transformation, battle, results, menu and ending presentation
acknowledgements are synthetic. In particular, Showdown's **228 battle-finished
acknowledgements do not demonstrate rendered battle playback**. The result does
not establish UIKit/Metal/audio output, device lifecycle, frame pacing, touch
input or a physical iPhone XR playthrough.

## Fresh Showdown path

| Measure | Observed result |
| --- | --- |
| Start | New Showdown0 menu selection, actual completed Story profile, seed `0x4741` |
| Restored mission state at start | None |
| Successful mission indices | 0–7, continuously and in order |
| Final handoff | `native_campaign_ending` |
| Next-mission installs / profile writes | 7 / 8 |
| Final profile | `completed = [true, true]`, `highest_unlocked = [17, 8]` |
| Original Story records | Preserved exactly |
| Ticks | 5,915 at 30 ms each |
| Public human commands | 666 |
| Exact in-memory replay comparisons | 643 |
| Written, read-back-checked entry checkpoint files | 8 |
| Native exceptions / search settling failures / reached search budgets | 0 / 0 / 0 |
| Trial-advice human admission rejections | 0 |

The eight entry checkpoint files were written from the actual live session and
read back for exact byte equality. A separate temporary session reconstructed
the same serialized state after synthetic focus acknowledgement. None of those
temporary sessions replaced the continuous live session. The 643 search replay
comparisons are separate in-memory checks, not additional disk reload tests.

| Showdown index | Resource | Human-turn record | Human units / enemies remaining | Native handoff |
| --- | --- | --- | --- | --- |
| 0 | 324 | 17 | 1 / 0 | Next mission |
| 1 | 325 | 26 | 3 / 0 | Next mission |
| 2 | 327 | 128 | 4 / 0 | Next mission |
| 3 | 328 | 3 | 1 / 0 | Next mission |
| 4 | 329 | 7 | 2 / 0 | Next mission |
| 5 | 330 | 133 | 2 / 0 | Next mission |
| 6 | 331 | 4 | 1 / 0 | Next mission |
| 7 | 332 | 6 | 2 / 0 | Campaign ending |

Commands comprise 81 moves, 51 attacks, 13 captures, 29 transformations,
46 heals, 120 upgrades, four productions and 322 human end-turns. The chosen
Showdown5 plan deliberately builds income and upgrades through normal turns
before attacking. This is one legal input path, not an optimal strategy claim.

## Arena configuration matrix

All twenty runs begin through native menu admission using the exact completed
profile produced by the fresh Showdown run. There are twenty checked profile
writes and twenty entry checkpoint write/read-back/reconstruction checks.
There are **eleven player-0 victories and nine player-1 victories**; the claim is
twenty normal match finishes, not twenty player-0 victories.

| Arena index | Faction0, human opponent | Faction0, AI opponent | Faction1, human opponent | Faction1, AI opponent |
| --- | --- | --- | --- | --- |
| 0 | Player1 wins | Player1 wins | Player0 wins | Player0 wins |
| 1 | Player1 wins | Player1 wins | Player0 wins | Player0 wins |
| 2 | Player1 wins | Player1 wins | Player0 wins | Player0 wins |
| 3 | Player0 wins | Player1 wins | Player0 wins | Player0 wins |
| 4 | Player1 wins | Player1 wins | Player0 wins | Player0 wins |

The ten human-opponent configurations use public human commands for both
players, with zero native AI-turn entries. The other ten use the actual native
opponent AI. The input pilot can consult read-only native AI planning advice,
but it never applies that planner's temporary form or statistics substitutions;
all advice must pass the stricter public human command admission.

Across the matrix, 1,270 accepted public commands include 455 moves, 298 attacks,
115 captures, 143 transformations, five upgrades, eleven productions and 243
human end-turns. There are zero native exceptions. Fifty-nine proposed advice
actions were rejected normally by human command admission and skipped. These
were not engine exceptions, state edits or bypassed rules.

The raw probe's generic `strategy` description mentions native opponent AI;
the explicit `arena_opponent`, controller settings and `ai_turn_entries` fields
identify which runs have human opponents. The matrix and this report make that
distinction explicitly.

## Pilot changes and legitimate progression

The Story pilot and all forty files in its freeze remain byte-identical. The
mode pilot is a separate host development tool, `session_mode_playthrough_probe.cpp`.
It adds actual-profile menu admission, the Arena menu choices, entry checkpoint
instrumentation, and a bounded mode input policy. No native gameplay library,
health, random state, objective, ownership, controller flag or campaign unlock
flag was altered.

During development, the tactical pilot could discard an attack that had already
satisfied an objective: it tried to append `end_turn()`, which the native session
correctly rejects while objective completion is pending. The mode pilot now
keeps that winning public-command path immediately. This fixes test input
selection, not a native game rule. Saved-checkpoint continuations first
established legitimate Showdown progress; the final proof then repeated the
entire eight-mission path from a new menu selection with no checkpoint restore.

The final `mode-hybrid` policy uses the existing tactical behavior for indices
0–3 and 7, combat-oriented choices for 4, ordinary economy choices for the first
29 turns of 5, stronger combat scoring until turn129, then public commands
selected through read-only native planning advice from turn130. Index6 also
uses that advice. The first four final handoffs match the previous successful
prefix exactly. Arena uses the owner-agnostic `native-planner-all` input strategy.

Counterfactual search uses unchanged live checkpoints only in isolated trial
sessions, executes the same public commands and real native opponent AI, then
discards the trial states. Only the accepted commands are replayed into the live
session. Exact checkpoint comparisons catch divergence. A menu start gets an
explicit deterministic test seed; no mid-mission random state is replaced.

## Source identity and current build boundary

`MODE_BUILD.json` pins the exact executed v9 pilot source and binary, compiler
flags, copied headers and frozen native libraries. The delivery intentionally
uses that proven v9 source; an unused v10 experiment is excluded.
`MODE_CORE_SOURCE_COMPARISON.json` compares the frozen library source inventory
against the exact staged snapshot for Apple commit `083057b` confirmed by the
parent build process. All 120 existing native source, include and third-party
files match byte-for-byte. The two added music-maintenance headers and Apple
host/UI changes are explicitly outside this pilot's compiled library inputs.
Those presentation/audio changes require their separate Apple and device checks.
Source equality connects the rule implementation; it does not turn a Linux
headless run into an Apple-device playthrough.

The original archive, terrain payload, binary executables and native save bytes
are excluded from source delivery. Their hashes are recorded for reproducibility.
`MODE_DEVELOPMENT_RUNS.json` preserves an index of all 95 completed development
and packaging-check runs, including normal losses and the early v4 entry-capture
instrumentation error. That error occurred before gameplay because the temporary
restored checkpoint had not acknowledged synthetic focus; v5 corrected the
instrumentation. It was not a native engine failure. Final results and full event
logs are retained under `playthrough_validation/mode_completion/`.

## Reproduction

The separate host-only target is included beside the existing Story probe:

```cmake
add_executable(g1_session_mode_playthrough_probe tools/session_mode_playthrough_probe.cpp)
target_link_libraries(g1_session_mode_playthrough_probe PRIVATE g1_resources)
```

After building, use the actual profile produced by the completed Story probe:

```sh
python3 analysis_tools/playthrough/run_mode_probe.py \
  --pilot build/native/g1_session_mode_playthrough_probe \
  --archive /path/to/iphone.big \
  --terrain-tables /path/to/terrain_visual_tables.bin \
  --profile /tmp/g1-complete-story/campaign.g1native \
  --output /tmp/g1-complete-showdown \
  --group 1 --mission 0 --strategy mode-hybrid --policy aggressive \
  --seconds 180 --human-turns 220

python3 analysis_tools/playthrough/run_arena_matrix.py \
  --pilot build/native/g1_session_mode_playthrough_probe \
  --archive /path/to/iphone.big \
  --terrain-tables /path/to/terrain_visual_tables.bin \
  --profile /tmp/g1-complete-showdown/campaign.g1native \
  --output /tmp/g1-arena-matrix --seconds 45
```

Both output directories must be new. The Showdown wrapper returns success only
for all eight successful native handoffs and the genuinely completed profile.
The Arena matrix returns success only after all twenty configurations finish
normally and preserve the profile bytes. A different menu seed or player choice
can produce different gameplay; this evidence is not exhaustive state-space
coverage. Physical iPhone XR playthrough, native-resolution output and sustained
60 Hz presentation remain distinct device acceptance checks.
