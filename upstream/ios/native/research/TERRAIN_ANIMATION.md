# Style1 animated terrain: fixed surfaces and original clock

This extends the native C++17 Terrain module with the original style1 face14
route. It produces two explicit surfaces from local game data: the ordinary
terrain atlas and the separate animated atlas. Product code executes no original
machine code, CPU interpreter or compatibility runtime. These results establish
CPU data/geometry behavior, not GPU or iPhone execution.

## Data and material linkage

Original renderer setup at `0x6e8ca..0x6e940` loads tagged resource420 and calls
`0x714c0` with 32×32 cells. The supplied image is 256×32, giving eight atlas
cells. Its SHA256 is
`1d000c28b8d2a8082b4c7ccf8e2c0ee119bcb36e76621d3763d9e0059f297a85`.
The ordinary style1 material remains resource777, a 512×256 atlas.

Resource307 supplies animation configuration. Original `0x2a85a..0x2a926`
loads four consecutive counted tables of counted byte rows into shared
configuration offsets `+0x28/+0x30/+0x38/+0x40`. The native decoder consumes
the complete input, bounds each original byte count, and requires the observed
three style rows in table0. The original resource has table row counts
`3,3,5,2`, and SHA256
`1f3a38cb119bf213df68bc46951a903a9e0f70fa77bfcfe13d15f8248b3df8f6`.
The report retains metadata, not the resource payload.

The controller linkage is explicit: renderer global `0xd8dcc` receives the
campaign/config controller from application `+0x3e0`. Controller initialization
`0x4750e..0x4751a` assigns application `+0x3f0` to controller `+0xdc`.
`0x45a54` returns the low byte of the third table count at shared config `+0x3c`.
A nonzero count enables the style1 face14 route. `0x474a0` and `0x45a5e` read
the selected style's table0 row for decoration candidate types.

## Fixed-face geometry

The unchanged original block `0x6af02..0x6b1e8` routes matching faces to a
separate mesh. Its append order is **top, front, left, right**; ordinary base
faces retain their original front, right, left, top order.

- A top selector14 emits an animated top quad and suppresses the entire base
  cell, including otherwise ordinary side faces.
- Selector14 side faces emit only when their corresponding neighbor mask bit
  is present. Their lower edge is exactly `top - 64` original world units,
  independent of the ordinary neighbor-height bottom.
- Routed side selectors are changed to -1 in the original face vectors. The
  unchanged base face builder skips these selectors.
- The fixed animated faces initially use atlas cell4 when a transient pool
  exists, otherwise cell0. This condition is derived from the original pool
  capacity, not from the appearance of the texture.

Positions retain the original four-vertex strip order, positive X/Z grid axes,
and 0.01 position scale. Animated UVs come from original `0x714c0`: an 8×1
layout, signed16 coordinates, and a width-derived256 inset on both axes.
Original finalization uses mode1, scale `1/65535`, and bias bits `0x3f000080`.
The native triangle-list convention remains `{0,1,2,2,1,3}`.

`TerrainMesh::animated_surface` holds resource420 and its fixed quads. Consumers
must retain a separate material and add the animation U translation only to
this surface. The base terrain has no animated translation. The three-argument
style1 mesh builder still rejects missing configuration; pass decoded
`TerrainAnimationConfig` to the explicit four-argument overload.

## Clock and candidate capacity

`advance_terrain_animation` reconstructs the isolated original clock at
`0x6d6ea..0x6d758`: add elapsed milliseconds; when the accumulator is at least
250, subtract250 **once** and advance phase modulo4. Excess time remains in the
accumulator. It does not consume every elapsed phase in one call. A later
zero-delta update can therefore consume an existing backlog. The original
texture translation at `0x6d7aa..0x6d7c8` is `(phase * 0.125, 0, 0)`.
Native state rejects an invalid phase or signed32 time overflow without mutation.

The first constructor pass `0x6a3cc..0x6a782` counts candidate cells over the
real columns and height+3 rows, clamping the additional rows to the last source
row. Each matching tile counts once. Division by7 gives the transient pool
capacity. `decoration_slots` exposes that exact capacity because the fixed-face
UV selector depends on whether it is zero. It **does not mean transient quads
have been generated**. The original shared-RNG selection, occupied candidate
bytes, per-slot phases and dynamic placement remain a separate implementation
task; no host random choice or per-display-frame draw is substituted.

## Validation performed

| Check | Result |
|---|---:|
| Synthetic original face-routing cases |128 |
| Base / animated quads in synthetic cases |216 /88 |
| Original clock cases |64 |
| Native scalar comparisons and malformed-state contracts |7,231 |
| Actual style1 campaign map/style pairs |11 |
| Actual interior cells |2,310 |
| Actual ordinary / animated quads |2,499 /297 |

All synthetic positions, selectors, face order and quantized UVs match unchanged
original execution. Clock accumulator, phase and U-translation float bits also
match. The native config decoder matches independent counted-row decoding of
the actual resource307. Actual-grid checks execute the complete bounded first
count pass, original controller accessors, original neighbor-height block,
original routing block, and original face builder. All two-surface outputs and
pool capacities match exactly.

| Grid resource | Base quads | Animated quads | Original pool slots |
|---:|---:|---:|---:|
|258|212|22|16|
|259|230|22|11|
|260|222|36|17|
|261|216|22|14|
|262|242|24|12|
|265|230|34|15|
|266|242|24|12|
|267|221|31|11|
|269|228|24|11|
|275|232|32|10|
|279|224|26|14|

The original-instruction harness maps the pinned original ARMv6 image and libgcc
read-only and checks they remain unchanged. It records allocation/free, M3G API
and ownership, engine singleton, nonthrowing unwind and triangle-strip factory
boundaries. The actual first pass additionally uses an exact bounded imported
`memset`. No geometry, candidate-count, config-accessor, UV, timing or random
instruction is replaced. Explicit endpoint observation stops isolated blocks
without modifying their instructions or computed state.

The original clock cases use zero transient slots to isolate fixed-face phase
behavior. They do not validate the later random reselection callbacks.
Actual geometry uses the interior-only row boundary documented in
`TERRAIN_DECODER.md`, with camera padding zero in the first pass. Outer padded
geometry, transient decorations, GPU pixels, device performance and a complete
campaign playthrough remain unverified by this slice.

Reports and fixture:

- `terrain_validation/original_animated_terrain.json`
- `terrain_validation/original_animated_maps.json`
- `terrain_validation/native_animated_terrain.json`
- `../tests/terrain_animated_cases.txt` contains synthetic inputs/results only.

## Reproduction

From the directory containing `native/`:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wconversion -Wpedantic -Werror \
  -I native/include native/src/Terrain.cpp native/tests/terrain_animated_test.cpp \
  -o LOCAL_VALIDATION/terrain_animated_test
LOCAL_VALIDATION/terrain_animated_test native/tests/terrain_animated_cases.txt

g++ -std=c++17 -O2 -Wall -Wextra -Wconversion -Wpedantic -Werror \
  -I native/include native/src/Archive.cpp native/src/Terrain.cpp \
  native/tools/terrain_probe.cpp -lz -o LOCAL_VALIDATION/terrain_probe
LOCAL_VALIDATION/terrain_probe animation_config --archive LOCAL_ASSETS/iphone.big 307 0xf4e02223
LOCAL_VALIDATION/terrain_probe mesh GRID_FILE TERRAIN_TABLES_FILE 1 RESOURCE307_FILE
```

The development-only scripts `analysis_tools/terrain/animated_oracle.py` and
`animated_map_oracle.py` list explicit pinned local input arguments in `--help`.
They require the original executable/libgcc and Unicorn only to generate the
independent evidence. Normal native compilation and fixture tests require none
of those instruction-execution dependencies. Original images, config, grids,
tables and local binaries are excluded from the source checkpoint.
