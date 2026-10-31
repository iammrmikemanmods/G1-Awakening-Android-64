# Native campaign save, results and handoff controller

`CampaignController.hpp/.cpp` connect the already recovered opcode-10 decisions
to the actual native `GameSession`, campaign profile writer and generic next
mission bootstrap. The controller contains portable C++17 code. It does not
open or execute the original ARM executable, and its tests contain constructed
resource data rather than copied original game assets.

## What the controller completes

The controller accepts one actual session stopped at opcode 10, snapshots its
campaign counts, mission record and shared random generator, computes the
original profile update, and calls `write_native_save_atomic` with the encoded
native profile. A missing parent directory or any other write failure leaves
the request in `save_failed`. Its exact prepared bytes remain available for an
explicit token-bound retry. Neither results nor a next-mission request is
available while that save is unconfirmed.

After a successful save, groups 0 and 1 expose the current, previous and best
records as an undrawn results request. The host must draw that request and call
`report_results_presented(token, actual_height)` before result input is accepted.
The original input masks 16, 64 and 0x8000 then select the recovered handoff;
elapsed time alone only changes the results background offset. Group 2 retains
the original immediate post-save handoff and does not acquire an invented
results screen.

For a next mission, `prepare_next_campaign` loads the actual selected resource
using `bootstrap_campaign` and the captured shared random stream. It never
reseeds the generator. The first successful preparation is cached; a repeated
call does not consume additional random words, while a failed archive load
leaves the same request available for retry. The host constructs the real
`GameSession(archive, prepared.bootstrap, prepared.context)`, installs it, then
reports that installation using the current request token.

Installation confirmation checks the campaign, resource, command boundary,
roster, activation counters and all generator state words against the prepared
result. It does not acknowledge the new session's world, camera or other
presentation requests. Those remain the host's independent work after
installation. Menu and ending acknowledgements likewise only close the
requested destination; they do not select another campaign or fabricate an
ending's subsequent input flow.

## Host contract

| Current gate | Permitted next operation | Required evidence |
| --- | --- | --- |
| `idle` | `begin(session)` | Stopped opcode 10, interaction mode 6, no moving unit, battle, or pending world presentation |
| `save_failed` | `retry_save(token)` | Same current token; a new actual file write is attempted |
| `results`, undrawn | `report_results_presented(token, height)` | The current result request has actually been drawn with a positive supported background extent |
| `results`, drawn | `update_results(token, edges, delta_ms)` | Actual current input/time; only the original matching input masks request handoff |
| `next_mission` | `prepare_next_campaign(token, archive)` | Successful prior persistence and required result input, except original group 2 |
| `next_mission`, prepared | `report_next_session_installed(token, session)` | The host installed the session built from that prepared world and random stream |
| `campaign_menu` | `report_menu_presented(token)` | The requested menu was shown |
| `campaign_ending` | `report_ending_presented(token)` | The requested ending was shown |
| `handed_off` | No further operation in this controller | Construct a new controller for a later mission cycle |

Request tokens are process-unique and never wrap. Each controller is a single,
noncopyable and nonmovable transaction owner; the application can hold it in a
`std::unique_ptr` or construct it in place. Responses with another token,
responses for another gate and repeated completed responses fail explicitly.
The application's existing session revision guards are still needed to ensure
that a report describes the actual visible host surface.

The constructor loads the current profile from the selected app-private path.
A missing file starts a default profile. An unreadable, malformed or nonregular
existing file rejects; it is not silently reset. The host creates its parent
directory. This is the versioned native campaign-record format documented in
`NATIVE_PROFILE.md`, not the original game's opaque save format or a complete
mutable mid-mission snapshot.

`profile()` returns the last confirmed profile. On a failed write it retains
the previous confirmed state, even when the underlying writer reports a
post-rename directory-flush error. An explicit retry writes the same prepared
transaction again. This controller is intended for the application's single
active campaign transaction; concurrent independent profile writers are not
coordinated by it.

## Original evidence and division of responsibility

The original decision functions and their reference cases are documented in
`MISSION_PROGRESSION.md` and `MISSION_PROGRESSION.json`. Their executable input
has SHA-256
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.

| Recovered decision | Original address |
| --- | --- |
| Opcode-10 save outcome and results dispatch | 0x56ee2–0x56f40 |
| Record and unlock update | 0x4e5a6–0x4e7b2 |
| Three signed best-record comparisons | 0x44058–0x440c6 |
| Original save call boundary | 0x4e7a4 calls 0x2fed8, then 0x2fd74 |
| Results mode and zeroed background offset | 0x4d5c4–0x4d5ea |
| Results input and background scroll | 0x57e54–0x57ece |
| Loss, menu, ending and next-mission selection | 0x57c98–0x57e3a |
| Campaign script tables | 0xcec18, 0xcebf8, 0xcebe4 |

The existing progression reference comprises 738 original-function cases and
10,843 native comparisons/guards. The new controller does not claim that a
mocked original save call proves durable native persistence: it invokes the
real native writer, whose actual filesystem behavior is exercised separately
below. Native token ownership, checked host drawing and installation checks
are explicit integration contracts rather than newly attributed original
instructions.

## Focused validation

`tests/campaign_controller_test.cpp` passes 53 runtime checks and a compile-time
single-owner assertion, compiled as C++17 with `-Wall -Wextra -Wconversion
-Werror -pedantic -O2`. It links the current native core including
`GameSessionWorld.cpp`; it does not use an emulated session or replace the file
writer with a supplied success boolean.

The checks cover:

- A real missing-parent failure, no premature results/unlock, and successful
  retry after creating the parent directory.
- An actual rename failure caused by a directory at the destination, followed
  by a successful retry after removing that obstruction.
- Closed-file readback of the persisted profile, reload by a new controller,
  corrupt-profile rejection and temporary-file cleanup.
- Undrawn-result rejection, stale and repeated tokens, original accepted and
  rejected input masks, background scroll, and no time-only handoff.
- Real session loss, campaign ending and group-2 immediate-handoff branches.
- Rejection of a dialogue gate or a still-pending world presentation as
  campaign completion.
- An unavailable next-script failure and a failure after random spawning that
  preserve the pending request and captured generator,
  exact continued random-generator state, cached preparation, and rejection
  of an installed session initialized with another random seed.
- Successful installation of the prepared next session while its actual world
  presentation and camera gates remain pending.

The synthetic next resource intentionally contains a random unit spawn, so
the random-stream test verifies state changes across bootstrap, not merely
equal unchanged seeds. No original text or asset payload is needed for these
tests. Exact source hashes, compiler command and validation output are recorded
in `CAMPAIGN_CONTROLLER.json` and `campaign_controller_validation/`.

## Remaining boundaries

This verifies native save/results/handoff orchestration. The host still must
render and connect the results, ending and campaign-menu surfaces, instantiate
and install the next session, and handle real application interruption. A
complete original campaign playthrough, mid-mission restore, Apple build of
these new files, XR/iOS 17.5 execution and sustained 60 FPS remain separate
implementation or validation gates. The tests do not claim any of them.
