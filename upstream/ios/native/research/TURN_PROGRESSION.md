# Native turn progression and income

`TurnProgression` reconstructs original `0x55840(true)`, its outgoing-unit
`0x430fc` resets, next-owner selection, and `0x4e8e8 -> 0x4e80c` income update.
The outgoing owner's units reset even when no active incoming unit exists.
The incoming focus uses the first active unit in ascending original type order.
A valid incoming owner receives its refreshed capture-network income. Controller
value exactly 1 starts the AI cursor at zero; other controller values increment
the original human-turn metric. Signed arithmetic wraps like the ARM source.

No active incoming unit means no owner switch, income or counter increment.
The resulting human controller still requests the original snapshot. Camera,
formatted income text and snapshot requests remain separate from their actual
native presentation/persistence.

`begin_current_player_turn` also exposes original `0x4e80c` without switching
owners. Opcode9 calls it before subsequent placement debits. The first mission
therefore starts human metric274 at1, while its baseline1 income is consumed by
the signed17 Bumblebee spawn debit; owner0 funds are0 at the first dialogue.
GameSession applies the ordered bootstrap requests with their original site
snapshots. It does not credit income again after all placements.

## Evidence and boundary

192 unchanged-original executions match 43,084 scalar comparisons, including
unit flags/anchors, shuffled unit order, missing incoming units, ownership/link
states and signed overflow. Code pages stayed unchanged. Four explicit
external hooks record camera focus, two UI string operations, and the snapshot
request. They do not simulate a completed save or camera draw. The raw metadata
is in `turn_validation/original_turns.json`; the native reference fixture is
`tests/turn_original_vectors.txt`.

The reproducer is `analysis_tools/turns/oracle.py`; run its `--help` for the
original image, movement oracle, capture reference, native probe and output
arguments. `tests/turn_test.cpp` runs as a stdin probe without arguments and as
an original-fixture test with its fixture path. The source contains no guest
execution. Whole AI turns, saved sessions and iPhone behavior need integration
and device evidence beyond this component.
