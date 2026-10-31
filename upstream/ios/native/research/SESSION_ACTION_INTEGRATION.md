# Native actions, terrain ownership and continuous session integration

The session now admits human production and tape activation through
`UnitInteractions`, while preserving the distinct original human and AI
wrappers. The original type-list callback does not perform the funds check in
the alternative button path. The host therefore chooses `direct_type_list`
explicitly; the API does not infer a confirmation path. Production computes and
validates its complete result before the stable roster, funds, activation count
and action flags are committed. Human production marks both the producer and
the new unit acted. AI production marks only the new unit.

Ordinary movement retains its original command admission: an unoccupied
reachable destination must exist before Move is available. A real route may
then end on an eligible friendly carrier. Admission uses current-form fields8
and9 and a centered occupant; enemy/incompatible destinations are rejected.
After actual travel and arrival facing, carrier arrival deactivates the tape,
preserves its old health and movement state, updates the byte activation count
and upgrade slot, and removes its selection. Later activation uses the original
reset/debit operation and the same stable type entry. It does not create a new
cargo inventory or restore cargo health. Original-function provenance and
limits are in `UNIT_INTERACTIONS.md` and its fixture report.

The native integration test has86 checks. Its new cases execute actual human
production, reject a funds-checked insufficient-funds attempt without mutation,
reject invalid placement, complete a real carrier route, and reactivate the same
tape type. They verify action flags, funds, counts, health behavior and renderer
spawn generations. Existing cases exercise a complete enemy capture turn,
move/attack/presentation/return sequence and AI production. These are synthetic
missions with explicit headless presentation acknowledgements, not a campaign
playthrough or physical rendering evidence.

Transform-aware Undo now uses the unchanged-original predicate and mutation
covered in `UNDO_AND_COMMANDS.md`. It restores movement anchors when permitted,
reverses a recorded transform without recording another one, and obeys the
selection-retention result. Changing to another friendly selection locks prior
movement/transformation Undo exactly where the original does; merely clearing
selection does not apply that branch. Integration checks exercise move/Undo/
move, completed-transform reversal and the friendly-selection lock. The dormant
operation10 transition and command-mask0x100 marker are not treated as proof
of an original visible Wait control.

## Terrain and shared random state

`TerrainRuntime` owns the style1 pool on the simulation side. Cold bootstrap
creates it during opcode9 before subsequent opcode6 random unit placements.
Live map commands do the same. This order follows the original loading-phase
call sites recorded in `TERRAIN_RNG_ORDER.md`; no guessed draw-count skip is used.
`CampaignController` carries the current immutable terrain tables into the next
mission's bootstrap and checks the prepared shared RNG against the installed
session.

After gameplay/AI updates, `GameSession` advances the pool once per simulation
tick while the recovered phase225 gate permits it. Native battle and world
presentation waits pause simulation. The display renderer reads the resulting
phase and quads; it never creates a pool or draws random words. A separate
terrain generation/revision refreshes dynamic geometry without marking a
tactical world change or forcing autosaves at animation frequency. Checkpoints
retain pool state and gameplay RNG together; restoration derives only the
immutable layout and consumes no draws. See `SESSION_SAVE.md`.

This implements style1 interior, single-batch terrain. Style0/2 decorative
sprites, original outside padding and the complete original loading/cinematic
scheduler remain distinct gaps. Correct isolated random functions and cold
map construction do not prove the whole game's random history or rendering.
