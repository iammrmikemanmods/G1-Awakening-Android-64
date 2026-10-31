# Complete native Story execution — headless evidence

A fresh continuous run now completes **all 17 Story missions**, indices 0–16,
through the native campaign-ending handoff. It performs **17 checked campaign
profile writes**; the reloaded profile records Story completed and highest
unlocked 17. The run uses the same frozen gameplay libraries as build 005.
Only the development pilot's input choices changed.

This is a **headless native gameplay/progression result**. All presentation
acknowledgements are synthetic. It does not establish rendered output, audio,
physical iPhone XR execution, frame pacing or a device playthrough.

## Exact observed result

| Measure | Result |
| --- | --- |
| Start | Fresh Story index 0, seed `0x4741`; no restored starting state |
| Outcome | `native_campaign_ending` |
| Successful mission indices | 0 through 16, in order |
| Next-mission installs | 16 |
| Campaign-profile writes | 17 |
| Final profile | `completed[0] = true`, `highest_unlocked[0] = 17` |
| Simulation ticks | 17,422 at 30 ms per tick |
| Public human commands | 1,140 |
| Exact in-memory live checkpoint comparisons | 797 |
| Final-run checkpoint-file writes | 0 |
| Synthetic battle acknowledgements | 476 |
| Native exceptions / settling failures / reached search budgets | 0 / 0 / 0 |

The 797 comparisons verify that replaying selected public commands produces
exactly the same serialized native state as their trial execution. They are
not disk checkpoint reload tests. The separate build 005 save suite covers
actual checkpoint files. Three normal trial-plan admission rejections on
Story index 5 remain from the previous pilot; it continued and won the mission.

## What changed in the input pilot

The previous pilot's first fourteen handoffs remain identical. Its loss at
index 14 was resolved by choosing different legal player inputs, without
changing units, health, random state, objectives, ownership or engine rules.

For index 14, the pilot prioritizes a coherent formation and enemy removal
more strongly than short-term site ownership. It wins through normal native
gameplay, then installs mission 15 and writes the real profile.

Missions 15–16 benefit from keeping the two long-range player units outside
minimum firing distance and selecting stronger attack opportunities. The
search includes sixteen preferred destinations before its edge candidates and
uses a stronger enemy-elimination score. These scores rank test inputs; they
are never injected into the game or used to resolve damage.

The search restores unchanged checkpoints only into isolated trial sessions,
calls the public native commands, settles the actual enemy AI, discards those
trial states, and replays the selected commands into the live session. Exact
checkpoint comparisons detect any divergence. The final proof starts from
Story 0 and runs continuously; it does not assemble a victory from saved
counterfactual states.

## Development and provenance

An actual live checkpoint at Story14's first objective was captured after the
existing successful prefix. Bounded formation policies were tested from that
save; unsuccessful runs are retained. A successful native Story14 continuation
then supplied a real Story15 checkpoint. The range-aware aggressive policy
completed 15–16 from that boundary. Finally, the required fresh Story0–16 run
reproduced the entire path and final profile.

`playthrough_validation/story_completion/DEVELOPMENT_RUNS.json` records the successful and unsuccessful
runs. The same evidence directory contains `ORIGIN.json`, `HEADER_INPUTS.json` and
`RANGED_BUILD.json`, which pin the copied
headers, unchanged frozen native libraries, pilot source and local executable.
The final raw result, event log and reproduction record are under
`playthrough_validation/story_completion/full_story_aggressive/`.
The original game payloads, binary executable and native save bytes remain
outside the source delivery.

To reproduce after building the updated `g1_session_playthrough_probe` target:

```sh
python3 analysis_tools/playthrough/run_complete_story_probe.py \
  --pilot build/native/g1_session_playthrough_probe \
  --archive /path/to/iphone.big \
  --terrain-tables /path/to/terrain_visual_tables.bin \
  --output /tmp/g1-complete-story
```

The wrapper requires a new output directory. It returns success only when the
fresh continuous run records all seventeen native handoffs and the completed
Story profile. Its output always retains the synthetic presentation boundary.

## Remaining validation

This closes the previously unverified full Story logic/progression path for
this one legal input sequence and seed. It does not cover every alternative
player choice. Showdown and Arena completion, current Apple compilation and
Metal output, remaining presentation/audio fidelity, physical-device lifecycle
and saves, native resolution, sustained 60 Hz presentation and an actual
on-device full playthrough remain separate checks.
