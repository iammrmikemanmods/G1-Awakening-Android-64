# Observed first-mission timer request

The original game requested a **29.999999329447746 ms NSTimer interval** throughout a directly observed loaded first-mission window. This is now a runtime observation of the original call arguments, rather than an inference from an FPS counter, a constructor, or a seeded oracle case.

The test was development-only. It ran the unchanged original IPA in the already-built, frozen Linux diagnostic runtime, using an isolated byte-identical copy of the prior mission save. No runtime rebuild, old-game patch, game-state injection, or PC-project modification was made. The native product does not include that runtime.

## Evidence

Two independently viewed captures, `frame_002040.png` and `frame_002640.png`, show the original first mission with yellow Bumblebee selected, the movement grid, HUD, and original action controls. Exactly **600 complete SetTimer calls** occur between their log markers. Every one reads `CCore_Cocoa + 8 = 0` and passes the same double argument to `scheduledTimerWithTimeInterval:target:selector:userInfo:repeats:`.

| Observation | Value |
|---|---|
| Flag load | Original Thumb `0x28f54`, `ldrb r3,[r3,#8]` |
| Core object in this run | `0x30024be0` |
| Observed flag | `0` for all 600 bracketed calls |
| Original call site | `0x28f7a`, before `objc_msgSend` |
| R2, low half of double | `0xe0000000` |
| R3, high half of double | `0x3f9eb851` |
| Double bits | `0x3f9eb851e0000000` |
| Interval | `0.029999999329447746` seconds |
| Bracket log lines | 16,449 through 21,259 |
| All timer samples in the run | 4,594; the same interval in every sample |
| Input | Three observed prompt taps: sound YES, title start, continue YES |
| Stop | Original 300-second supervisor bound; 300.293 s total |

The game and diagnostic executable hashes remained unchanged. All 21 checked runtime-source hashes still match the frozen build. The actual fetched instruction prefixes at `0x28f54`, `0x28f56`, `0x28f78`, and `0x28f7a` match the original executable bytes. This is an instruction-prefix check at the observed sites, not a hash of all relocated live memory.

The compact evidence record is [timer_runtime_observation.json](timer_runtime_observation.json). Full local evidence remains under `research/native_timer/runtime01_probe/`, including `runtime.log`, `result.json`, `source_integrity.json`, the input record, and the captures. The original executable SHA256 is `0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`; the unchanged diagnostic executable is `be938215ab4a7b091aa9c4d13fc39cc06c39af7ff965bf4efc7103bb3db40fda`.

## Implication for native timing

This establishes the **requested 30 ms delay in the tested first-mission state**. It does not establish exactly 33⅓ engine updates per second. Original `HandleTimer` at `0x29380` calls the engine dispatcher and only then re-arms the next one-shot timer. Callback work and scheduling add to the interval. The diagnostic trace also single-steps the CPU, so its wall-clock FPS must not be used as a target rate.

The recovered movement routine advances a fixed amount per engine update. A 60 Hz native display callback therefore cannot simply invoke that routine once on every display frame without addressing the change in simulation speed. The observed timer request is a concrete input to a separate native simulation-clock decision. Movement-duration parity, other game modes, physical-device timing, and sustained 60 Hz presentation still need their own validation.

This also resolves a misleading static lead. Core vtable slot `+8` at `0x1a740` writes an integer 10/30 into the separate bridge object at global `0xd8cf0`; SetTimer reads the core object's byte at global `0xd8cf4`. Those setter calls alone did not establish the active NSTimer branch. The runtime register evidence above does so for the bracketed state, without assigning a meaning to unobserved writes or uninitialized memory.

## Reproduce the analysis without running the old game

From the project root, with the local trace and original executable available:

```sh
python3 analysis_tools/timing/trace_summary.py \
  research/native_timer/runtime01_probe \
  --first-gameplay-frame 2040 --last-gameplay-frame 2640 \
  --original-executable research/binary/G1awakening.armv6 \
  --output timer_trace_summary.json
```

The script checks each observed instruction prefix against the original bytes, pairs flag loads with timer-call registers, decodes the double, verifies the original branch relation, and counts only samples bracketed by the explicitly supplied, viewed captures. It never infers timer values from frame rates. The native evidence record adds run-integrity and visual-review metadata to this reproducible summary.

The run ended at its bound and the game handled termination with exit code zero. That exit is not a full-game success result. No combat, campaign completion, save-state change, or device performance was established. Only source and metadata belong in the native repository; the diagnostic runtime, original data, saves, and screenshots remain development-only artifacts.
