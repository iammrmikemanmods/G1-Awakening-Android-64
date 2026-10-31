# Native opponent planning and admission

`AI.hpp` / `AI.cpp` reconstruct the original opponent's planning and idle-turn
admission in portable C++17. `AISite.cpp` and `AIIncome.cpp` contain the separately
recovered capture candidate and hypothetical income helpers. Product code uses
typed native data; it does not execute the original ARM image or include a guest
CPU. Returning a plan or admission request is not evidence that its action, a
complete opponent turn, or a campaign has been executed.

## Recovered interfaces

| Native interface | Original function | Scope |
|---|---|---|
| `ai_position_score` | `0x4e8f4` | Temporary candidate position, enemy threat and site/item income score; restores the actor's source position. |
| `choose_ai_attack` | `0x50ed6` | Attack form, enemy order, movement candidate enumeration, exact tie rules and plan fields. |
| `choose_ai_capture_plan` | `0x508a2` | Original site/item candidate selection, including captured-network income potential. |
| `choose_ai_fallback_plan` | `0x4ec72` | Site/item fallback or gradient approach toward an enemy, possible upgrade discriminator, and explicit post-planning form/stats. |
| `plan_ai_unit` | `0x512e6` | Compare attack and capture; ties choose capture. Invoke fallback only when both are absent. |
| `ai_owner_slots` | `0x496e4` | All 23 type objects in ascending type order, assigned by faction; includes inactive/unspawned types. |
| `choose_ai_turn_admission` | Idle branch of `0x58dcc` | Two production priorities, ordinary owner-slot cursor traversal, or an explicit end-turn request. |
| `ai_production_destination` | `0x4a678`, phase 0 of `0x58dcc` | First vacant reachable neighbor in north, west, east, south order; no spawn or funds write. |
| `compute_movement_range_with_budget` | `0x4a29c` | Explicit signed initial search budget. The existing ordinary movement wrapper retains its previous behavior. |

### Inputs and action ownership

`AIWorldView` borrows the current combat rules, movement board, aligned combat
and movement unit vectors, site topology and capture state. It also requires
the actual current player, controller bytes, player factions, funds, available
types and persistent `map+0x34` owner-slot selections. The movement board must
already describe current stationary occupancy. An inactive object and an absent
active placement are distinct from the full owner-slot table.

The full owner slots matter: original `0x496e4` considers every type object,
including unspawned ones. A slot stored in `map+0x34` must not be interpreted as
an index into only the active native roster. Definition factions supply the
default for absent types; live faction overrides take precedence. Ordinary
campaign factions `[0,1]` were used in the driver comparison. The implementation
also preserves the original faction membership operation when both owners
reference the same faction, without claiming that such a campaign was played.

`AIPlan` exposes the original plan fields without guest pointers. Discriminators
are 0 move, 1 attack, 2 capture, 3 heal, 4 move then upgrade. The inspected
`0x512e6` call chain generates 0, 1, 2 and 4; the heal consumer exists, but no
heal candidate is called by that planner. Healing and upgrading require their
separate execution adapters. Production admission returns the producing unit
and requested type; the caller must run the real native spawn/cost operation.

The caller must apply `AIPlanningResult.form_after_planning` and its stats even
when no plan exists. These are the original planner's form/stat side effects,
not an ordinary player transform: they must not invent animation, action-spent
or transform flags. After planning, the original action consumer can separately
request a normal transform to `plan.movement_form`; arrival has another guarded
transform controlled by `transform_on_arrival`.

For ordinary admission, commit `next_cursor` after planning, as the original
driver does. Production preserves the cursor. `turn_finished` requests the
existing native end-turn transition; it does not perform that transition.

## Behavior that must not be simplified

### Attack and score

Attack search starts with the current form if its raw attack-low field is
nonzero, otherwise the alternate form if that field is nonzero. Its explicit
budget is the signed movement field of the original current form, without
subtracting spent movement. A form-zero signed maximum range greater than one
forces budget zero for controller value 1. Candidates exclude negative and 127
movement-field cells. Enemy types are visited in original ascending owner-slot
order; candidate coordinates scan y before x.

The per-enemy choice is not a simple maximum over all cells. A best position
that is a site/item can prevent a later greater score from replacing it; the
actor's current tile has a separate preference. Across enemies only a strictly
greater score replaces the earlier candidate. Score −1 is the original
no-result sentinel, including when a candidate really scores −1.

Original `0x43954`, used by the score, overwrites its nominal target/form
arguments through `0x4392a`: it tests whether any opposing active unit is in
the actor's current-form range. The native implementation preserves that
behavior instead of substituting a target-specific test. Score evaluation
consumes no RNG words. Signed scalar modifiers and 32-bit wrapped score
arithmetic are preserved. Original signed-byte site coordinates do not match
positive unit coordinates above 127.

### Fallback, routes and form state

Fallback can temporarily choose form 1 when its signed base movement exceeds
form 0 and signed definition scalar 2 permits it. The long search budget is
terrain-rule row 4 field 0 multiplied by width plus height. Site and item
filters use the actual original movement field and ownership rules. The last
eligible site wins; item candidates instead compare scores. The original item
fallback calls the site-income helper, which is intentionally preserved.

If no site/item candidate exists, the original gradient walk approaches active
enemy units, taking the shortest positive route and preserving type/tie order.
Its destination can itself be a blocked or negative-field enemy tile. The
ordinary human movement wrapper rejects such a destination, so it is not
reused for this AI walk. Step admission preserves the original first-expensive-
step rule; occupied endpoints are backed up along the route. Cycles,
disconnected gradients and overflow of the original bounded route storage
raise explicit unsupported-input errors. No alternate pathfinder is used.

The successful enemy-approach branch can toggle the temporary form an extra
time before common cleanup. This can leave the unit in the alternate form,
including cases where a later result is absent. The original128-case comparison
includes 38 observed form changes. Returning only a plan would lose this state.

Fallback changes a result to action 4 only when the actor's original owner slot
matches the stored `map+0x34` selection and `0x4ec0e` permits upgrading: active,
not acted, form-zero field 12 is not 1, level at most 24, and owner's funds meet
the effective signed map+0x14c cost. The original loader forces type21 to40;
other types retain signed scalar1. Applying the upgrade is a separate operation.

### Idle production and destination

The idle driver first considers active producers whose form-zero and selected-
form field 12 both equal 1. Only if none yields a candidate does it consider
field 9 in the same way. Available inactive production types are filtered by
the original either-form field-8 predicate. It chooses the largest positive
effective signed cost affordable by the current player; equal costs keep the
earlier type. The last eligible producer in a priority pass wins. Production
has no action-spent gate in this original branch.

Adjacency filling uses budget 1 with cost mode zero: a non-99 raw terrain cost
becomes 1, while 99 remains impassable. Point/region rules do not participate;
occupancy is tested afterward. An origin on raw cost 99 requires selected-form
signed field 6 greater than zero. The execution phase then prefers north,
west, east, south. That phase reloads the selected coordinates as signed bytes,
unlike the admission helper's unsigned unit coordinate loads. The APIs retain
that distinction. Neither admission nor destination selection creates a unit.

## Validation

Expected outputs come from unchanged original functions executed only by the
development oracle. They are not generated from the C++ implementation.

| Corpus | Original coverage | Native result |
|---|---|---|
| Synthetic attack/score | 192 worlds; 768 score queries; 192 explicit-budget movement fields, including negative and greater-than-byte budgets | 6,479 checks passed |
| Synthetic fallback/full choice | 137 worlds including9 type21 cost boundaries; entire `0x4ec72` and `0x512e6`; 97 fallback plans, 111 combined plans, 38 form changes | 8,050 checks passed |
| Synthetic idle driver | 201 worlds including9 type21 cost boundaries; 65 ordinary requests, 50 production requests, 86 end-turn boundaries; all50 production destinations | 15,633 checks passed |
| Actual definition attack | All 23 original types, both forms, two levels; first grid/style/rules, synthetic adjacent placements | 92 cases, 20,718 checks passed |
| Actual first-map planner | All 23 definitions, grid 268/style 0, first sites and placements; eight explicitly seeded AI/funds/ownership scenarios | 8 cases, 1,994 checks passed |
| Existing ordinary movement regression | Existing unchanged original-function fixture | 256 cases, 9,092 comparisons and 4 malformed-input checks passed |

The native fixture test also rejects six malformed shape/state inputs. The
current five AI fixture runs pass with the undefined-behavior sanitizer. The
shared oracle now seeds the original loader's type21 override in map+0x14c;
all five corpora were regenerated and rerun. `EFFECTIVE_UNIT_COST.md` records
the defect, loader execution and exact39/40/41-credit boundary evidence. The
capture candidate and income helpers have their separately recorded original
comparisons; their evidence is in `AI_CAPTURE.md` and
`ai_site_validation/original_ai_site.json`.
No complete opponent turn, save/reload, full campaign, audio/lifecycle or XR
performance result follows from these function comparisons.

The original image SHA-256 is
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.
The original ARMv6 libgcc SHA-256 is
`d758fbf1ac952532955d62866bd02df55315c0599f64bdd978ff941fccbdcf83`.
Both are mapped read-only and checked after execution. Imported arithmetic
transfers to original libgcc. Bounded malloc/free, exact byte memory operations
and nonthrowing SjLj registration are explicit infrastructure boundaries.
No AI game helper, result, damage formula, score or RNG output is substituted.
Driver comparison stops at natural action/turn/spawn entry boundaries before
execution, rather than pretending those actions succeeded.

The first-map source comparison explicitly seeds AI player 1, controller bytes,
funds, income counters, selected upgrade slot and a few ownership/position
changes. It is not an observed live campaign turn. Source-data fixtures contain
decoded original definition bytes and remain outside the committed native
tree. Synthetic fixtures are the only committed expected-value inputs.

## Reproduction and records

From project root:

```sh
g++ -std=c++17 -O2 -ffunction-sections -fdata-sections \
  -Wall -Wextra -Werror -pedantic -I native/include \
  native/src/AI.cpp native/src/AISite.cpp native/src/AIIncome.cpp \
  native/src/CaptureAction.cpp native/src/Combat.cpp native/src/Movement.cpp \
  native/src/World.cpp native/tests/ai_attack_test.cpp -Wl,--gc-sections \
  -o research/native_ai/ai_planner_tests
research/native_ai/ai_planner_tests native/tests/ai_attack_cases.txt
research/native_ai/ai_planner_tests native/tests/ai_planner_cases.txt
research/native_ai/ai_planner_tests native/tests/ai_turn_cases.txt
```

The ordinary native CMake/CTest targets need only native source and synthetic
fixtures. `analysis_tools/ai/README.md` gives optional original-oracle commands.
Compact provenance, fixture hashes, native source hashes and exact result
counts are in `ai_validation/original_ai_oracle.json` and
`ai_validation/native_ai_validation.json`. Older execution records retain
their historical oracle hashes; current oracle file hashes are recorded
separately instead of rewriting history. The final production fixture uses X
for its optional destination record so it cannot collide with END.

Do not package local ARM images, libgcc, archives, source-data fixtures, raw
reports containing source payloads, test executables or compatibility runtimes
as native source deliverables.
