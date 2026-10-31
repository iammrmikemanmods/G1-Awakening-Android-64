# Native mission and battle music maintenance

This follow-up recovers the missing periodic music requests for an active map
and an ordinary 3D battle. It does not change the existing audio dispatcher,
decoded resources, gameplay state, simulation cadence or save format.

## Recovered behavior

The ordinary battle update tail at `0x428a4..0x428c6` checks the application's
music-enabled byte `+0xa4`. If enabled, it calls the original operation-6 audio
query. Only an idle result calls the map's style helper `0x4e7b8`.

The active map-update body at `0x5b5a4..0x5b5e8` uses the same enabled/query/idle
sequence. It additionally excludes map interaction mode `+0x2d8 == 7` and
application mode `+0x1c0 == 10`. The original outer map dispatch already checks
application mode 100 at `0x5b304..0x5b30e`; the inner mode-10 guard also catches
a menu transition during that update. Results mode 7 is independently recovered
at `0x4d5c4..0x4d5ea` and documented in `MISSION_PROGRESSION.md`.

The real style helper reads signed `map+0x22c`. Style -1 produces no request;
style 0 requests `(5,1)`, style 2 requests `(5,2)`, and every other non--1 value
requests `(5,3)`. The helper's separate disabled-music stop branch is unreachable
from these two maintenance sites because their enabled guard has already passed.
The independent map-install path retains its existing disabled-music stop.

Operation 6 queries whether **any native voice** is playing. This is not a
music-only check. A still-playing shot, voice or other effect postpones the
background request. No duration estimate, continuous loop option or operation-4
resume call appears in these recovered maintenance bodies.

## Native integration

`AudioMaintenance.hpp` represents the exact guards and prospective request as
a small read-only plan. It does not query audio, consume RNG, mutate the session
or assert playback. Its query flag remains true for style -1 when the original
guard permits a query; the style sentinel only removes the later play request.

The proposed Apple host method is called after an actual 30 ms battle update
and its emitted audio cues, or after an active map update and its emitted audio
and message transitions. It queries `G1AppleAudio` operation 6 on the main queue
and sends the planned original request only when output is actually idle.

The native active-session branch corresponds to original application mode 100.
Existing native panel/type-list guards exclude menus and results. The method
also excludes stopped mission handoffs until their actual save/results/menu
owner runs, and rejects a map-maintenance call once a battle has taken ownership.
Those are explicit bindings to this native host, not claims that the complete
original UI state machine has been reconstructed. The battle call occurs only
after a real controller advance, once its initial frame has been presented.

The existing audio output code still schedules one buffer at a time, clears
voice activity after actual played-back completion, and invalidates stale
callbacks when buffers are stopped. The next permitted simulation update can
therefore request the next original background playback without inventing a
sample-count clock or silently overlapping another voice.

## Original-function parity

The development oracle executes unchanged original ARM instructions for both
maintenance bodies, the real singleton accessor `0x62d1c`, the argument-zeroing
wrapper `0x2ad6c`, and style selector `0x4e7b8`. Its only new boundary is the
existing audio dispatcher entry `0x2ad12`: requests are recorded and operation 6
receives the independently supplied output-playing value. The existing audio
dispatcher has its separate 512-case original-function validation.

The original image and support library are hash-checked, executable text is
read-only and rechecked, and every case verifies that complete synthetic map
and application storage are unchanged. There are **1,280 cases and 2,752 scalar
comparisons**, with zero differences. They cover both call sites, enabled and
disabled music, idle and busy output, five interaction modes, four application
modes and eight signed style values including -1 and integer extremes.

`audio_maintenance_test.cpp` compiles as C++17 with `-O2 -Wall -Wextra
-Wconversion -Werror -pedantic` and passes the generated fixture. The proposed
CMake registration adds one portable suite. The Apple method has not been
compiled or run by this Linux validation; it needs the hosted Apple gate.

Exact results and file hashes are in
`audio_event_validation/music_maintenance_validation.json`.

## Reproduction after integration

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/native --target g1_audio_maintenance_tests
ctest --test-dir build/native -R '^audio_maintenance_original_function_vectors$' --output-on-failure
```

Regenerating the reference requires the explicitly supplied original executable,
its pinned libgcc and a local Unicorn installation. They are development inputs,
never linked into or packaged with the native app:

```sh
python3 analysis_tools/audio/music_maintenance_oracle.py \
  --binary /path/to/G1awakening.armv6 \
  --libgcc /path/to/libgcc_s.1.dylib \
  --unicorn-dir /path/to/unicorn/python \
  --base-oracle analysis_tools/combat/oracle.py \
  --native-probe build/native/g1_audio_maintenance_tests \
  --fixture native/tests/audio_maintenance_cases.txt \
  --report native/research/audio_event_validation/music_maintenance_validation.json
```

## Remaining limits

This closes the active mission/battle maintenance decision and supplies its
host connection. It does not prove audible output, interruption behavior,
device timing or complete event coverage. The separate menu maintenance at
`0x36eae..0x36f22` requests slot 0 only after its own mode, current-menu and
parent-menu exclusions. That behavior is not replaced with a generic native
menu loop in this change. Original menu ancestry, all remaining sound triggers
and actual iPhone XR audibility remain separate work.
