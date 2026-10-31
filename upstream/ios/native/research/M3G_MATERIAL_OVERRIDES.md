# Original battle-model texture replacements

The native helper now resolves the original external image replacements for
effective model types **3, 6, 7, 13, 14, 15 and 20**. It returns ordered updates
to the actual **Texture2D object identity**, and decodes each required PNG as
the original loader's **Image2D RGB format 99**. It does not replace an
Appearance, duplicate a texture per surface, or substitute new artwork.

This is a native C++17 data/binding module. Original ARM instructions run only
in the separate development recorders; no guest CPU or original executable is
linked into the product. No Apple compilation or GPU/device result is claimed
by this report.

## Recovered bindings

The two signed table entries at `0xceb14 + effective_type * 8` contain tagged
image resources. Negative entries skip that slot. The table is read directly
from the pinned original image by the recorder. All other effective types in
the 24-entry table have no replacement.

| Effective type | Model resource | `material`: Appearance → Texture → image resource | `material2`: Appearance → Texture → image resource |
|---:|---:|---|---|
| 3 | 492 | 22 → 21 → 686 | 98 → 97 → 651 |
| 6 | 369 | 12 → 11 → 1216 | Absent |
| 7 | 565 | 12 → 11 → 1255 | Absent |
| 13 | 631 | 22 → 21 → 572 | 98 → 97 → 704 |
| 14 | 795 | 22 → 21 → 595 | 98 → 97 → 790 |
| 15 | 967 | 58 → 57 → 1270 | 14 → 13 → 1244 |
| 20 | 1070 | 12 → 11 → 796 | Absent |

Appearance and Texture numbers are serialized object identities in their
respective model files. Replacement numbers are archive resource IDs. They
are different namespaces. Names are hashed **without** the `Transform`
suffix: raw `material` gives 500033589 and raw `material2` gives 253263228.
The original caller uses `0x26b94` at `0x3e7fe` and its second-branch equivalent.

The caller receives an already effective type. Requested type17's conditional
remap to type23 belongs to the separately recovered
[model attachment](M3G_MODEL_ATTACHMENT.md); this helper neither repeats that
remap nor chooses a fallback model.

## Exact caller behavior and bounded implementation

`M3GMaterialBindings::plan(selected_group, effective_type)` implements the two
material stages of original `0x3e416`, from `0x3e7e6` through the instruction
before `0x3f560`. The selected Group is the actual model child established by
the loader/attachment path. A serialized World also supports the recovered
Group interface, but a World is not implicitly substituted for the selected
model root.

For each present slot, the caller finds the raw name, queries an Appearance,
loads the specified PNG and obtains texture unit 0. A named Appearance with a
non-null texture0 supplies the target. If that route is absent, wrong-type, or
has no texture0, the original fallback:

1. Visits the selected root's direct children in order.
2. Skips a child that does not provide Group, and skips an empty direct Group.
3. Follows **child0** repeatedly through nested Groups.
4. Accepts a Mesh at the first non-Group child; otherwise tries the next direct
   root child. It does not search child1 for a more convenient Mesh.
5. Requires the selected Mesh's appearance0 and obtains its texture0.

A nested empty Group and a missing required Mesh/appearance0 fail explicitly.
If an existing fallback Appearance has no texture0, the original has loaded
the PNG but makes no SetImage call. The native plan retains that ordered step
with `texture_object == nullopt`. It does not invent a texture. Applying
`material` followed by `material2` preserves the original last-write behavior
when both steps select the same Texture2D.

The reference graph constructor validates object identities, typed edges,
single-parent Node children and cycles with explicit stacks. Byte decoding
uses the existing strict M3G body decoder first, then retains the references
needed for identity lookup. The 27 audited files contain 7,404 objects and
match the independent reader's eight identity fields for every object.

The native name search accepts at most one matching reachable authored ID.
It deliberately rejects ambiguous IDs instead of extending the Node-only
lookup helper or guessing arbitrary Object3D reference order. All required
names meet this condition. Nonrigid objects and non-null alignment references
remain explicit unsupported inputs to this bounded helper. Failed requests
raise `FormatError`; partial original error-path side effects are not replayed.

### Independent original lookup check

The caller recorder has explicit public find/getter boundaries. To check that
the expected named Appearance is actually reachable through the original
engine, a separate recorder executes the unchanged `0x99c90` search with the
original internal reference enumerators:

| Reference owner | Original enumerator |
|---|---|
| Group | `0x90108` |
| Mesh | `0x97db4` |
| Appearance | `0x89ca4` |
| Texture2D | `0xa6500` |
| VertexBuffer | `0xa9efc` |
| AnimationTrack | `0x89420` |
| Object3D animation references | `0x99e1e` |

It seeds the authored object IDs, linked children, Mesh arrays and other
references from the independent file reader. **All 14 searches across the
seven models return the expected results, including the 11 required named
Appearances.** Find and reference enumeration have no host substitutes in
this run. It is still a seeded internal graph, not execution of the complete
original M3G object loader. The fixture preserves that distinction.

## Image format and pixel decoding

The original resource loader's PNG path reaches `0x7ea68` through
`0x26c2a` → Loader → serializer image fallback `0xa0634`. The reference run
executes the complete `0x7ea68` PNG routine, its original libpng/zlib bodies,
and unchanged ARMv6 libgcc arithmetic. Image factory/initialization calls are
recorded at the public boundary. Allocation, bounded libc operations,
nonthrowing setjmp and ownership are the stated substitutes.

All eleven images initialize **format 99 (RGB)**. Palette PNGs supply RGB
palette entries plus indices; raw RGB PNGs supply three bytes per pixel. The
native LodePNG decoder expands them to straight RGBA8 with alpha255 and no
display/gamma conversion. Every native RGBA SHA-256 matches the corresponding
original decoded and expanded content: **2,621,440 decoded bytes** across the
eleven resources. Loading by archive ID/key produces exactly the same output
as decoding the independently extracted member.

| Image resources | Dimensions | Authored PNG encoding |
|---|---|---|
| 686, 572, 595 | 256 × 128 | 8-bit indexed RGB |
| 651, 1216, 1255, 704, 796 | 256 × 256 | 8-bit indexed RGB |
| 790 | 256 × 256 | 8-bit RGB |
| 1270 | 256 × 128 | 8-bit RGB |
| 1244 | 512 × 256 | 8-bit RGB |

All are noninterlaced and have no `tRNS` transparency. The bounded native
loader requires the original resource's dimensions and encoding, validates
PNG framing/CRC and rejects acquired transparency. These are explicit input
contracts, not a claim that RGB is correct for arbitrary PNGs or replacement
resource IDs. Resource content is paired with the source manifest in the
development validation; the product helper does not hardcode payload hashes.

### SetImage changes the image reference

The Texture2D public constructor `0xb1cac` installs slot `+0x88` as
`0xb1e3e`. That wrapper unwraps the Texture/Image and calls `0xa62e4`.
The core retains the new image, releases the previous reference, writes the
Texture's internal `+0x30` image pointer and validates power-of-two dimensions
in the inclusive range 1..1024. The caller recorder executes these original
instructions and verifies that no other byte of the seeded Texture object
changes. The eleven source dimensions satisfy that original check.

Existing Texture2D function, wrap, filter, blend-color and transform state
therefore remains attached to the texture identity. Ordinary Mesh/Group clones
retain Appearance references as separately established in
[Appearance order](M3G_APPEARANCE_ORDER.md).

## Renderer integration contract

The public API is in `include/g1/M3GMaterialOverrides.hpp`; implementation is
`src/M3GMaterialOverrides.cpp`. The renderer adapter should:

1. Construct `M3GMaterialBindings` from the exact model data and request a plan
   for the already selected Group/effective type.
2. Load each step's PNG through `load_m3g_material_image(archive, resource_id)`.
3. Apply steps in order to **every** surface whose
   `appearance.texture->texture_object` equals the target. A null target is
   the explicit load-only case.
4. Give the decoded replacement a distinct host image identity, update that
   texture's image reference **and `image_format = 99`**, then classify queues
   and upload the resulting images. Do not change another Texture2D merely
   because it originally shared the same Image2D.
5. Preserve all other Appearance/Texture state and the original selected
   model attachment. The plan does not create an Appearance or select a camera.

`authored_image_object` identifies the original file reference for diagnostics;
it does not describe an image already replaced by an earlier plan step.
Host synthetic image keys are only adapter resource identities, not newly
claimed original serialized object indices. No GPU sampler or render-pass
policy is introduced by this module.

## Validation and evidence files

| Check | Result | Evidence |
|---|---:|---|
| Original caller/control-flow cases | 40 synthetic cases plus seven authored models / 11 assignments | `m3g_validation/original_material_overrides.json` |
| Original name lookup | 14 searches / 11 required matches | `m3g_validation/original_material_find.json` |
| Complete original PNG decode | 11 successful returns, original code unchanged | `m3g_validation/original_material_png.json` |
| Native source-data parity | 27 files, 7,404 identity objects, 11 assignments/images, 59,424 comparisons | `m3g_validation/material_overrides.json` |
| Native original-vector tests | 40 cases / 272 checks | `tests/m3g_material_cases.txt` and focused test |
| Native additional contracts | 37 checks, including a 20,000-Group chain | `m3g_validation/material_overrides_contracts.json` |
| Optimized / UBSan test executions | Both pass all 309 checks | Same contract report |

The synthetic fixture contains no original game payload. Additional contracts
cover ambiguous IDs, cycles, malformed typed references, invalid table/root
inputs, PNG dimensions/encoding/transparency/CRC/truncation, and palette/raw
RGB expansion and row order. The bounded independent source review found no
concrete integration or format blocker; it does not substitute for execution.

All original-code evidence pins executable SHA-256
`0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd` and
libgcc SHA-256
`d758fbf1ac952532955d62866bd02df55315c0599f64bdd978ff941fccbdcf83`.
Each recorder verifies unchanged read-only original code and records its
own source hash, dependency hashes and explicit substitutions. Native input
parity is tied to the asset manifest and archive hashes. Final owned file
hashes are listed in `m3g_validation/M3G_MATERIAL_OVERRIDES_FREEZE.json`.

## Reproduction

From the checkpoint's `g1_port` directory, the focused source-only test needs
C++17, zlib and the already bundled PNG decoder:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -I native/include -I native/third_party/lodepng \
  native/tests/m3g_material_overrides_test.cpp \
  native/src/M3GMaterialOverrides.cpp native/src/M3GScene.cpp \
  native/src/M3G.cpp native/src/Archive.cpp native/src/Image.cpp \
  native/third_party/lodepng/lodepng.cpp -lz -o /tmp/g1_material_test
/tmp/g1_material_test native/tests/m3g_material_cases.txt
```

The probe uses the same source list, replacing the test with
`native/tools/m3g_material_probe.cpp`. Its modes are:

```sh
m3g_material_probe model.m3g SELECTED_GROUP EFFECTIVE_TYPE
m3g_material_probe --image RESOURCE_ID image.png
m3g_material_probe --archive-image RESOURCE_ID iphone.big
```

Image modes emit one JSON header line followed by raw RGBA bytes to standard
output; they are intended for bounded machine validation. Do not display the
binary output as terminal text.

The development-only scripts are under `analysis_tools/m3g/`:

- `material_override_oracle.py`: both original caller stages and actual SetImage.
- `material_find_oracle.py`: separate original reference traversal.
- `material_png_oracle.py`: complete original PNG decoding.
- `material_override_validate.py`: native metadata/plan/pixel/archive parity and
  deterministic generation of the synthetic C++ fixture.

Their `--help` options require explicit local original image/libgcc/manifest/
extraction inputs and a Unicorn path where applicable. The executed commands,
compiler flags and hashes are retained in the contract/freeze records. These
tools are not dependencies of the native app or the source-only fixture
test.

## Remaining scope

This closes the required external image dependency for the seven affected
ordinary model types. It does not establish the whole battle controller,
effects, model cache scheduling, full object loading, lighting/render passes,
GPU output, target-device performance, or campaign completion. Those remain
separate integration and acceptance work.
