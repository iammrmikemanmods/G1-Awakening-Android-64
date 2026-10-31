# Native first-mission bootstrap

## Implemented boundary

**g1::bootstrap_first_campaign(const Archive&)** constructs typed native state
from the original first campaign resource and its associated data. It uses
only the native FGIB, mission, terrain and unit-definition decoders. It never
opens the original executable or executes ARM32 instructions.

The result includes the map, effective terrain lookup, terrain rules, decoded
unit definitions, initial unit roster and ownership, site coordinate groups,
available types, a title text index, a camera request and the next command
cursor. Its movement_board() and movement_profile() methods connect that state
to the separately recovered native movement-cost/range implementation.

This is an initialization slice, not a whole mission executor. It extracts
the verified state-bearing effects of the prefix; it records presentation
requests rather than reproducing title/camera timing, UI callbacks, or the
original renderer's staged loading. It returns **before command index 9,
opcode 3**, whose dialogue handler is not implemented. It does not execute a
move, change turns, resolve objectives, run AI/combat, save a game, or complete
a campaign. Unknown commands and unsupported spawn branches stop explicitly
with the current cursor. Invalid data raises FormatError.

## Original identity and first-scene result

The user-supplied ARMv6 executable SHA256 is
0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd.
The original iphone.big SHA256 is
663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4.
All resource IDs below use member key **0xf4e02223**; the original executable's
0x03xxxxxx tag is a resource dispatch tag, not the FGIB member key.

| State | Actual native decode |
|---|---|
| Campaign selection | Group 0, entry 0; mission resource 315 |
| Mission length | 29 commands |
| Grid | Resource 268, 15 columns × 14 rows |
| Style / raw lookup | Style 0, resource 282 |
| Effective lookup | Original style-0 overrides, including raw-lookup replacement |
| Terrain rules | Resource 306, six records of three raw bytes |
| First unit | Type 11, definition 284, faction/owner 0, form 0, tile (1,2), movement budget 2, spent 0 |
| Second unit | Type 19, definition 286, faction/owner 1, form 0, tile (4,7), movement budget 3, spent 0 |
| Site record | (5,10), initial state −1, one coordinate group [(5,7), (10,7)] |
| Available types | 11 and 19 |
| Presentation requests | Raw title index 208; camera request (1,2), immediate flag true |
| Stop | presentation_required, cursor 9, opcode 3 unconsumed |

The recovered movement fill visits 12 cells for type 11 and 11 for type 19,
including each origin. These are budget fields on the initial occupied map.
They are not route choices, executed movement, or evidence of a playable turn.

## Address-level evidence

All addresses are original virtual addresses in the identified ARMv6
executable. Direct Thumb disassembly was checked against the original bytes;
the prior Ghidra function inventory is useful context, but several shared
branch blocks are split into misleading function boundaries.

| Fact | Original evidence |
|---|---|
| First new-game entry | 0x305ba calls 0x300f6 with campaign index 0, group 0 and no faction swap |
| Default faction mapping | 0x51bf2; writes at 0x51d0a–0x51d20 create player factions [0,1] |
| Campaign resource table | 0xcec18, 17 entries; entry 0 is tagged resource 315 |
| Map-index resource table | 0xcec60, 22 entries; entry 0 is tagged resource 268 |
| Unit-type resource table | 0xcecc0, 23 entries; differs from archive ID order |
| Mission framing | Parser 0x48378; opcode-7 loop 0x48698–0x487ac |
| Mission dispatch | Logical dispatcher 0x5608c–0x578fc, including its shared branch blocks |
| Map/style selection | 0x56c90–0x56d04, shared branches 0x57898–0x578cc, call to loader 0x507fc |
| Style overrides | Style 2 at 0x56d08–0x56d3a; style 0 at 0x56d3c–0x56de0 |
| Unit definitions | Loader 0x5a522, field reads 0x5a7f0–0x5a982 |
| Counted data helpers | 0x2a648 (BE16 list), 0x2a3ac (byte list), 0x2a2ba (nested byte lists) |
| Faction → owner assignment | 0x496e4, using unit byte +0xb and map player faction vector +0xa0 |
| Command-6 placement | 0x56602–0x56878; 0x5678e calls 0x54016 |
| Unit activation/reset | 0x54016 calls 0x4333a; form byte +0xc becomes 0, active +0xd becomes 1, exclusion fields +0x40/+0x44 clear, 0x430fc clears spent movement +0x59 |
| Command-7 runtime records | 0x56880–0x56c1a; sites at map +0x3f0, nested groups at +0x400, state vectors initially −1 |
| Available roster | Command 15 at 0x5763c–0x576e0, availability bytes at map +0x24c |
| Camera request | Command 4 at 0x562b4–0x56318, calling 0x557f4 with signed x/y and third-word !=0 |
| Dialogue boundary | Command 3 at 0x56124–0x561a4, calling 0x4dd12; native code stops before it |

### Terrain selection

Style selection is explicitly **{0→282, 1→280, 2→281}**. It must not be inferred
from resource numbering or atlas order. Style 0 replaces all 32 lookup bytes
with zero, then assigns 1 to indices 1,2,3,4,5,26,27 and 3 to indices 6,12,13.
Style 1 preserves its decoded lookup. Style 2 changes indices 8,30,31 to 1 and
index 19 to 0. All 66 combinations of 22 map indices and three styles were
compared with executed original control flow.

### Unit data and movement input

Each definition contains two BE16 text indices, a counted BE16 reference
list, five scalar bytes, a counted auxiliary byte vector, and counted form
vectors. All 23 original definitions have two 14-byte forms. Raw field names
remain numbered where semantics are unresolved. Faction is scalar byte 0;
movement budget is selected form byte 0, and the signed traversal field used
by the original movement function is form byte 6.

The public definition preserves raw data. It does not apply unrelated
original UI transformations such as text-index offsets or type-21's override
of scalar field 1. Neither field is used for this slice's movement inputs.
Only the original first-placement branch is supported: valid explicit type,
the last two command-6 words both zero, no duplicate active type, valid
coordinates and a known default faction. Randomized negative sentinels,
faction flipping and special placement flags stop as unsupported.

An opcode-7 site is not a movement-blocking point. The latter is a separate
vector at map +0x420, populated by opcode 17, absent from the first prefix.
The native movement board therefore adds no blockers at the site or its
coordinate groups. The original renderer predicates 0x4b3aa→0x4b256 and
0x4b06e→0x4aed6 respectively search the site coordinates and nested pair
coordinates. Direct branch/data inspection makes both false at (1,2) and
(4,7); this separate predicate inference has not been counted as an executed
original-function test.

## Verification and reproduction

The source-only g1_world_tests suite has **90 passing checks**. Constructed
FGIB resources exercise the complete native archive → mission → definitions
→ terrain → roster → movement-input path without embedding original game
payloads in the test. It covers truncation and trailing bytes, form widths,
resource-table bounds, style overrides, ownership/form defaults, signed
camera words, variable site groups, occupancy, movement integration,
unsupported-command cursor preservation, missing resources and invalid
positions. These are contract cases, not a substitute for original behavior.

The development reference script analysis_tools/world/world_oracle.py
(authored in the research workspace as
research/binary/native_world/world_oracle.py) performs the stronger checks:

| Original-data / original-code comparison | Result |
|---|---:|
| Independently extracted and field-decoded unit resources | 23 matched |
| Campaign, map and unit resource tables | All original entries matched |
| Original owner assignment 0x496e4 | Executed for all 23 seeded definitions; both native active owners matched |
| Original spawn/reset 0x4333a | Both native initial units matched, including form reset, active state, coordinates and spent budget |
| Original map/style fragment | 66 combinations matched |
| Original movement on the actual first-map inputs | Two 210-cell budget and entry-cost fields matched |

The oracle maps the original code read-only and checks it stayed unchanged.
It uses no instruction patches. Two explicit boundaries replace imported or
infrastructure behavior: imported ___divsi3 at 0xba288 performs signed
integer division toward zero; 0x507fc records the original resource tags
and provides actual decoded lookup bytes. That second boundary does not pick
the tags or calculate the overrides. Unknown imports fail explicitly.
Original ownership has no boundary substitutions; the previously validated
movement oracle also runs without substitutions.

This is bounded reference execution with seeded object layouts and a seeded
dispatcher stack slot. It is not a whole engine boot or original resource
loader test. Unit-definition framing is compared with an independent raw
decode, not execution of the original definition parser.

From a repository containing the native sources and preserved development
tools:

    cmake -S native -B build/native -DCMAKE_BUILD_TYPE=Release
    cmake --build build/native
    ctest --test-dir build/native --output-on-failure
    build/native/world_probe /local/path/iphone.big

    python3 analysis_tools/world/world_oracle.py \
      --binary /local/path/G1awakening.armv6 \
      --archive /local/path/iphone.big \
      --native-probe build/native/world_probe \
      --movement-oracle analysis_tools/movement/oracle.py \
      --unicorn-dir /local/path/to/Unicorn-2.1.4/python \
      --output build/world_original_reference.json

The reference requires the exact original hashes above, Python 3 and external
Unicorn 2.1.4. Its paths are explicit; it can be copied into the development
tools directory without edits. The native build has no Unicorn dependency.
The report is retained at
research/world_validation/original_world_oracle.json.

The associated opcode-7 framing correction passes all 30 original streams,
803 commands, 2,800 argument words and 855 rejection cases. The refreshed
independent reference retains every original canonical decode hash. Its
format-contract suite passes 64 checks.

These newly added World sources have been compiled and executed on Linux
with strict C++17 warnings. Apple build status is reported by the repository
CI separately; these local checks establish no iPhone execution, rendering,
performance, 60 FPS pacing or complete-campaign result.
