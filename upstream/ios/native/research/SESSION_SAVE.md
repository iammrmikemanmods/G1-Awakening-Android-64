# Native mission checkpoint and cold resume

`SessionSave` persists real native gameplay state at completed action boundaries
inside an active opcode-12 mission objective. It is separate from the campaign
profile: the profile stores unlocks and best results; the session checkpoint
stores the mission currently being played. Neither format is an original ARM
object dump or a claim of compatibility with the old app's saves.

## State and restoration

The checkpoint retains campaign group/index, the exact objective cursor and
controller backing flags; stable unit identities, active/inactive state,
positions, offsets, facing, anchors, all recovered movement/action flags,
health, upgrade level, combat stats, health countdowns and immunity; site/item
ownership and link bytes; funds, income counters, player/AI cursor, upgrade
slots, activation counts, death records and mission metrics. All624 random
words, the random cursor and draw counter are retained together. Repeated
activation counts are saved explicitly and are not rebuilt from unique live
units. Original map metadata, availability, site points, markers and native
selection also survive.

Format3 retains the complete style1 terrain pool: its shared250ms clock,
global phase, candidate bytes, slot phases and chosen candidate offsets. Its
immutable layout is rebuilt from the matching archive and the supplied480-byte
terrain tables. The table checksum is stored; the tables themselves are not
duplicated into the save. Decoding an enabled terrain runtime requires those
matching tables. Pool creation is never called by restoration.

Styles0/2 additionally retain every decorative sprite's animation index,
selected step, elapsed time, repeat counter, direction and sticky completion;
visibility, parent group, attachment order and optional original position;
the reservation candidate bytes, slot phases and eight-phase clock. The six
immutable animation descriptors and layout are decoded from the same original
archive during restoration, without creating sprites or consuming random words.
The session owns both initialization stages before subsequent unit spawns and
all later sprite ticks. The renderer only reads the resulting state. Format2
did not store this state and is explicitly rejected; no released playable app
used that intermediate checkpoint version.

Immutable grid, terrain lookup/rules, unit definitions and mission commands
are reloaded from the supplied original archive. Archive byte length and a
cached Adler32 pair the checkpoint with its data. The separate payload checksum
detects accidental corruption. These checksums are not cryptographic
authentication. The format has an explicit version, declared byte length and
2MiB limit; counted structures and state relationships are validated before
session installation. A malformed, truncated, wrong-data or unsupported-version
save fails explicitly. The host preserves that file rather than silently
starting a new game over it.

`GameSession::restore_checkpoint` constructs a fresh native session and installs
the saved mutable values without replaying map resets, spawn debits, income,
capture, objective satisfaction or random draws. It re-decodes the same mission
objective, retains its controller values, and requests actual world presentation
and initial camera focus. The host must load/render the saved map, roster,
ownership and markers, present the matching frame, and acknowledge those
requests. All player commands remain blocked until required presentation is
complete. Old view/dialogue/battle/token caches are cleared when installing the
new session.

## Checkpoint boundaries and filesystem behavior

`checkpoint_available()` rejects in-flight movement, battle presentation,
pending AI actions, camera focus, world loading and active transformation
animation. It accepts a settled human or settled AI boundary at a real gameplay
objective. It does not permit an arbitrary mission cursor supplied by a save to
bypass camera or script gates. If the app backgrounds during a busy action,
the previous durable checkpoint remains the resume point. Mid-animation exact
visual resumption and script/dialogue/cinematic snapshots are not implemented
by this format.

The host encodes a stable snapshot and uses `write_native_save_atomic` to write
it. That writer creates a sibling temporary file, handles partial writes and
EINTR, flushes the file, renames it atomically and flushes its parent directory.
An actual failure is returned, including a directory flush failure after rename.
The host only updates its successful-save cache after success. It compares
encoded state to the previous durable bytes to avoid redundant writes. A
profile save failure never retires the current mission checkpoint. Successful
mission handoff requires the host to retire the old Continue target or replace
it with the new mission's checkpoint.

The original health preset at unit+34 starts at0: constructor4306c writes it at
430c2; outer43f42 separately changes timer+30 to-1 at43f70..43f74. The native
fresh `CombatUnitState` keeps those two defaults distinct. Existing inactive
states retain their preset during later activation.

Original59a7e calls4351c for every active unit, including stationary units.
`GameSession` now decrements stationary timers while `MovementAction` owns the
travelling actor's countdown, preventing a double tick. Inactive timers remain
unchanged. Both paths preserve disabled-1 and clamp the countdown at0.

## Verification and limits

The synthetic filesystem/session test executes actual capture and upgrade
commands, writes and reloads the file, destroys the source session, restores a
new one, and runs the native opponent. Subsequent stochastic battle results and
the complete serialized gameplay state match uninterrupted continuation. It
also verifies that unfinished movement cannot replace the prior checkpoint,
completed movement can be saved, real write errors do not claim success, and
corrupt/invalid inputs reject. This test contains no proprietary data or ARM
execution and uses explicitly synthetic presentation acknowledgements.
The current74-check test also saves running terrain pools for all three styles
to disk, destroys the source session, restores without recreation, then runs
the next enemy turn. Both subsequent damage and the complete pool/RNG/gameplay
bytes match, including style0/2 sprite playback and attachment state.
The malformed-state checks include missing/mismatched terrain tables and an
invalid saved slot phase, missing sprite state, an out-of-sequence sprite step
and an invalid parent group. Stationary and moving countdowns are verified to
tick once and clamp to zero.

`session_campaign_probe` additionally loads all30 actual mission resources.
Each of the five Arena scripts is exercised with both faction orders and both
human/AI opponent settings, for45 entries and540 native world operations.
It reaches the first real objective in each and performs a checkpoint/decode/
cold-restore comparison. All45 serialized states match byte-for-byte after
explicit headless presentation acknowledgements. This verifies entry and restoration,
not victory, complete mission playthrough or physical rendering. Six campaign
entries legitimately begin with an empty roster before live script spawns.

With terrain runtime enabled, all45 first-objective checkpoints also match on
cold restore;17 contain style0/2 sprite pools and the other28 contain style1
pools. Checkpoints range from3255 to17477 bytes. Source reports distinguish
these entry checks from mission completion. Human production and carrier/tape
transitions are now connected through actual session actions, independently
covered in the session integration test.

Full cinematic presentation, physical XR save/resume and sustained60Hz remain
separate release work. Native
presentation waits pause the simulation; these tests do not establish that every
original loading/cinematic scheduler interleaving has been reconstructed.

```sh
cmake --build build/native --target g1_session_save_tests session_campaign_probe
ctest --test-dir build/native -R '^native_session_checkpoint_and_resume$' --output-on-failure
build/native/session_campaign_probe /path/to/original/iphone.big /path/to/terrain-tables.bin
```

The hosted Apple workflow also compiles and runs the native session, campaign
handoff and atomic-profile filesystem tests on macOS. A future recorded run is
required to establish Apple validation of this new slice; earlier interaction
build5795315 does not cover these additions. No physical iPhone execution,
complete game or full-playthrough claim is made by these tests.
