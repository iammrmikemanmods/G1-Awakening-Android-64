# Native objective evaluation and script resumption

`MissionObjectives.hpp/.cpp` decode every objective kind used by the supplied
game's 30 mission streams and recover the original evaluator's control-flow
decision. The native implementation reads actual game state supplied by its
caller. It never executes the old executable or treats UI input as gameplay
progress.

## Verified rules

| Kind | Arguments after kind/text | Original behavior |
| --- | --- | --- |
| 0 | site, desired state | Compare site owner/state. Site -1 requires every site's own state to match; nested items are not checked. |
| 1 | site, group, item, desired state | Compare the indexed item's state. Any index -1 selects all site states and every nested item, ignoring the other index filters. |
| 2 | target | A nonnegative type succeeds when that type's object is absent or inactive. -1 selects faction 0 and -2 faction 1, resolves that faction to an owner slot, then requires every object in that owner list to be inactive. |
| 3 | target, x, y | A named type must have exactly those byte coordinates and zero movement offsets. This branch does **not** check active. Target -3 resolves the current-owner byte through the faction table, rejects controller flag exactly 1, then accepts any active unit in the selected owner list centered at the coordinate. |

The kind-3 wildcard's faction lookup is a literal preservation of the original
comparison, including swapped faction tables; it is not simplified to the
current owner index. The explicit named/wildcard active-state difference is
covered by targeted tests.

Before evaluating the selected kind, the original checks owner-0 active count
zero, the group-0/index-9/type-12 special branch, and owner-1 count zero. An
empty owner-1 list does not short-circuit the first group-0 mission's objective.
The evaluator obtains type-12 activity from the actual indexed unit object;
it does not use a stale duplicated flag in `CampaignContext`.

After success, original `0x558e4` clears objective-active, returns interaction
mode to 6, resets the current player's action state through `0x430fc`, and
sets phase 99. A group-0 loss normally seeks the **last later opcode 1**, with
the stored cursor one command before it; group-0 mission index 16 is an
exception. The special mission-9/type-12 branch also takes that search.
Kind 2 targeting type 0 has an additional original branch that sets dialogue
variant byte +0x231 and advances the stored cursor by 10 before the success
handler. No invented victory/loss screen replaces these script branches.

## Native controller integration

`MissionExecution` has an explicit campaign-context constructor in addition
to its existing first-campaign default. The resource ID must match the
original table and the bootstrap must be at an unconsumed dialogue boundary.
General world bootstrap/command support remains a separate implementation
slice; this constructor does not manufacture a next mission's world state.

Opcode 12 decodes into `state().mission_objective` for all four kinds. The
first-kind-1 compatibility view remains in `state().objective`; it is empty
for other kinds. Kind 1 keeps gate `captured_site_item`, other kinds use gate
`objective`. Both suspend ordinary script ticks.

Use the full observation overload with actual sites, units indexed by original
type (including inactive objects), campaign context and current owner. A
successful decision returns action-reset and return-to-script effects; the
caller must apply them. On the next tick the recovered resume cursor controls
which command enters. The dialogue body uses the recorded alternate variant
where applicable. The legacy two-argument capture observer remains explicitly
limited to the first mission and retains its earlier stop behavior for loss
branches; it is not the general evaluator.

Shared `MissionSiteState`, `CapturedSiteObjective` and their helper declarations
now live in `MissionObjectives.hpp`; their names and first-goal behavior are
unchanged. The native target must compile `src/MissionObjectives.cpp`.

Invalid unit/site indices, missing faction mappings, malformed argument counts,
out-of-script jumps and unknown kinds fail. The controller rejects a negative
resume cursor from a malformed/custom transition sequence; all original
mission resources have their later transition. It does not reproduce unsafe
original pointer arithmetic or an unbounded missing-faction scan.

## Original evidence and verification

The source image SHA-256 is
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.

| Evidence | Location |
| --- | --- |
| Opcode-12 argument installation | 0x57040–0x571a6 |
| Full objective evaluator | 0x559b4–0x55d4a |
| Kind 0 | 0x55a30–0x55a8e |
| Kind 1 | 0x55a90–0x55b6c |
| Kind 2 | 0x55b6e–0x55be8 |
| Kind 3 | 0x55bea–0x55cc2 |
| Extra type-0 cursor/variant branch | 0x55cc4–0x55d12 |
| Success and reverse-transition search | 0x558e4–0x559b2 |
| Actual current-player action reset | 0x55840(false) → 0x430fc |

The development oracle executes the complete original evaluator, success
handler and per-unit reset across **400 cases**, replacing **no external
function**. It covers all four kinds, absent/inactive objects, offsets, faction
swaps, controller flags, zero-unit branches, mission-9 and mission-16 cases,
the +10 branch, multiple or missing later transitions, and both current owners.
It checks that current-owner objects reset and other-owner objects do not,
and that movement offsets are preserved.

The native objective test passes **3,217 comparisons/guards** against those
original outputs. The updated mission-controller suite passes **731 checks**,
including a real-state inactive-unit observation, delayed script resumption,
loss-transition selection and stale-campaign rejection. An independent native
archive probe decodes **all 31 actual objective commands** across all 30
streams, with kind counts **3, 1, 25, 2**. The archive SHA-256 is
`663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4`.

The reference world observations are synthetic and independently varied;
these tests are not a campaign playthrough. The native evaluator emits the
control-flow/reset decision; actual movement, capture, combat, persistence
and presentation remain their respective native subsystems' responsibilities.
