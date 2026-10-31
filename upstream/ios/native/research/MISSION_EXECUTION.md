# Native mission dialogue and control flow

**Subsequent progression extension:** the original capture/dialogue validation below remains valid. `MissionExecution::begin_completion` now consumes the stopped opcode-8 entry using actual campaign state and preserves its phase-8 → 99 → next-command timing. The updated controller suite passes 724 checks; opcode 10 still requires actual persistence/results/handoff integration. See `MISSION_PROGRESSION.md` and its separate original-function fixtures. The earlier resource probe intentionally still reports its original cursor-26 boundary.


## Implemented boundary

`Text` and `MissionExecution` are portable C++17 modules. They read original
resource data and run recovered native control flow. They do not open or execute
the old ARM32 executable. Original-function reference execution is isolated in
`analysis_tools/mission_execution` and is never linked into the app.

The continuation begins at the first campaign bootstrap's unconsumed command
9, opcode 3. It implements dialogue gates, camera requests, delays, phase-1/2
transitions, and the ordinary captured-site-item objective branch. It can
continue through the later conversation after a real capture-state observation.
It **stops before command 26, opcode 8**, whose completion presentation and
audio/player-dependent effects have not been implemented. Opcode 10 save and
campaign handoff also remain explicit unsupported boundaries.

This is a **control-flow component**, not a playable mission or complete world
executor. It returns required external effects and requests. The caller must
perform text layout/drawing, camera movement, gameplay initialization, capture,
unit action resets, audio, and the other game systems. A returned effect is not
evidence that those operations happened. The local resource probe deliberately
supplies synthetic observations to test control flow and labels that result as
not a playthrough.

## Original identities and address evidence

| Input | SHA256 |
|---|---|
| Audited ARMv6 executable | `0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd` |
| Original `iphone.big` | `663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4` |
| Localization resource 1 | `8c189a68d0d31b53d08b88438fa0df06c88196bf446a43801ae332039c4f452a` |
| First mission resource 315 | `7ecc54a292717325b9e6e2c44c3c166e44be296b2e408c42b76cc49b6d10346a` |

All addresses below are virtual addresses in that executable. The logical
dispatcher spans several fragments that older decompiler function boundaries
split incorrectly; direct branch disassembly and bounded original execution
provide the reference here.

| Behavior | Original evidence |
|---|---|
| Mission dispatch | `0x5608c..0x578fc` |
| Previous dialogue cleanup when next opcode differs | `0x560c0..0x56124`, call `0x2bdc0` |
| Opcode 3 entry and skip/restored-game branches | `0x56124..0x561a4`, call `0x4dd12` |
| Dialogue text construction/layout | `0x4dd12..0x4e23c`; external font/wrapping remains a renderer responsibility |
| Dialogue gate update | `0x4e3bc..0x4e538` |
| Input edge consumption | `0x2b9c2..0x2b9dc` |
| Character budget and draw-produced reveal flag | `0x5b99e..0x5bb86` |
| Script index to localization tag | `0x445c8..0x457fe`, switch helper `0xb9cb0` |
| Unit name index adjustment | read `0x5a7f0`, add four `0x5a7f8`, store `0x5a7fe`; second index similarly at `0x5a81a` |
| Speaker name lookup | `0x4dfc2..0x4dfda`, map `+0x12c[speaker]` |
| Camera opcode and delegated completion | `0x562b4..0x56318`, helper `0x557f4` |
| Delay entry | `0x56450..0x56480` |
| Mission phase/timer update | `0x57914..0x57a48` |
| Kind-1 objective argument setup | `0x5708a..0x5710c` |
| Cold gameplay entry and skip reset | `0x57410..0x57458` |
| Capture predicate | `0x559b4`, kind-1 branch `0x55a90..0x55b6c` |
| Objective-success transition | `0x558e4..0x559b2` |
| Current-player action reset | `0x55840(false)` calling `0x430fc` per unit |
| Site/item initial state | `0x56994` and `0x56ae8`, both `-1` |
| Completion presentation boundary | `0x56484..0x56600` |
| Save/profile and campaign handoff boundary | `0x56ede..0x56f40`, including `0x4e5a6` |

## Localization and speaker identity

Resource 1 uses FGIB member key `0x69e4c505`; mission/unit resources use the
different key `0xf4e02223`. The text payload is 28,253 bytes with 511 entries.
Its strict decoded framing is:

1. LE `u16` flags `0x2000`, then LE `u16` entry count.
2. Each index entry is LE `u16 string_id`, LE `u16 absolute_offset`.
3. A LE `u32` sentinel after the table equals the payload length.
4. Each string span starts with the four bytes `04 00 00 00`, contains strict
   UTF-8 with no embedded NUL, and ends in one NUL.

IDs are sparse and source order is preserved. Padding between the table and
first string is allowed; the actual first string starts at offset 4096.
Duplicate IDs, overlapping/non-increasing spans, wrong wrappers/sentinel,
truncation, and invalid UTF-8 are rejected. No normalization, transliteration,
replacement characters, or invented fallback strings are introduced.

Mission text indices are **not** those sparse string IDs. The original mapper
has 486 supported indices and returns `0x21ffXXXX` tags. The native table stores
their low 16-bit IDs. Only indices 373..377 differ in the alternate-story
variant. Every index in both variants was independently executed and compared.
Out-of-range indices are rejected instead of forwarding the original `-1` tag.

Unit-definition raw name indices require the loader's **+4**, narrowed to 16
bits then sign-extended before the mapper. Ignoring that operation gives wrong
names. Type 11 raw index 124 becomes index 128, string ID `0x01a0` (Bumblebee).
Dialogue speaker type 1 raw index 122 becomes index 126, ID `0x02a7` (Ratchet).
The native `World` still preserves the unmodified raw resource words;
`unit_text_index()` performs the UI conversion explicitly. Speaker `-1` uses
the original tutorial heading ID `0x019d`.

## Dialogue contract

`advance_dialogue()` translates the original gate update. Its caller supplies
real line count, line height, the original two font metrics, box height, and the
draw result indicating whether visible text is fully revealed. These are not
deduced from string length or guessed device typography.

The reveal accumulator starts at `-65536000`. A tick adds the original fixed
point result of `delta_ms << 16` multiplied by 2048, clamped at zero when it
crosses zero. The draw-side budget is `1000 + arithmetic_shift_right(offset,16)`.
For ordinary small positive deltas this reveals one additional unit of the
original string budget every 32 ms. Native arithmetic explicitly preserves
32-bit wrap and signed shift behavior; long-frame values are not silently
reinterpreted as a different formula.

The original input helper is significant: a matching query clears the **whole
input-edge word**, not only its own bit. Queries occur in this order: mask 1
(back), 16 or 64 (advance), 128 (skip). Thus simultaneous bits cannot be treated
as independent button presses. Unknown unmatched bits remain available.

The gate first determines whether more lines remain outside the current box.
Back moves the first line back by one. Advance with more pages applies the
original integer page step. Otherwise an advance/back input reveals currently
incomplete visible text; only a later advance with fully revealed text and no
more pages marks phase 99. The original branch order is preserved even when a
page change occurs before the current text has finished revealing.

Skip requests audio notification `(5,18)`, marks phase 99, and sets the flag
that skips following dialogue commands. The cold objective-entry path clears
that flag. Actual audio dispatch is external. The module does not invent touch
gestures, tap-count rules, fonts, portrait layout, UI buttons, or sound mapping.

## Cursor, camera and timing contract

`tick()` performs one original phase update. Completing a dialogue or delay
marks phase 99; a **subsequent tick** advances the cursor and dispatches the
next command. A large tick that dispatches a new delay does not also decrement
that newly created delay. A delay that overshoots zero retains its signed
remainder and does not carry unused time into later commands.

Camera requests retain signed coordinates and the original third-word
nonzero test. The caller applies the request and reports whether the renderer
has reached it. The command index acts as a response token, so a late response
cannot complete a different camera command. For an immediate request, report
success after applying it; the native layer does not add an artificial camera
animation. The current first-mission continuation contains only non-immediate
camera requests.

Request optionals are backing data: only the active gate should be presented.
At an exhausted stream the original tick calls gameplay entry directly rather
than going through the dispatcher's previous-dialogue cleanup branch. The
native mode change preserves that distinction. The actual audited first
mission stops at opcode 8 before this generic end-of-stream case.

## First objective and required gameplay effects

Opcode 7 defines coordinates separately from ownership/state:

| Original field | Native representation |
|---|---|
| map `+0x3f0` | Site coordinate records already decoded in `World` |
| map `+0x400` | Site/group/item coordinate hierarchy in `World` |
| map `+0x3e8` | `MissionSiteState.state` |
| map `+0x3f8` | `MissionSiteState.item_states[group][item]` |

The first opcode-12 instruction is kind 1, description index 245, parameters
`[site=0, group=0, item=0, desired_state=0]`. Its target coordinate is **(5,7)**.
The initial state is `-1`, so it is not initially satisfied. Moving onto that
tile, waiting, or pressing advance does not directly satisfy the predicate.
A real native capture action must update the relevant item state.

The kind-1 wildcard branch has an easily missed rule: if **any** of its three
indices equals `-1`, it compares **all** site states and all nested item states
against the desired state. The other index filters are not retained. This and
the vacuous empty-container result were checked against original execution.
Other negative/out-of-range indices fail explicitly in native code rather
than reproducing unchecked original pointer access.

Opcode 12 keeps the existing phase value (normally 99), sets interaction mode
to gameplay (0), and suspends script ticks. It also has original gameplay
initialization/tutorial/UI work outside this module. `enter_gameplay_mode` is
a required external effect, **not a claim that those systems are implemented**.

When the normal live-player predicate succeeds, original `0x558e4` changes
mode to script (6), clears the active objective flag, resets selection fields
`+0x238/+0x23c` to `-1`, calls the current-player unit reset, and marks phase 99.
The next tick advances beyond opcode 12. `MissionExecution` returns the reset
and mode effects; the action-state owner must apply them. Unit `0x430fc`
clears spent movement and action bytes `0x2c/0x2d/0x38/0x2f/0x39/0x3a`, and
copies the current tile into `0x48/0x49`. It preserves movement offsets and
other fields. The separately recovered movement-action helper can apply this.

The native continuation supports the regular cold first-campaign context.
Player-loss/alternate mission branches stop explicitly instead of selecting a
guessed failure/success jump. It does not run AI, resolve combat, implement
capture costs, invoke tutorial overlays, or write saves.

## Audited first-mission continuation

Resources and the initial prefix 0..8 are described in `WORLD_BOOTSTRAP.md`.
Resource 315 contains 29 commands, not merely a scene description.

| Cursor | Opcode | Original request / condition |
|---|---:|---|
| 9 | 3 | Bumblebee, text index 280 |
| 10 | 3 | Ratchet, 281 |
| 11 | 3 | Bumblebee, 282 |
| 12 | 3 | Ratchet, 283 |
| 13 | 4 | Camera (5,7), non-immediate |
| 14 | 3 | Ratchet, 284 |
| 15 | 4 | Camera (1,2), non-immediate |
| 16 | 3 | Bumblebee, 285 |
| 17 | 3 | Ratchet, 286 |
| 18 | 12 | Kind-1 capture goal, text 245; item (0,0,0) must become state 0 |
| 19 | 5 | Wait 1000 ms |
| 20 | 3 | Ratchet, 287 |
| 21 | 3 | Bumblebee, 288 |
| 22 | 3 | Ratchet, 289 |
| 23 | 3 | Ratchet, 290 |
| 24 | 5 | Wait 1000 ms |
| 25 | 1 | Original phase-1 transition |
| **26** | **8** | **Explicit stop before completion presentation**, text 202 / ID `0x052b` |
| 27 | 5 | Original next wait 1000 ms; not reached past the unsupported presentation |
| 28 | 10 | Save/profile/campaign handoff; not executed |

## Verification and limits

| Check | Result |
|---|---|
| Original text switch | 972 cases, every 486 indices × two variants; three invalid indices checked |
| Original dialogue function | 476 full function executions, including actual input helper, page/reveal/skip branches |
| Original captured-item predicate | 164 case shapes, each run with active counts `[2,1]` and the first-campaign `[1,1]`: 328 executions |
| Original objective success/reset | Executed within successful predicate cases; mode/selection/cursor and unit fields checked |
| Original mission clock | 48 delay/transition cases; separate phase-99 next-dispatch observation |
| Native Text tests | 1,048 checks, including malformed UTF-8/table inputs |
| Native MissionExecution tests | 716 checks, including the original vectors and explicit boundary contracts |
| Actual localization resource | All 511 entries, IDs, offsets and UTF-8 bytes compared exactly |
| Actual mission resource | All 18 entries from cursor 9 through unconsumed 26 observed under explicitly synthetic presentation/capture feedback |

The reference maps hash-checked original code read-only. Only imported integer
arithmetic, synthetic font selection/metrics, recorded audio notification, and
the clock-only dispatcher boundary are supplied externally. No original
dialogue branch or capture result is replaced by a predetermined outcome.
The full original dialogue allocator/font renderer and the actual capture
action are not executed by this reference. The first-script resource probe is
not UI, capture, full-mission, iOS or device evidence.

Portable reproduction instructions and source are in
`analysis_tools/mission_execution/README.md`. Evidence summaries without game
text payloads are in `mission_execution_validation/` and
`MISSION_EXECUTION.json`. No original binary, extracted text payload, test
interpreter or CPU backend belongs in the app target.
