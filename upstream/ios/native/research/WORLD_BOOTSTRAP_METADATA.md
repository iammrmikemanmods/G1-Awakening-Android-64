# Ordered native bootstrap effects

This extends `WORLD_BOOTSTRAP.md`; its earlier first-prefix resource/state
comparisons remain valid. The current native prefix still stops before the
first dialogue. General live world commands are a separate implementation.

`WorldBootstrap.flag_227` records the original combat phase flag. Opcodes13
and14 set/clear it at0x56270–0x56292. `WorldUnit.spawn` describes initial
activation properties, not a live HP mirror: ordinary placement has health100,
flag18=false and damage_immune=false, from0x4333a–0x43388. Special placement
branches are still explicitly unsupported by this prefix version.

## Ordering contract

Consume `initialization_effects` in vector order. A begin-current-player-turn
entry contains the site snapshot at that point. Build an income network from
that snapshot and call the separately recovered turn initializer. Do not use
later site placements or ownership to calculate an earlier turn's income.
Nested item owners in these cold snapshots are all−1.

The original map loader0x507fc clears sites through0x4b3fc before the opcode9
branch calls0x4e80c at0x56e38. The turn entry therefore precedes the opcode6
placement debits. Each debit uses `unit_spawn_cost`: signed scalar1, except
type21 is overwritten with40 by0x5a8ce–0x5a8de. The original subtraction at
0x54094–0x540c8 wraps at32bits, then clamps a negative result to0; the native
`apply_spawn_funds_debit` preserves those exact operations.

For first campaign315 the order is:

1. Command2/map load: begin owner0's turn with no sites. Original income
   recomputation has baseline1; its human turn counter advances to1.
2. Command3/type11: debit owner0 by17, leaving its funds0.
3. Command4/type19: debit owner1 by8, leaving its funds0.
4. Command5 creates the initially unowned site/groups. Those do not change
   the preceding income calculation.

These values are a composition of the recovered initializer and original
spawn debit. This report does not claim a newly executed whole-game startup.

## Validation

The development-only `bootstrap_metadata_oracle.py` runs unchanged original
fragments0x5a8ce..0x5a8e0 and0x54094..0x540ca, including their actual vector
accessor callees. No external call is replaced. It compares23types×4rawcosts×
6funds values: **552 cases** covering signed128/255 and32-bit wrap boundaries.
The native world test passes **1,203 comparisons/guards**, including this
fixture and ordered synthetic bootstrap inputs. The test embeds no original
archive data. The original executable identity is
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.

The fixture is `tests/world_funds_original_vectors.txt`. Run the existing
`g1_world_tests` executable with that fixture path to include the reference
comparisons; without it, the99source-only contract checks still run.

This extension does not itself apply the requests toGameSession, persist
funds, runAI, or establish campaign/device verification.
