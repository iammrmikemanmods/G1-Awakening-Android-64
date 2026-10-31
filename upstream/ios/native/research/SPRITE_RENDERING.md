# Native sprite geometry, animation and placement

## Implemented scope

Three portable C++17 modules now extend the earlier `Atlas` decoder:

| Module | Native responsibility |
| --- | --- |
| `SpriteGeometry` | Convert the recovered signed-halfword local positions and texture coordinates into GPU-ready float vertices; select descriptor/image IDs for supported unit types. |
| `SpriteAnimation` | Start and advance a decoded animation in integer milliseconds, retaining the original repeat, reverse and completion behavior. |
| `SpritePlacement` | Convert unit cell/form/offset/predicate inputs into the original transform components and a column-major model matrix. |

They have no interpreter, JIT, guest address space, original executable dependency, or UIKit compatibility runtime. The original resource archive remains a separately supplied data input. The development-only original-function references are Python tools under `analysis_tools/sprite`; they are excluded from the native build and app.

This report supersedes the earlier `ATLAS_DECODER.md` milestone's unresolved sprite UV-transform and animation-unit limitations. It does not change the earlier record's scope or claim that its original checks covered these later findings.

## Source identity and evidence locations

All addresses below refer to the supplied G1 Awakening iOS 1.1.3 ARMv6 executable, SHA-256 `0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`. The references preserve its code pages read-only and verify that they remain unchanged. Address constants occur only in analysis tools and evidence, not the native implementation.

The original ARMv6 `libgcc_s.1.dylib` slice is used by the float-arithmetic references. Fat-file SHA-256: `d758fbf1ac952532955d62866bd02df55315c0599f64bdd978ff941fccbdcf83`; ARMv6 slice SHA-256: `7c737e6bc896ae963833a8c2580fb5511948f3b120d6cccf73707b23a7cdbe7d`. These files are local oracle inputs and are not included in the source repository or native product.

### Sprite vertex transform

The sprite constructor calls `0x71884` with mode 0 and float argument 2.0 at `0x73dba`–`0x73dbe`. That establishes:

```text
position = original_signed_halfword_position * 0.005
uv       = original_signed_halfword_uv * (1 / 65536) + 0.5
```

The position bias is zero. Float bit patterns are position scale `0x3ba3d70a`, UV scale `0x37800000`, and both UV biases `0x3f000000`. Original dummy-geometry construction leaves the shared scale `0xd914c` equal to 1.0. The existing texture-coordinate array path through `0x72100`/`0x72340` updates the coordinates without resetting this scale/bias. Its fallback creation branch is not the normal sprite initialization path.

The reference executes original `0x70eb4`, `0x71248`, `0x71884`, `0x72100` and `0x715d0`. It covers all observed descriptor maximum part counts: 1, 3, 4, 8, 9, 10, 11, 13, 14 and 16. M3G object ownership, GPU API calls, allocator, engine-singleton and unwind-registration boundaries are recorded substitutes; original coordinate/setup and libgcc arithmetic execute unchanged. All ten cases pass. No pixels are rendered by this reference.

Each original part is a separate four-vertex triangle strip starting at vertex zero in the part's geometry. The equivalent triangle list is `{0,1,2, 2,1,3}`. Reverse part order and all orientation branches remain those previously verified against `0x728f0`; normalization does not reinterpret their orientation bytes.

`SpriteVertex` is exactly **20 bytes**, with position at byte 0 and UV at byte 12. Metal must use a vertex descriptor with these offsets/stride, or `packed_float3` and `packed_float2` in a storage buffer. A naturally aligned Metal `float3` struct is not the same layout.

### Unit type to descriptor

The original factory `0x66d58` uses the 38-entry table at `0xcf5b0`. For types below 64 it uses the type as the index; for types 64 and above it uses `type - 41`. Native code rejects any resulting index outside the recovered table: supported types are 0–37 and 64–78.

The descriptor sequence is preserved in `SpriteGeometry.cpp`. All entries use image resource **961**, a 512×512 RGBA PNG, and resource key `0xf4e02223`. Its payload SHA-256 is `fec01805fd0e58b17085dfaccfb7c7e74591d5347cc128d420bd70622aad9979`. Type 11 selects descriptor **868**; type 19 selects **517**. Form and facing animation selection is a separate lookup, not an invented `type*2` descriptor mapping.

The seven empty descriptor framing exceptions in `ATLAS_DECODER.md` remain explicit. Their native metadata framing is verified; their original zero-rectangle loader path remains unresolved. Empty frames emit no quads.

## Animation time and behavior

### The duration words are milliseconds

The clock function at `0x1ac9c` computes elapsed `mach_absolute_time` since a stored origin, applies the timebase numerator/denominator, divides nanoseconds by float **1,000,000.0** (`0x49742400`), and converts to an unsigned integer. The core update at `0x9288` subtracts the previous sample, clamps against core field `+0x1c`, and stores the delta at `+0x20`. Constructor `0x91d4` initially sets that clamp to 1,000 ms.

The renderer owner stores this same core at `+8`: `0x6432a` calls `0x62c64`, which creates the core through `0x9208`/`0x91d4`; `0x64330` stores it. Core vtable `0xd2f38` slot `+0x24` reaches `0x63ac0`/`0x639a8`. The read at `0x63a90` therefore obtains the core's millisecond delta. The observed update chain is:

```text
0x63a90 → 0x38a24 → 0x47b2c → 0x5b152 → 0x5d5bc
         → 0x6526c / 0x65bd0 / 0x6d624 → 0x72cc0
```

The portable player accepts a caller-supplied millisecond delta; it does not impose a display refresh rate or speed up game time. The original live-unit update also has a two-times rate at `0x6ce0e`–`0x6ce14` while the sprite's current animation equals resource309's special row-eight animation and completion byte `+75` is zero. The form-change routine `0x43a54` sets unit `+2e`; renderer `0x6cdae..0x6cdca` consumes that flag and selects the special animation, choosing reverse when the new form is zero. Ordinary movement mode `+8` is not the predicate for doubling. This state belongs to the unit renderer, not the generic animation player.

### Preserved original semantics

Native start/reset follows `0x72d80`; advance follows `0x72cc0`. The implementation preserves details that would be lost by replacing it with a conventional elapsed-time animation library:

- Forward playback loads the entry step's duration once and uses that duration for every transition within that update. Reverse playback reloads the duration when changing steps.
- Reverse starts on the last step with its full duration remaining. Its transition condition is strictly less than zero.
- Negative repetition counts loop. Repetition count zero loops in the forward direction but completes in reverse. Finite completion retains the terminal frame.
- Completion stays set until reset, while later advance calls retain the original update behavior.
- The returned refresh flag compares the final step index with the entry step index. A whole cycle returning to the same index need not refresh geometry.

The player owns its step sequence. Empty lists, more than 255 steps, zero durations, durations outside the original positive signed-16-bit range, and overflowing signed-32-bit time arithmetic are rejected before invalid state mutation. Large valid deltas use arithmetic cycle skipping while retaining the original results.

The native `select_step(uint8_t)` method follows original `0x72cb4`: when the supplied step is below the sequence count, it stores that step index and preserves elapsed time, repeat count, reverse direction and completion. An out-of-range index leaves the state unchanged. The live-unit renderer calls this with zero at `0x6cea4..0x6cea8` when a current-player unit has action flag `+0x2c`, its current animation is not the row-eight special animation, and its step is nonzero. This is a pose selection, not a playback reset. Two focused contracts were added for state preservation and out-of-range rejection; all **26** current animation contracts pass. The earlier start/advance reference results remain unchanged; this small selector extension was instruction-reviewed and contract-tested rather than included in those earlier oracle counts.

The native Apple unit-render loop also preserves the recovered scheduling order in `0x6cd20`: active types are visited in ascending type order. A pending form-change flag `+0x2e` is cleared immediately at `0x6cdc6`; the row-eight sequence is requested with one repetition and reverse enabled only for new form zero. Normal selection uses `form*4+facing` with repeat count -1. A changed sequence starts without advancing during that update. While the existing special sequence is incomplete, `0x6ce0e..0x6ce12` doubles the shared loop delta; later units inherit that doubled value. The generic player does not impose these unit-state decisions. The native application integration and its remaining rendering/UI limits are recorded separately in `NATIVE_GAMEPLAY_UI.md`.

The original-function comparison covers all **170** original animations and **32** constructed sequences, four repeat-count choices in both directions: **1,616 cases, 17,776 recorded states and 124,432 scalar comparisons**, all passed. Original start and advance instructions execute unchanged. Only allocation/free, byte-buffer initialization and a geometry-refresh observer are substituted; no timing or playback-state calculation is mocked.

### First-mission idle selection

The original shared configuration object is established by `0x2f702` (allocation size 0x60), `0x2a2b0`/`0x2a27c` (twelve vectors), and the assignment at `0x2f73a`–`0x2f742` to app field `+0x3f0`. Renderer setup at `0x5cf28` through `0x2f768` binds the same object at renderer-owner `+0x7c`; `0x4ab80`–`0x4ab8a` copies it to map field `+0x294`.

`0x5d2ea` loads tagged resource `0x03000135` (309), `0x5d2fe` reads the owner `+0x7c` object, and `0x5d304` calls `0x2a714`. Its resource-309 branch at `0x2a818`–`0x2a854` invokes counted-byte decoder `0x2a3ac` and stores the vector at object `+0x20`.

Resource 309 contains a count byte 207 followed by 23 nine-byte rows; payload SHA-256 `41f078c506c57432a3a5280db79433a29c9309c6c04878da45a49104eaaec3dc`. Original lookup `0x5d17a` reads a signed byte at `type*9 + form*4 + facing`, returning zero for an index beyond the vector count. Renderer `0x6cdda..0x6cde2` passes unit byte `+4b` as this last argument. Travel helpers `0x431da` and `0x43212` prove the facing values: up 0, down 1, left 2, right 3. No separate stationary-versus-travelling selector is used by this lookup.

Spawn `0x4333a` initializes form 0, facing 1 (down) and both movement offsets to zero. Sprite creation `0x652e4` uses that lookup and starts `0x72d80` with repeats −1 and reverse false. The first mission places type 11 at (1,2) and type 19 at (4,7); both rows select animation **0** for form 0/facing 1. Both animations initially display frame 0. These choices follow the original configuration/consumer chain, not a screenshot-based guess. Future facing/form transitions must be handled by the unit renderer; the generic animation player does not choose them.

The exact type11 row is `[0,0,2,1,5,4,6,7,3]`; the type19 row is `[0,0,2,1,-1,-1,-1,-1,-1]` after signed-byte interpretation. Getter `0x5c9ca` selects row index 8, so the two initial animation-zero sprites differ from their special indices 3 and -1. Consequently the first-spawn idle delta multiplier is **1**, independently of completion byte `+75`. This proof does not assume a tick rate or infer speed from rendered frames.

## Unit placement and model matrix

`make_unit_sprite_placement` accepts tile coordinates, form, signed live movement offsets, and the two original terrain/site predicate results. It returns both the original components and `model`, a 16-float column-major matrix for column vectors.

The anchor block `0x6cf00`–`0x6cf66` and complete function `0x727a4` establish:

```text
rawX = tileX*32 + offsetX
rawZ = trunc(float(tileY*32 + offsetY + 32)
             - (form == 0 ? float_bits(0x412aaaab) : 16.0f) + 10.0f)
base = inSiteGroup ? 5 : onSite ? 20 : 0
rawY = base - 6
scale = 0.8f
adjustment = ((1.0f - scale) * 0.5f) * 32.0f
translation[i] = (float(raw[i])/scale + adjustment)/100.0f
```

The original calls identity, uniform scale, translation, and then **−45° around the X axis** before assigning the transform to the mesh. Wrapper `0x7431c` receives axis components (1,0,0). The earlier tentative Y-axis interpretation was corrected before the matrix implementation and reference were frozen.

The transform interface's dynamic factory path is engine-vtable slot `+0x10` → `0xb0414`, public type 0x1a → `0xb206c`, internal `CssTransform` type 0x1d → wrapper constructor `0xb1fde`. Its relevant operation targets are:

| Interface slot | Wrapper | Matrix operation |
| --- | --- | --- |
| `+0x1c` | `0xb1f60` | Identity via `0xa6848` / `0x94760`. |
| `+0x30` | `0xb26a8` | Scale via `0xa6a44` / `0x94f60`. |
| `+0x34` | `0xb22c0` | Translation via `0xa6766` / `0x94fd8`. |
| `+0x28` | `0xb256c` | Degree conversion, quaternion via `0x9fbd4`, rotation via `0xa69b0`. |

All three nonidentity operations use combiner `0x95a0c`. Its translation path `0x95a92` onward computes the destination basis times the incoming translation, then adds the old translation. The original matrices store translation in float indices 12–14. This establishes **postmultiplication**, hence the final column-major matrix is:

```text
M = S(0.8) * T(translation) * Rx(-45°)
```

Translation is scaled by 0.8, not rotated. Apply `model` directly to the `SpriteGeometry` vertices; adding another cell/world scale would be incorrect. The original quaternion conversion introduces a one-ULP difference between the magnitude of the sine and cosine basis entries. The fixed orientation coefficients are retained explicitly in native code; this API is a unit-placement API, not an unrestricted axis-angle transform library.

### First two models

The first mission's special site is (5,10), with group pairs (5,7) and (10,7). Original `0x4b3aa`/`0x4b256` matches site coordinates; `0x4b06e`/`0x4aed6`/`0x4ac72` searches the group pairs. Both predicates are false at the two initial unit positions. This predicate result follows direct original-branch/data evidence; the component oracle seeds those boolean outcomes rather than executing the full map predicate machinery.

| Unit type / tile | Raw anchor (X,Y,Z) | Final model translation (X,Y,Z) |
| --- | --- | --- |
| 11 / (1,2) | (32,−6,95) | (0.345600009, −0.0344000012, 0.975599945) |
| 19 / (4,7) | (128,−6,255) | (1.30560005, −0.0344000012, 2.57560015) |

The first three matrix columns are (0.8,0,0,0), (0,0.565685391,−0.565685451,0), and (0,0.565685451,0.565685391,0). The last column contains the translation and 1.

### Placement references and limits

The component oracle executes the original anchor block with explicit caller locals, then the complete placement function with recording transform APIs. **130 cases / 1,430 scalar comparisons** pass bit-for-bit, covering coordinate extremes, both forms, movement offsets and both predicate combinations.

The separate matrix reference then executes the original identity, scale, translation, quaternion, normalization and matrix-combine routines. The scalar vector-function dispatch pointers are seeded with symbol-identified original functions, which execute unchanged. **130 cases / 2,080 matrix scalar comparisons** pass bit-for-bit against the native model matrices; the component comparisons are repeated as an input consistency gate.

The matrix reference substitutes external `memcpy` and calls the host's `sinf`/`cosf` for those external libm imports. All remaining float arithmetic uses unchanged original ARMv6 libgcc instructions. Therefore this establishes the transform order, convention, native/core arithmetic agreement and documented float coefficients with those libm boundaries. It does **not** establish historical iOS libm bit identity or GPU pixel fidelity.

## Validation and reproducibility

| Local check | Result |
| --- | --- |
| SpriteGeometry contracts | 21 passed. |
| SpriteAnimation contracts | 26 passed. |
| SpritePlacement contracts | 28 passed. |
| Original render-setup calls | 10 cases passed. |
| Original animation states | 1,616 cases / 17,776 states / 124,432 scalar comparisons passed. |
| Original placement components | 130 cases / 1,430 scalar comparisons passed. |
| Original matrix core | 130 cases / 2,080 matrix comparisons passed. |

Standalone contract compilation used GCC 13.3.0, C++17, `-O2 -Wall -Wextra -Wconversion -Werror`. The root project integrates these modules and no-argument tests in CMake. The development-only reproduction commands and required local inputs are in `analysis_tools/sprite/README.md`. Machine-readable outputs are the four `research/sprite_validation/original_*.json` records, with code and input hashes; `SPRITE_RENDERING.json` freezes source hashes and summary results.

These are native CPU component and original-function checks. This report does not claim a Metal render, a full native campaign, original camera behavior, combat, persistence, audio, sustained 60 FPS, or an iPhone XR/iOS 17.5 test. The original executable, libraries, external oracle engine and screenshots are not used by the native product. Camera/viewport hierarchy and full game-state execution remain separately assessed work.
