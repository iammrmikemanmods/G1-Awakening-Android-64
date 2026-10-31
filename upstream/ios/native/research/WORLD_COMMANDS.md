# Native world commands and general campaign bootstrap

## Implemented boundary

`WorldCommands` prepares typed native operations for every world-changing
opcode observed in the 30 original mission streams. It does not run an old
executable. `CampaignBootstrap` applies the cold setup prefix to decoded native
state and stops **before** the first camera, dialogue, delay, transition,
objective, completion, or handoff request. `MissionExecution` now has a separate
world-command gate: elapsed time and dialogue input cannot acknowledge a
pending operation.

The live `GameSession` remains responsible for applying plans to its actual
movement, combat, capture, funds, selection, RNG, renderer, and audio state.
The existence of a plan is not evidence that those effects occurred. Camera
completion, gameplay goals, profile persistence, and campaign handoff retain
their distinct real-result requirements.

## Original evidence

The source is G1 Awakening iOS 1.1.3 ARMv6, SHA256
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.
Virtual addresses are file offsets plus `0x1000`.

| Opcode | Recovered operation | Original evidence |
|---|---|---|
| 0 | Set flag225, request loading stage at least1, phase0 | `0x56224..0x5624c` |
| 16 | Resolve actual title text index, request stage at least2, phase16 | `0x561a8..0x56220`, actual switch `0x445c8` |
| 6 | Explicit/random type, optional faction inversion, exact activation and count/debit request | `0x56606..0x5687c`, `0x4e53a`, `0x496e4`, `0x54016` |
| 7 | Replace sites and arbitrary counted groups of coordinate pairs | `0x56880..0x56c1a` |
| 9 | Load grid/style/lookup, clear map state, initialize owner0 turn, request style music | `0x56c20..0x56ed8`, `0x507fc`, `0x4e80c`, `0x4e7b8` |
| 13/14 | Set/clear original combat flag227, retaining its phase | `0x5626c..0x56294` |
| 15 | Replace availability; nonempty group0/resource333 also enables type4 | `0x57640..0x576e0` |
| 17 | Replace points `[x,y,site,1]` and set referenced site owners to1 | `0x576e8..0x5780c` |
| 18 | Set marker254 to signed input pair; renderer mode2 | `0x56320..0x56380` |
| 19 | Set marker25c to literal `(1,1)`, ignoring parsed pair; geometry request and renderer mode3 | `0x56386..0x563c0` |
| 20 | Set target type's damage-immunity byte from input !=0 | `0x563d2..0x5642c` |
| 21 | Validated original no-effect operation | Parser `0x48578`; dispatch falls through `0x576e2..0x578e4` |

Opcode21 must not be interpreted as blocking rectangles. The original parser
retains its count-plus-four-words-per-record payload, but the examined dispatch
body does not consume it. Reference execution checks the entire seeded map and
app regions remain unchanged. Unknown opcodes still reject or stop explicitly.

Original phase updater `0x57914` changes phases0/16/13/14/18/19/20 to99 on the
next update. The following update advances the command cursor. Opcodes6/7 set99
at entry; 9/15/17/21 preserve the entering phase, normally99. Native gates
preserve that difference and never carry excess elapsed time through several
commands.

## Roster, counts, and random state

The native roster contains unique original types, with stable indices in first
activation order. A repeated placement updates its existing entry. Every
original activation still increments the owner's eight-bit active count,
including an already-active type. Actual resource323 initializes three unique
objects with four activation events, yielding original counts `[3,1]`. The
count must not be recomputed from the number of roster objects.

Type identities, including inactive entries, survive a later map replacement.
The live map operation deactivates them, resets owner counts and slot cursors,
clears sites/points/cost regions/markers, and starts owner0's real turn against
the empty network. Funds and faction identities are preserved. A map's turn
credit occurs before subsequent unit placement debits; the ordered bootstrap
metadata retains that exact boundary.

Random selection calls the existing native shared `GameRandom`. The original
candidate is `abs(signed32(word) % 23)`, followed by definition-faction and
inactive-unit rejection. The availability list is not a candidate filter.
Preparation copies the caller's generator and returns its resulting state;
the actual session commits that state together with the placement. There is no
clock seed, implicit reseed, replacement distribution, or fabricated draw.
An empty candidate set and an explicit one-million-draw resource bound fail
instead of reproducing an unbounded original retry.

`UnitSpawn` supplies the already verified activation result, including form0
reset, the original terrain-forced form1 branch, special HP9/immunity/timers,
signed cost, retained untouched fields, and count increment. Its independent
192-case reference remains documented in `UNIT_SPAWN.md`.

## Public consumption contract

`bootstrap_campaign(archive, context, random, config_damage_immunity)` returns:

- `world`: decoded state and unconsumed execution cursor.
- `unit_activations`: exact movement/combat activation states aligned with
  `world.units`, including repeated-spawn reset/preservation effects.
- `applied_commands`: setup plans in original order. Their gameplay metadata
  has already been applied to this returned world. The caller still implements
  real title, marker, renderer, and audio requests; it must not debit funds or
  activate units a second time.
- `random_after`: the generator state to commit after successful construction.

The input generator remains unchanged on success or failure. Initial funds,
turns, and activation counts come from `world.initialization_effects` in order.
The old `bootstrap_first_campaign` remains available with its existing bounded
dialogue boundary; the new bootstrap does not silently change that helper.

Six actual campaigns place the initial camera before the first unit: resources
316, 317, 318, 319, 338, and 339. Empty initial rosters are valid. All30 original
streams reach an unconsumed opcode4 camera with this bootstrap. Subsequent
unit commands must run after the actual camera gate.

For live continuation, retrieve `pending_world_command()`, prepare its typed
plan against current state, perform the operation, then call
`report_world_command_applied(command_index)`. A stale or repeated token throws.
The controller cannot independently establish that rendering or gameplay work
completed; the native session is the authoritative caller. General bootstrap
can also enter an objective directly. `take_initial_effects()` retains that
objective's real gameplay-entry request and drains it exactly once.

`WorldBootstrap::movement_board()` now forwards actual opcode17 points and
explicit cost-region state. Opcode7 site coordinates do not become movement
blockers. Site points retain their linked site index separately from the
movement point's state field.

## Validation

The development reference executes the unchanged dispatch body
`0x560c0..0x578e4` with a seeded frame, plus actual owner rebuilding, explicit
faction selection, MT19937, signed candidate selection, and style-music logic.
The external boundaries are bounded allocation/free/memset, vector/string
marshaling, renderer/audio requests, shared-generator lookup, and exact signed
modulo arithmetic. No gameplay decision or random word is supplied by a hook.
The complete site loader and map I/O are not executed by this new oracle; their
format/data evidence comes from the existing World/Terrain recovery.

Recorded results:

- **151 original world-command/RNG cases**, with **1,702** native comparisons
  and contract guards, including generic bootstrap and repeated activation.
- **794 MissionExecution checks**, retaining all existing476 dialogue,
  164 capture, and48 clock reference cases, plus new request/token/phase tests.
- **30 actual archive prefixes**,135 initial activation events, six empty
  initial rosters. The corpus probe does not advance goals or claim a playthrough.
- Strict C++17 builds with `-Wall -Wextra -Wconversion -Werror -pedantic`.

Machine-readable results and current source hashes are in `WORLD_COMMANDS.json`
and `world_commands_validation/`. The portable original-function reproducer is
`analysis_tools/world_commands/`; it requires the user's hash-checked original
executable and is never compiled, linked, or packaged into the native app.

Remaining boundaries include live GameSession/renderer integration, original
profile replay (native saves restore their own state), full campaign testing,
and actual iPhone XR/iOS17.5 execution. Terrain and presentation limitations
reported by their respective modules remain in force. This validation does
not establish a native-device frame rate or completed campaign.
