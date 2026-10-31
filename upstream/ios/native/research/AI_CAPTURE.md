# Native opponent capture planning

`AISite.cpp` reconstructs the complete original `0x508a2` decision, with
`AIIncome.cpp` for `0x4b56c` and `0x4b452`. The planner computes reachable
positions in the actor's current movement form, excludes other stationary
active occupants, preserves stored candidate ordering, and selects the
highest income change. A site wins an equal-scoring item. The selected capture
form uses the original nonzero test; this differs from the human command's
exactly-one capability check.

The old income baselines are explicit inputs. A hypothetical capture changes
only a copy of ownership, recomputes the exact recovered network income, and
returns the signed 32-bit change in own income minus the other player's change.
Neither ownership, links nor actual funds are changed by planning. Signed site
coordinate loads, stale income values, current-player/actor-owner distinctions,
and arithmetic wraparound are retained.

## Evidence

The unchanged original whole planner was executed in 256 synthetic worlds;
101 returned capture plans. Another 2,076 direct original income-helper calls
were compared. The native fixture test passed 5,073 comparisons and guards.
Original code pages were read-only and checked unchanged. The reference
checks that the original restores all unit, owner, counter and link state after
each call. The fixture includes occupied targets, unreachable cells, equal
scores, alternate forms, signed coordinate boundaries and overflow baselines.

The reference executes the actual game helpers. Its explicit external
boundaries are bounded allocation/free/memory operations and nonthrowing SjLj
registration; original ARMv6 arithmetic helpers execute their pinned original
code. Those development dependencies are not linked into the native product.
See `AI_CAPTURE.json` for identities, counts and the fixture hash.

## Reproduce

From the project root:

```sh
python3 analysis_tools/ai/site_oracle.py \
  --binary PATH_TO_ORIGINAL_EXECUTABLE --libgcc PATH_TO_ORIGINAL_LIBGCC \
  --unicorn-dir PATH_TO_UNICORN \
  --fixture native/tests/ai_site_cases.txt \
  --output research/original_ai_site.json
cmake --build build/native --target g1_ai_site_tests
build/native/g1_ai_site_tests native/tests/ai_site_cases.txt
```

The original-input report can be regenerated locally. No original executable,
resource payload, emulator or compatibility runtime is required by the app.
These comparisons establish the bounded decision code; they do not establish
that a complete opponent turn, mission or iPhone playthrough has run.
