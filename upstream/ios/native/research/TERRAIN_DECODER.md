# Native terrain data and interior mesh

This component is a C++17 reconstruction of bounded resource and terrain-face
operations from G1 Awakening iOS 1.1.3. It reads original data and emits native
vertex arrays. It contains no original machine code, guest CPU, compatibility
runtime or executable loader. It is not a complete game or a device result.

## Recovered first-mission linkage

The campaign table at original VA `0xcec18` begins with resource315. That stream's
opcode9 arguments are `[0,0]`. The native handler at `0x5608c..0x578fc` uses the
map table at `0xcec60`: map index0 is resource268, a **15×14 grid**. The second
argument is style0. Its raw rule lookup is resource282, followed by the original
handler's explicit overrides. World bootstrap owns that selection and override
logic; Terrain decodes the unmodified lookup bytes.

Style0 clears all32 effective lookup entries, then assigns1 to tile types
1,2,3,4,5,26,27 and3 to types6,12,13 (`0x56d3c..0x56de0`). Style1 selects raw
lookup280; style2 selects281 and overrides8→1,19→0,30→1,31→1. A raw lookup alone
therefore does not establish a mission's effective terrain rules.

## Data formats and strict supported profile

| Input | Recovered framing | Original evidence | Native constraints |
|---|---|---|---|
| Grid resources258–279 | `u8 width, u8 height, width*height row-major bytes` | `0x50522`, indexing `0x496bc` and `0x6aba0..0x6abb4` | Dimensions1..255; exact extent; tile0..31 or empty sentinel255 |
| Lookup resources280–282 | One discarded leading byte, then lookup bytes | `0x50390`, especially `0x5047a..0x504ba` | Observed marker1 and exactly32 entries; marker meaning is unresolved |
| Rule resource306 | `u8 row_count`, then `u8 field_count` and fields for every row | `0x53c5c→0x2a714→0x2a2ba` | Nonempty counted rows, each exactly3 bytes; exact consumption |
| Visual tables |32 five-byte records for each of three styles | Style selection `0x6e310..0x6e326`; record accesses in `0x6a2a4..0x6b8a4` | Port data envelope is exactly480 bytes; four atlas selectors0..31 and signed height byte |

All22 original grids contain4,510 cells;21 are15×14 and one is10×10. None uses
the empty sentinel. The grid decoder preserves that original sentinel, but
mesh generation rejects it until its complete scene-routing behavior is
implemented. Resource306 has six three-byte rows. Movement consumes raw field0
as terrain cost and raw field2 in its special traversal rule; this decoder does
not assign a speculative meaning to field1.

The executable's three visual tables are extracted at build time from the
supplied original IPA. They are **data**, not code:

| Style | Original table VA | Records | Terrain atlas resource |
|---|---:|---:|---:|
|0|`0xd9084`|32×5 bytes|693|
|1|`0xd8f38`|32×5 bytes|777|
|2|`0xd8fe2`|32×5 bytes|761|

`native/tools/extract_terrain_tables.py` verifies the pinned IPA and executable
hashes, validates the Mach-O command and file-backed segment extents, and reads
only those table ranges. Its480-byte output is style-major0,1,2. Metadata records
the original virtual/file offsets, atlas identities, source hashes and per-table
hashes. No original executable is written to disk by this tool or placed in the
native bundle. The original table data must not be committed to Git.

## Geometry and texture coordinates

`terrain_cell_quads` reconstructs the observed single-cell path through
`0x65310→0x71248→0x70f70`. Four selectors identify top, front, left and right
faces. Original append order is front, right, left, top. Raw coordinates use
positiveX for columns, positiveZ for rows and positiveY for height. Cells span
32 world units. A visual record's signed height byte is multiplied by32.

Neighbor-height tests determine front/right/left faces; the top bit is16. The
front neighbor is the next row. On style0's final row, the original uses visual
tile13 as the front neighbor. Style0 adds16-unit bevel extensions beside lower
neighbors and shifts the next cell's top edge by16 when the previous cell's
right face is present. These are original rules, not a flat-board approximation.

Each face emits four original strip vertices. Triangle-list consumers use
indices`{0,1,2,2,1,3}`. The module does not flip axes or winding. `position_scale`
is0.01, as recorded from the original terrain finalizer called with`r1=1.0`.
Consumers can retain raw units or apply that scale consistently to both terrain
and the later recovered scene/camera transforms.

All three atlases are512×256. Original`0x714c0` receives64×64 cells, giving an8×4
atlas. Its quantized UVs use signed16 values, with a width-derived128 inset on
**both** axes. Original terrain finalization uses mode1: scale`1/65535` and bias
with float bits`0x3f000080` (`0.50000762939453125`). The single-cell path sets the
additional subdivision ratio to1. The API returns raw quantized UVs and their
normalized coordinates. It does not substitute conventional pixel-edge UVs.

`build_terrain_mesh` generates only cells inside the grid. The complete original
constructor also adds screen/camera-dependent repeated columns, a front boundary
atZ=−32, and three extra rows. Those are not included. The interior API explicitly
starts each row with previous mask`0x10`; omitted outer columns can affect the
left-edge bevel state in the complete scene.

Style1 has an additional animated face14 routing branch, gated at`0x6a404` and
handled in`0x6af02..0x6b1e8`. The explicit overload taking decoded resource307
now emits its separate resource420 surface and ordinary base surface. The
three-argument overload rejects style1 without this configuration. Exact
fixed-face geometry, candidate capacity and the250ms texture clock are recorded
in`TERRAIN_ANIMATION.md`; the transient shared-RNG decoration pool remains
separate work. Styles0 and2 retain the original three-argument interface.

## Validation actually performed

1. **Independent source-data check:** all22 grids, all3 lookups, the6 rule rows
   and all96 visual records match the independent Python framing and compact
   reference hashes. Direct native decoding and native Archive lookup agree.
   Every result reproduces its original source bytes exactly.81 mutations of
   original inputs are rejected, including semantic field corruption with valid
   outer framing.
2. **Original face-function oracle:**128 synthetic cases execute unchanged
   original atlas setup and face-builder instructions, covering every face mask,
   bevel mode, previous-right-face state and all32 atlas cells. The native test
   matches256 quads and5,760 scalar comparisons.41 additional format/API contracts
   pass. The fixture contains synthetic data/results, no original payloads.
3. **Actual first-grid geometry:** the development oracle executes the original
   neighbor-mask block`0x6ae1a..0x6af02` for all210 cells of grid268/style0, then
   the original face builder. All255 native interior quads match the original
   positions and quantized UVs (5,100 scalar values). This uses the documented
   interior-only row boundary, not a claim to execute the full scene constructor.
4. **Transform/strip recording:** original`0x71884` and`0x715d0` confirm the
   terrain position/UV transform and separate four-vertex strips.

The development instruction oracles map the unchanged original ARMv6 libgcc for
integer and float arithmetic. Their substitutions are bounded allocation/free,
ownership and M3G API recording, engine singleton, unwind registration, and the
triangle-strip factory boundary. No geometry or UV arithmetic is hooked or
replaced. These oracles are excluded from the native product.

Reports are in`native/research/terrain_validation/`:
`native_terrain_validation.json`, `original_terrain_oracle.json`, and
`original_first_grid_oracle.json`. The compact distributable source reference is
`native/tests/terrain_reference.json`.
The subsequent animated-face slice adds `original_animated_terrain.json`,
`original_animated_maps.json` and `native_animated_terrain.json`, described in
`TERRAIN_ANIMATION.md`. Historical reports retain their original narrower scope.

## Reproduction

Run from the project directory containing`native/`, with the user's original
IPA supplied locally:

```sh
python3 native/tools/extract_terrain_tables.py ORIGINAL.ipa \
  --out LOCAL_ASSETS/terrain_visual_tables.bin \
  --metadata LOCAL_ASSETS/terrain_visual_tables.json

python3 native/tests/verify_terrain.py ORIGINAL.ipa \
  --out LOCAL_VALIDATION/terrain

g++ -std=c++17 -O2 -Wall -Wextra -Wconversion -Werror -pedantic \
  -Inative/include native/src/Terrain.cpp native/tests/terrain_test.cpp \
  -o LOCAL_VALIDATION/terrain_test
LOCAL_VALIDATION/terrain_test native/tests/terrain_cases.txt
```

The original-instruction oracles additionally require the local audited original
executable, original ARMv6 libgcc, and development-only Unicorn. They live in
`analysis_tools/terrain/geometry_oracle.py` and`map_oracle.py`; their`--help`
documents explicit input paths. Normal native verification needs none of those
runtime dependencies.

The input IPA SHA256 is
`b24fbe5cc4d5cb603189a476c4f81dd2a6ecc18b025eae913262055a7fc443fd`.
The visual data output SHA256 is
`b870e3d74244741003c8068178e7f58bb5fbe35599599c918e024227463725a8`.

Camera/scene placement, unit anchors, complete terrain animation, GPU pixel
fidelity, complete gameplay and iPhone execution are separate evidence gates.
These CPU/resource results establish none of those outcomes.
