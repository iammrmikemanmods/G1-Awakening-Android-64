# Continuous native enemy action execution

`AIExecution` translates original57ed0's plan-consumer decisions. `GameSessionAI`
connects the recovered planner, command delay, original movement, transformation,
attack, capture, repair, upgrade, production and turn routines to actual mutable
native game state. The product executes C++ directly; original Thumb functions
are used only by development comparison tools.

The unchanged-original consumer corpus contains136 cases covering all five
plan action codes, same-tile versus travelling plans, both forms, pre-arrival
and post-arrival transformation, and ordered site/item layouts. Native tests
perform681 comparisons. Exact boundaries are recorded: renderer56044, command
phase setup5494c and upgrade556a8 are observed external requests in this corpus,
not mocked gameplay outcomes. Upgrade, movement, combat and capture mutations
have their separate original-function corpora. The complete58dcc driver is not
claimed as a whole-function original execution comparison.

Action0 starts original phase2 even for an empty route; that route still
completes on one simulation update. A nontrivial destination also selects
phase2 before the requested arrival action. Same-tile attack may request the
second transformation and sets the original attack-animation gate; same-tile
capture scans all signed stored coordinate pairs in order and its second
transformation does not set that gate. Later matching capture phase requests
replace earlier ones. Same-tile repair selects phase4; upgrade is immediate.

The session retains a real pending AI action, elapsed signed32 delay, actual
actor/target, original form changes and cursor. Admission prioritizes original
production before ordinary owner slots; the cursor is committed after planning
returns. The recovered100ms threshold is applied independently of display
refresh. Matching native focus frames coordinate camera requests. Attack waits
for the original global transform gate, which is cleared only by a completed
special-to-normal sprite sequence acknowledged from a presented frame. A stale
or repeated generation cannot complete another unit's current request.

Movement uses the same board, profile, budget field, route and fixed-distance
updates as human movement. Arrival synchronizes logical state before subsequent
attack/capture/repair/upgrade. An original occupied destination is not silently
treated as an empty move; transport has its own recovery/integration scope.
Capture uses the AI plan's stored site/group/item, rather than re-running the
different human coordinate selector. Battles use the shared original random
generator and actual target/HP/death transitions, then wait for real battle
presentation. Production uses the exact north/west/east/south destination
selection, activation, funds and counts; the AI marks the newly created unit
acted, while the producer remains unmarked. Original human production has a
different action transition and must not reuse that AI flag behavior blindly.

The original turn transition resets outgoing actions, computes incoming income,
selects the next controller and requests focus/snapshot work as appropriate.
No UI acknowledgement fabricates a captured site, eliminated unit or satisfied
mission objective. If a goal becomes true, the actual mission observer takes
its recovered branch and can interrupt the pending action flow.

Session integration fixtures exercise complete enemy capture and turn return,
production with real fund/count/new-unit flags, and move/attack/battle completion
followed by a return to the human player. They use synthetic host presentation
acknowledgements; they are not physical camera, Metal, audio or full campaign
tests. The checkpoint test adds deterministic cold continuation through an
enemy turn and stochastic battle.

```sh
python3 analysis_tools/ai/execution_oracle.py \
  --binary /path/to/G1awakening \
  --libgcc /path/to/libgcc_s.1.dylib \
  --unicorn-dir /path/to/python-packages \
  --output research/native_ai/original_execution.json
build/native/g1_ai_execution_tests native/tests/ai_execution_original_vectors.txt
ctest --test-dir build/native -R '^native_session_integration$' --output-on-failure
```

The oracle authenticates the pinned original executable and libgcc, keeps their
text read-only and verifies it unchanged after execution. Its synthetic fixture
can be published; original executables, source-data fixtures and decoded assets
remain local. `AI_EXECUTION.json` records the fixture/oracle hashes and explicit
comparison boundary.
