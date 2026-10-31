# Native battle model timeline

`M3GBattleTimeline` implements the two recovered model-time helpers in
ordinary C++17. It has no original executable, CPU interpreter, JIT, engine
wrapper, or Apple dependency. It computes the animation time to request,
the subsequent stored clock, and the original helper's end result for one
update. The full cinematic phase controller remains a separate task.

## Source behavior preserved

The helpers are original `0x3c23a` (start) and `0x3c950` (update), using
signed halfword tables at `0xceaa8` and `0xcea90`. They multiply frame numbers
by binary32 bits `0x42055555`, then truncate to integer only where the
original does. The recovered ranges are:

| Clip | Start frame | End frame | Start time ms | Float32 end threshold ms | End sample ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 20 | 0 | 666.6666259765625 | 666 |
| 1 | 50 | 90 | 1666 | 3000 | 3000 |
| 2 | 100 | 160 | 3333 | 5333.3330078125 | 5333 |
| 3 | 200 | 250 | 6666 | 8333.3330078125 | 8333 |
| 4 | 320 | 350 | 10666 | 11666.666015625 | 11666 |
| 5 | 260 | 300 | 8666 | 10000 | 10000 |

Below the floating end threshold, the requested animation time is the old
clock; only afterward does the original store `clock + delta`. At or beyond
the end with restart disabled, it requests the truncated end time and leaves
the stored clock unchanged. With restart enabled, it requests/stores the
start time and discards overshoot. The native result has separate fields so
the caller cannot accidentally substitute the new clock for the requested
sample time.

The register ADD is represented with unsigned 32-bit addition followed by a
bit-preserving signed interpretation. This avoids native signed-overflow
undefined behavior. Invalid clip indices fail with `FormatError`; no guessed
range or fallback clip is substituted.

`reached_end` is the helper's return value. The inspected cinematic update
calls ignore that return and progress through a separate phase timer and
combat flags. It must never be used by itself as a GameSession battle
presentation acknowledgement. See `M3G_BATTLE_BINDING.md` for the exact
phase/model/clock/restart mapping, including the original phase-12 model-A /
clock-B pair and phase-4 restart behavior.

The original public model slot is resolved in `M3G_PUBLIC_ANIMATE.md`. A
native renderer can apply `M3GAnimations::sample(step.animation_time_ms)` to
its rigid pose and retain `step.stored_clock_ms` for the next simulation
update. That remains a model-timing integration, not a substitute for the
recovered camera, model visibility/placement, battle effects, or actual
presentation completion.

## Validation

The source compiled with `g++ -std=c++17 -O2 -Wall -Wextra -Wconversion
-Werror`. The focused test passed:

- 54 unchanged-original reference cases: six starts and 48 updates.
- 186 exact field/table comparisons, including binary32 end bits, stored
  clock, requested animation time, and original update return.
- 58 native contract checks, including restart classification, invalid
  indices, and the explicit 32-bit arithmetic boundary.

The original cases use the private audited ARMv6 executable and original
libgcc, with instruction regions verified unchanged. Only retain/release,
the public time-call endpoint, and nonthrowing unwind registration are
recording boundaries. The fixture includes both fractional and integer end
equality, a positive in-range step, crossing, and large-step restart/clamp.
Overflow and negative delta are native checks derived from the ADD
instruction; they are not additional original-execution cases.

Reference SHA256:
`15baf7f0794896bd5815f46b7721c3099a94fea28201eb8b3eba74ad3f2b8c4a`.
The test's generated C++ fixture contains only synthetic input/output
records. It is excluded from the production library. The comparison record
is `m3g_validation/battle_timeline_native.json`.

No Apple compilation, GPU output, device performance, complete cinematic,
or full campaign result is claimed by this test.

## Reproduction and integration

From the repository root:

~~~bash
python3 analysis_tools/m3g/timeline_fixture.py \
  --fixture native/research/m3g_validation/original_battle_timeline.json \
  --output native/tests/m3g_battle_timeline_fixture.hpp --check
g++ -std=c++17 -O2 -Wall -Wextra -Wconversion -Werror \
  -I native/include native/src/M3GBattleTimeline.cpp \
  native/tests/m3g_battle_timeline_test.cpp \
  -o /tmp/g1_m3g_battle_timeline_test
/tmp/g1_m3g_battle_timeline_test
~~~

The development-only original reference runner is
`analysis_tools/m3g/battle_timeline_oracle.py`; its private-input and loader
requirements are in `analysis_tools/m3g/BATTLE_TIMELINE_REFERENCE.md`.

The core source addition is `src/M3GBattleTimeline.cpp`. A standalone test
target can use `tests/m3g_battle_timeline_test.cpp`, named
`m3g_battle_timeline_contract`, with no arguments. The test header is
`tests/m3g_battle_timeline_fixture.hpp`. No CMake or frozen Apple source was
edited by this slice.
