# Lawful native Story progression pilot

## Recorded outcome

The frozen hybrid input policy completed **fourteen consecutive Story missions,
indices 0–13**, from a fresh Story0 start. It then took the original native loss
handoff on Story14. The final profile has `highest_unlocked[0] == 14` and
`completed[0] == false`. The fifteen recorded handoffs include the loss; they
must not be reported as fifteen wins or a complete campaign.

The run used the actual decoded original game data and native gameplay,
opponent AI, objectives, shared random stream, checked profile writes and
campaign handoffs. It did not execute the old ARM executable. It supplied
**synthetic presentation acknowledgements** for dialogue, camera, world, focus,
transform, battle, results and menu. It is not a UIKit, Metal, audio, iPhone,
60 FPS, campaign-ending or complete-playthrough validation.

Recorded values from `playthrough_validation/tactical_continuous/result.json`:

| Measure | Observed value |
|---|---:|
| Successful Story handoffs | 0–13, continuously |
| Final outcome | Normal loss on Story14 |
| Native exceptions | 0 |
| Simulation ticks of 30 ms | 16,301 |
| Human public commands | 1,029 |
| Moves / attacks / captures | 348 / 172 / 24 |
| Transforms / heals / upgrades / productions | 176 / 68 / 8 / 22 |
| Human End Turn commands | 211 |
| Checked profile writes, including loss | 15 |
| Successful next-session installations | 14 |
| Counterfactual branch command evaluations | 96,132 |
| Accepted branch commands | 95,512 |
| Exact live checkpoint comparisons | 689 |
| Counterfactual settle bounds reached | 0 |
| Search budgets reached in this run | 0 |
| Measured host wall time | 46.7271 seconds |

Host wall time measures this headless test on the recorded development machine.
It is not device throughput or a rendered-frame measurement. The result hash
is `dec386f94f36c4fdb996654417c5b7642cef3e83c8f8fb2002cc85eedb7b7c1f`.

## What changed

Only the development input pilot changed. The linked gameplay libraries were
copied from the parent's stable 51-suite build with `-ffp-contract=off` and kept
unchanged throughout these policy experiments. The canonical source was left
untouched while the experiment ran. The freeze supplies a self-contained
replacement for `native/tools/session_playthrough_probe.cpp`; it does not
change any game rules or the app.

The `native-planner-tactical` policy preserves the prior
`native-planner-search` decisions for Story0–6, then uses a bounded legal-turn
search for non-position objectives from Story7 onward. The first seven handoff
records—including each tick, active counts and campaign record—are identical
to the prior continuous run that subsequently lost on Story7. Position escape
objectives retain the earlier bounded escape search.

The earlier input strategy used native planning advice only for inactive-unit
objectives. Story7 instead asks for station ownership. New candidate turns
include actual movement, attacks, captures, healing, upgrades and paid
production. They also include the legal sequence **vehicle movement, then
robot transformation at the destination, then an admitted action**. Every
command still passes the normal public `GameSession` admission checks.

The beam uses heuristics to rank candidate inputs; those scores are not game
mechanics and are never stored in gameplay state. It previews the real
opponent's next turn before retaining partial plans. A plan can intentionally
leave some units idle. This is an automated test-input strategy, not a claimed
optimal player or a modification to the recovered AI.

## Checkpoint discipline

Every branch begins with `GameSession::restore_checkpoint` on an unmodified
native checkpoint. The branch invokes public selection/action/End Turn
methods, advances the normal native tick and supplies the explicitly
synthetic presentation callbacks. It does not write unit fields, hit points,
ownership, controller flags, objective state, command cursors or RNG words.

No branch checkpoint replaces the live session. Only the selected sequence of
public commands is replayed. After each settled command boundary, the live
checkpoint encoding must be byte-for-byte identical to the recorded branch
checkpoint. A mismatch throws and records `native_exception`; none occurred
in the frozen continuous run. This includes random and mutable terrain state.

The inherited JSON key `escape_search` contains combined escape/tactical
counters. Its `solutions` count means replayable input plans, **not mission
victories**. Campaign success is counted only from actual checked
`CampaignController` handoffs. The final result explicitly records this scope.

Tactical search limits are 8,000 branch commands, one human turn plus the real
opponent response, six wall seconds, a partial beam of eight, and at most 32
response candidates per actor. The whole run was bounded by 60 wall seconds,
200,000 ticks, 150 human turns per mission and 17 mission handoffs. The recorded
run hit its normal loss before any of these limits. Machine speed can change
which candidates are considered if a wall-time search bound is reached on a
different host; such a bounded result must remain labelled as bounded.

## Reproduce against a built native core

The original archive and small extracted terrain tables are local inputs, not
part of this source handoff. Their SHA-256 values are:

- `iphone.big`: `663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4`
- Terrain tables, 480 bytes: `b870e3d74244741003c8068178e7f58bb5fbe35599599c918e024227463725a8`

Build the normal `g1_session_playthrough_probe` CMake target if present in the
staged source. Alternatively, link the frozen single source to the matching
native libraries with C++17, `-O2 -ffp-contract=off`, and zlib. The strict source
validation also used `-Wall -Wextra -Wconversion -Werror -pedantic`.

```bash
python3 analysis_tools/playthrough/run_strategy_probe.py \
  --pilot /absolute/path/to/g1_session_playthrough_probe \
  --archive /absolute/path/to/iphone.big \
  --terrain-tables /absolute/path/to/terrain_visual_tables.bin \
  --output /absolute/path/to/a-new-run-directory
```

The wrapper records the actual binary and input hashes, exact command, exit
status and observed handoffs. It validates inputs and never alters them.
The underlying command is:

```bash
g1_session_playthrough_probe \
  --archive /absolute/path/to/iphone.big \
  --terrain-tables /absolute/path/to/terrain_visual_tables.bin \
  --output /absolute/path/to/a-new-run-directory \
  --strategy native-planner-tactical --group 0 --start-mission 0 \
  --seed 0x4741 --max-missions 17 --max-ticks 200000 \
  --max-human-turns 150 --max-seconds 60
```

The recorded executable SHA-256 is
`28ae8ac7c951ca1cd4d43e81ae1df5f9bba3035d56e3467a213e0aa2f64640e1`.
The copied native resource library SHA-256 is
`95d734aa7d6e407b0fbd491a96a1fe3100b14029099bdccc0cb9e86498889e1e`.
The build records retain the other library hashes and original source-input
hashes. A later staged core must be rebuilt and rerun before associating this
outcome with that later binary.

## Retained development failures and limits

The development metadata includes all prior isolated Story7 attempts. The old
direct/economy/coordinated/native-planner input policies lost normally. The
first tactical variants and the native-planner-for-all-objectives variant
reached their human-turn bound without winning. They remain evidence; they
were not overwritten by the successful continuous prefix. A cold isolated
Story7 start has a different RNG history from continuous progression and must
not be described as an equivalent starting state.

Historical source versions are retained with their matching include fragment
and result/event hashes. The final frozen destination source inlines that
fragment for a single-file CMake target. The historical result files used the
older generic search-limit fields; the development catalog records the exact
tactical limits separately. No native save binary, compiled executable, static
library, original executable or original archive is needed in the repository
payload. The actual checked profile file remains local to its recorded run.

Remaining gates include winning Story14 and then reaching/completing Story15
and16, complete Showdown/Arena runs, actual presentation and audio, Apple
compilation of the combined checkpoint, and XR/iOS17.5 device acceptance. The
pilot's normal losses alone do not establish a core defect or correctness of
every gameplay branch.
