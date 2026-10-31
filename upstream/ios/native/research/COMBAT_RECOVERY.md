# Native combat core

This module reconstructs the original game's combat arithmetic and the ordered
health transition of a battle in portable C++17. It uses decoded native data and
typed native state. It contains no original executable, guest CPU, compatibility
runtime, imported game code, host random distribution or gameplay-result stub.

The source is `include/g1/Combat.hpp` and `src/Combat.cpp`. The dependency-free
test is `tests/combat_test.cpp`, with original-function output for **synthetic**
inputs in `tests/combat_cases.txt`. The synthetic fixture contains no original
resource payloads. Original assets used by the separate source-data comparison
remain local and are not committed.

## Established behavior

| Native operation | Original evidence | Recovered behavior |
|---|---|---|
| `combat_stats` | `0x4328a..0x43338` | Signed form bytes 3, 4, 5 become signed halfwords at unit offsets `0x1c`, `0x1e`, `0x20`. Each is `base + level * trunc(base / 5)`; their use in damage establishes attack low, attack high, defense. |
| `set_combat_health` | `0x43184..0x43196` | Negative input clamps to zero; other input is stored as a byte. It does **not** clamp at 100 and does not deactivate the unit. |
| `apply_combat_damage` | `0x43198..0x431ae` | Unit byte `0x19 != 0` suppresses both health and timer writes; otherwise subtract damage and call the health setter. |
| Health/countdown | `0x43142..0x43170` | Health 0–9, 10–24, 25–49, 50–74, 75–255 select 750, 1500, 2500, 5000, −1 for fields `0x34` and `0x30`. The countdown's wider presentation role remains unnamed. |
| `GameRandom` seed | `0x7acc..0x7b00` | 624-word MT initialization, multiplier 1812433253, initial index 624. |
| `GameRandom::next_word` | `0x7b26..0x7c0a` | Original in-place MT twist and temper; exact raw unsigned 32-bit word. |
| Type modifier | `0x4384c..0x438c0` | Signed scalar field 3 of the attacker's definition, or scalar field 4 if the defender's selected-form field 6 equals 1. |
| Nearby ally aura | `0x43738..0x437de` | +15 if another active same-owner unit has selected-form field 11 nonzero and is within one tile on **each** axis. Diagonals qualify. This is boolean, not +15 per helper. |
| Defender terrain | `0x437e0..0x43838` | Grid tile → effective style lookup → terrain resource 306 row field 1, interpreted unsigned. Add the defender's nearby-ally aura. |
| Target membership | `0x52fdc → 0x52f84 → 0x43976 → 0x4926a` | Flag `0x38` must be zero, selected-form signed field 3 and scaled attack-low must be nonzero; faction/phase flag gate applies. Target is active, belongs to the other owner, and lies in the inclusive Manhattan range from unsigned form fields 1 and 2. |
| Combined support | `0x43c9a..0x43e2c` | Only when attacker and target are adjacent. Other active units opposing the target's owner, exactly one tile from the target, qualify if selected-form signed field 3 is positive and signed field 1 equals 1. They are ordered by original owner-slot/type order. |
| Strike | `0x43bea..0x43c98` | Signed random variation, modifiers, defense, support and attacker health determine damage; minimum 5. Type 10 has an additional random evade branch. Apply through the immunity-aware health setter. |
| Battle setup | `0x585c4..0x586aa` | Prepare attacker's support, then defender's support only if the counter is eligible; this happens before either strike. |
| Exchange | `0x58172..0x582ec` | Attack first; if defender dies, remove its active state and skip counter. Otherwise an eligible defender counters using its **reduced health**. Apply an attacker death afterward if necessary. |

Definition scalar fields 3/4 are loaded by `0x5a910..0x5a92c` into map vectors
`0x15c`/`0x164`, indexed by original type. Native `UnitDefinition` already
preserves these fields. This introduces no alternate stat table.

### Damage and random-word ordering

Let `L`, `H`, `F` be attack-low, attack-high and target defense; `S` the prepared
support bonus; `A`/`D` the recovered attack/defense context bonuses; and `hp` the
attacker's current health byte. The ordinary calculation in `0x43bea` is:

1. If `H - L != 0`, consume one **signed** interpretation of the next original
   random word, take signed remainder by `H - L`, then its ARM-style absolute
   value. If the span is zero, use zero and consume no word.
2. Calculate `L + S - F + A + variation - D`, multiply by `hp`, divide by 100
   with truncation toward zero, and clamp the result to a minimum of 5.
3. If target type is 10, consume another word. Interpret it as signed, take the
   original wrapping absolute value and signed remainder by 100. A result
   greater than 49 changes damage to zero.
4. Apply damage unless the target's immunity flag is set. The returned rolled
   damage is retained even if immunity suppressed the HP write.

The implementation preserves 32-bit wrapping and the rare `INT_MIN` absolute
value behavior instead of relying on C++ signed overflow. The attack upper
bound is exclusive in the nonzero-span random variation. Replacing this with a
host uniform distribution, unsigned modulo or inclusive upper bound changes
results and subsequent random-word ordering.

Support preparation itself consumes a span draw **even if no helper contributes**.
For qualifying helpers the original uses a 16.16 multiplication with a quarter
per helper; the native implementation preserves the shifts and wrapping. The
support participant list is prepared for both sides before the first strike,
so it can contain defenders' helpers even if the defender subsequently dies.
The original storage has six shared slots; the native API rejects larger
synthetic/unsupported arrangements before changing units or the RNG.

The counter suppression rule reads signed maximum-range field 2 of the
attacker's **form zero**, regardless of its current form. If that value is
greater than one and distance is greater than one, the defender does not
counter. Otherwise the ordinary target-membership predicate is applied in
reverse. A dead defender never counters.

### Seed and state lifetime

The original generator constructor `0x7c44` calls `0x1adc4`; that function calls
the imported `_gettimeofday` at `0xba630` and returns the first 32-bit word of
`timeval`, the seconds value. Microseconds are not included in this seed.

`GameRandom` requires an explicit seed. The application should retain a single
shared stream across actions and future native consumers. It must not reseed
per strike or exchange. Native state snapshot/restore preserves all 624 words,
index and the diagnostic draw count. This state representation is **not** an
implementation of the original save-file format. Index 625, the original
uninitialized-state sentinel, is rejected on native restore because the native
API requires explicit seeding.

## Original-function comparison

The development-only oracle is `analysis_tools/combat/oracle.py` at project
level. It maps the pinned original ARMv6 image and its pinned ARMv6 libgcc code
read-only. Original functions execute unchanged. Imported integer division,
remainder and 64-bit multiplication transfer to the actual original libgcc
exports. The oracle never supplies a damage formula, random output, target
decision, support amount or health result.

The four explicit boundaries are:

- `0x2b46c`: service-registry lookup returns the explicitly allocated, seeded
  original RNG object. The original `0x2b43c` wrapper and `0x7b26` draw execute.
- `0xba1f8`: register a nonthrowing bounded SjLj frame; recorded without a host
  exception chain. Exception imports are rejected.
- `0x5c9b2`: record removal of a destroyed unit's renderer presentation.
- `0x60918`: record clearing its UI string. Other original state writes in the
  enclosing cleanup function still execute.

The setup oracle stops at `0x586aa`, immediately before the support presentation
branch. Resolution starts at `0x58172` and stops at `0x582ec`, after health/death
changes and before cinematic ordering. Death fixtures seed the owner
turn-selection indices to −1; cycling to the next selected unit is outside
this comparison. Original death logging and owner-count writes execute.

| Comparison | Coverage | Result |
|---|---|---|
| RNG | 6 seeds, 1,250 words each, including multiple twist boundaries; all 6 final 624-word states | Exact |
| Signed stat scaling | 36 input cases, including negative signed bases and level 255 | Exact |
| Health mutation | 64 setter/damage cases including byte truncation, immunity, negative/large integer inputs | Exact |
| Strike arithmetic edges | 16 cases including actual original RNG outputs `0x80000000`, `0xffffffff`, `0x7fffffff`, zero, overflow-sized support and negative spans | Exact |
| Synthetic world battles | 192 scenarios; all modifiers, eligibility, support list/order, strike order, damage, HP, active state, countdowns and following random words | 23,845 total checks passed in fixture test |
| Actual decoded data | 92 scenarios using all 23 original types, both forms, levels 0/3, original first grid/style0 lookup/terrain rules | 16,199 total checks passed in separate local fixture test |
| Undefined-behavior sanitizer | Same two fixtures with signed-arithmetic edge cases | Passed without a sanitizer diagnostic |

The synthetic battle set includes 132 counter-eligible scenarios, 100 executed
counters, 86 destruction events, 136 scenarios with support (319 participants),
17 immunity strikes and 33 evades. The source-data set uses synthetic adjacent
placements, not a captured live battle. Some forms intentionally fail the
target predicate; the direct core comparison still exercises the original
callee on those inputs. It does not establish that the UI would authorize
those attacks. Expected results are produced by original execution, not the
native implementation.

`combat_validation/native_combat_validation.json` records the native commands,
source/test hashes and results. `combat_validation/original_combat_oracle.json`
records compact provenance and coverage; the full raw development report and
the source-data fixture stay outside the committed native tree.

## Integration contract and remaining work

`CombatRulesView` borrows the current grid, effective lookup, terrain rules and
all 23 decoded definitions. The caller supplies current typed units, including
health, form, upgrade level, scaled stats, the immunity flag and flag `0x38`.
`make_combat_unit` initializes a newly placed unit at health 100 and level zero;
it does not reconstruct an existing save. After transforming or upgrading,
refresh stats with `combat_stats`. Its level argument is the stored unsigned
byte used by ordinary upgrade/transform paths. The original setter's arbitrary
32-bit register inputs are outside that API; a script integration must validate
or explicitly recover any wider input before calling it.

`combat_can_target` is the recovered target-membership gate. Its caller still
owns turn ownership, active-action selection and the action-spent flag. The
original human attack path is map state 3 with the same selected cursor tile:
`0x58c44 → 0x588b0 → 0x585c4`. The keyboard/action path also reaches `0x588b0`
from `0x595fc`; the AI driver calls `0x585c4` at `0x5903a`. After the call, the
original action path marks unit flag `0x2c` with `0x430f4` and refreshes map/UI
state. Those action transitions are not silently added by the arithmetic core.

`resolve_combat_exchange` applies HP/support/active changes and emits typed
destruction records. The application must synchronize the unit state into its
world, remove destroyed occupancy, apply the appropriate owner counts and
death log, invalidate associated states, advance any current-unit selection,
evaluate mission conditions and update presentation. The original `0x43ac8`
clears active plus associated fields `0x24`/`0x28`; selected-owner cycling is a
separate branch. A destruction event is not mission completion.

Battle cinematics, camera/audio coordination, AI planning, complete turn/economy
rules, upgrade eligibility, capture/objective consequences, actual changed-state
save/cold reload, campaign completion and XR/device performance are separate
gates. These comparisons are native source validation and development-only
original-function evidence. They are not an iPhone test or full-game playthrough.

## Reproduction

From the project root containing `native/`, `analysis_tools/`, `research/`:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -I native/include native/src/Combat.cpp native/tests/combat_test.cpp \
  -o research/native_combat/combat_test
research/native_combat/combat_test native/tests/combat_cases.txt
```

The original-function oracle is optional development tooling. Supply the pinned
local original image/libgcc and the existing isolated Unicorn installation:

```sh
python3 analysis_tools/combat/oracle.py \
  --binary research/binary/G1awakening.armv6 \
  --libgcc ../.tools/g1_runtime/Applesauce/touchHLE_dylibs/libgcc_s.1.dylib \
  --unicorn-dir /tmp/g1-movement-oracle-tools/python \
  --output research/native_combat/original_combat_oracle.json \
  --fixture native/tests/combat_cases.txt --battle-count 192 \
  --archive research/assets/iphone.big \
  --source-fixture research/native_combat/original_source_cases.txt
research/native_combat/combat_test research/native_combat/original_source_cases.txt
```

Omit both archive/source-fixture arguments for the synthetic-only oracle. Do
not commit or package `original_source_cases.txt`, the old executable/libgcc,
the archive, local test binaries or any compatibility runtime. The ordinary
native test needs only its synthetic fixture and native C++ source.
