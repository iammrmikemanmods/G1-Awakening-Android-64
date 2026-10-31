# Native mission completion and campaign progression

`MissionProgression.hpp/.cpp` recover the original completion, profile-update,
results-input and next-campaign decisions. These are portable C++17 functions.
No original executable or CPU interpreter is opened, linked or executed by the
native implementation. The original executable is only an explicit input to
the separate development reference generator.

## Original ordering and integration contract

1. Opcode 8 enters phase 8 and selects presentation requests from the actual
   campaign/world state. For the ordinary first-campaign success it requests
   audio operation/argument `(1,0)`, then `(5,21)`, with **no generic popup**.
   The next original clock tick changes phase 8 to 99; only the following tick
   dispatches the next command. It does not wait for a fabricated acknowledgement.
2. First-mission command 27 waits 1000 ms. Command 28 is opcode 10 with next
   campaign index 1. `MissionExecution::begin_completion` now permits the
   original phase-8 continuation; opcode 10 remains a typed integration boundary.
3. `prepare_campaign_profile_update` computes the original profile mutations.
   The caller must actually persist this updated profile. The original mutates
   in-memory records before attempting persistence; a failed write is not a
   successful mission handoff.
4. `campaign_save_disposition` accepts that **actual persistence result**.
   Failure requests the save-error path. Successful groups 0/1 enter results
   mode 7 and reset background offset to zero. Successful group 2 hands off
   immediately.
5. `advance_mission_results` consumes original input masks 16, 64 or 0x8000;
   matching any clears the entire input edge word, as the original input helper
   does. Without that input, only the results background scroll changes.
6. `campaign_handoff` computes either a next-script load, campaign menu return,
   or ending-text request. A next-script load requires actual world cleanup,
   loading-stage reset and construction from the selected archive data. A plan
   returned by this module is not evidence that those operations have run.

The cold first-mission controller verifies its command token and group/index
before `begin_completion`; stale or repeated calls fail. The returned audio
and text requests must be implemented by the application. The controller's
existing goal gate still requires a real capture-state mutation; ordinary UI
input does not satisfy that goal.

## Address evidence

All addresses refer to the original ARMv6 executable with SHA-256
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.

| Behavior | Original location |
| --- | --- |
| Opcode-8 owner/faction/controller selection, audio and text requests | 0x5648a–0x56600; special group-0/index-9 branch at 0x578d0 |
| Text constructor maps values <=392 and passes larger values through | 0x301de–0x3020e |
| Phase 8 becomes 99 without advancing the cursor | 0x57914 clock, phase-8 branch |
| Opcode-10 save-result dispatch | 0x56ee2–0x56f40 |
| Complete profile record/unlock update | 0x4e5a6–0x4e7b2 |
| Three signed best-record comparisons | 0x44058–0x440c6 |
| Active profile lookup | 0x2b5e6 |
| Persistence boundary | 0x4e7a4 calls 0x2fed8; that function calls 0x2fd74 for profile slot +1 |
| Results mode 7, UI preparation, background offset reset | 0x4d5c4–0x4d5ea |
| Results input/scroll update | 0x57e54–0x57ece |
| Loss/menu/ending/next-mission decision | 0x57c98–0x57e3a |
| Next-script selection | 0x48cfa–0x48d18; tables 0xcec18, 0xcebf8, 0xcebe4 |

The completion owner is 1 when owner 0 has no active units, or in the special
group-0/index-9 branch when owner 0 has exactly one unit and original type 12
is active. Otherwise it is 0. Group 2 chooses tagged text 0x21ff029a for faction
0 or 0x21ff0437 for a nonzero faction. Other groups use 0x21ff0332 when the
selected faction is 1. These tagged cases set configuration byte +0x44. The
normal first-campaign branch retains script index 202, whose mapping is the
MISSION COMPLETE resource, and explicitly bypasses the generic text call.

The original mapper returns 0xffffffff for already-tagged inputs; this is why
the second mapper comparison does not suppress the substituted tagged text.
The later text constructor is the component that accepts those tags directly.

## Profile details

The record columns are original profile +0x10, +0x18 and +0x20, indexed first
by campaign group and then mission index. They receive map +0x274, +0x278 and
+0x27c. The first is incremented by the human-turn path; the other two retain
numbered names in this module. Their update directions are respectively
minimum, maximum and minimum, each treating exactly -1 as unset. These record
updates occur even on a loss, before unlock eligibility is checked.

Profile +4 and +8 hold the two unlock values. A nonnegative next index raises
the selected field only if larger. An eligible negative next index finishes
group 0/1, sets its completed byte and stores the original table length 17/8,
also setting map +0x3c8. Group 2 skips best records but still follows the
original second-unlock-field branch; it does not set a campaign-finished flag.
This seemingly unusual group-2 behavior is preserved and covered by the
original-function fixtures.

Group 0 has resources `[315,326,333,334,335,336,337,338,339,316,317,318,319,320,321,322,323]`.
Group 1 has `[324,325,327,328,329,330,331,332]`.
Group 2 has `[310,311,312,313,314]`.
Thus first-campaign index 1 selects resource **326**, not adjacent resource 316.

Invalid groups, out-of-table indices, malformed profile table shapes, and
nonpositive/overflowing background-image periods fail explicitly. The original
unchecked pointer behavior for invalid tables is not reproduced.

## Verification and limits

`mission_progression_original_vectors.txt` contains 738 rows: 144 completion
cases, 144 profile cases, 288 handoff cases, six save-dispatch cases, 126
results-update cases and 30 table entries. They cover both persistence outcomes,
ordinary and special loss branches, both factions/controllers, signed metric
extremes, completed flags, negative campaign-end sentinels, results input
consumption and Q16.16 wrap. All untouched profile records are checked.

The strict C++17 test passes **10,843 comparisons/guards** against those rows.
The original clock additionally executes phase 8 for delta 0, 1 and 10000,
each producing phase 99 with the same cursor and unchanged wait storage.
The updated mission-controller test passes **724 checks**, including the
phase-8 entry, next-tick transition, subsequent 1000-ms wait and explicit
profile-save boundary. Existing dialogue, capture and clock fixtures remain
unchanged.

Reference execution is deliberately bounded. Profile update, nested record
comparison and results updater execute complete original functions. Completion,
text conversion, opcode-10 dispatch and handoff use the unchanged original
function bodies with seeded stack frames; their C++ unwind setup/epilogue is
outside the reference boundary. Audio, text rendering, menu presentation,
world cleanup, script installation and persistence are recorded external calls.
The profile-save return value is supplied independently as success or failure.
These tests establish native decision equivalence, not durable persistence,
rendering equivalence, a campaign playthrough or iPhone performance.

General mission/world commands and the remaining objective kinds are the next
separate recovery slice. This module does not silently skip them or claim
that all 30 missions are executable merely because all campaign table entries
are now known.
