# Native M3G scene assets, common draw queue and Metal adapter

This checkpoint connects the recovered native M3G components into a CPU frame
planner and a Metal encoder. It retains actual mesh and Sprite3D resources,
evaluates current node state and uses one shared draw queue. It does not select
the game's battle phase, camera, form or attachment order on behalf of the
caller. The current app integration and completed gameplay presentation tokens
are unchanged by this isolated patch.

## Render assets retain the data needed by animation

`M3GScene::render_asset(root)` decodes the selected real hierarchy into
`M3GRenderAsset`. Its rigid geometry remains in `geometry.surfaces`; images for
both draw types live once in `geometry.images`. Each Sprite3D retains its own
node, image and Appearance references, scaled flag, signed crop, image format,
scope, original transform and inherited alpha. The mixed `traversal` vector
records child encounter order and mesh submesh order before sorting.

Unlike a serialized static snapshot, this immutable asset retains disabled and
zero-scope geometry. A later visibility sample can reveal a node; discarding
that geometry at load time would make that animation impossible. Current
`M3GScenePose` state determines enable, alpha and scope admission at frame time.
Malformed required geometry fails rather than being replaced. Runtime alignment,
skinned/morphed geometry and unsupported required drawable state remain explicit
errors.

`static_model()` retains its existing static-only behavior and explicit
Sprite3D rejection. Both geometry paths now traverse ancestors and children
iteratively, avoiding an unbounded native recursion depth on a long valid
backward-reference hierarchy.

The supported Sprite3D data slice matches the required battle files: immutable
power-of-two images up to 1024 pixels per axis and the recovered unlit
Appearance path. Non-power-of-two padding, multiblock textures, runtime crop
animation and additional Sprite3D Appearance features are not silently guessed.

## Exact opacity classification and Appearance ordering

`m3g_mesh_queue_transparent` implements the original mesh predicate established
by `M3G_MESH_CLASSIFICATION.md`:

- REPLACE 68 is opaque without the subsequent alpha checks.
- Other modes except ALPHA 64 are transparent.
- ALPHA is transparent when inherited alpha is below one, an existing material
  diffuse alpha is not full, a four-component vertex-color array exists, an
  absent color array exposes a non-full default alpha, or either texture slot
  uses an alpha-channel image format 96, 98 or 100.

The predicate uses component/image metadata, not pixel scans or a guessed
texture-function optimization. The native Scene already resolves an absent
CompositingMode to its original REPLACE default. The standalone predicate accepts
finite effective-alpha diagnostics outside `[0,1]` as the original comparison
does; actual prepared frames require valid normalized inherited node alpha.

Each frame instance supplies explicit opaque grouping keys. Mesh keys represent
Appearance creation order; opaque Sprite3D keys represent Appearance identity.
Transparent entries use zero. No serialized object ID is silently substituted
for an original runtime pointer or serial.

`m3g_mesh_appearance_order_keys(scene)` provides canonical per-file ranks for
the source-confirmed ordinary load/shared-Appearance path. The original loader
constructs Appearances in record order and Mesh/Group cloning shares them.
Because the common queue compares grouping keys only within the same node and
queue key, these ranks preserve equality and ordering for that bounded path.
Distinct records remain distinct even when their bodies are identical.

The proof and limits are in `M3G_APPEARANCE_ORDER.md`. Ranks are not claimed as
absolute historical serial values. The supported order-equivalence policy
excludes a signed serial transition through `0x7fffffff → 0x80000000`, counter
reuse, and independently replaced/cloned/cross-file Appearances. Such inputs
need their actual explicit identity/order metadata. Two actual resources are
checked against original creation observations: Bumblebee 867 uses Appearance
records `[21,34,42,56]`; Sunstreaker 829 uses `[34,35,43,68]`.

## One validated CPU frame

`prepare_m3g_frame(instances, view)` takes:

- Immutable assets and current evaluated node maps.
- Unique instance namespaces, explicit parent transforms/alpha/enable and the
  actual attachment encounter order.
- An explicit decoded Camera projection, evaluated camera-to-world transform,
  viewport and drawable scope mask.
- Explicit grouping maps for required opaque entries.

It checks enable and each drawable's own scope, computes its current world
matrix and inherited alpha, then sends meshes and sprites through the recovered
common queue. Distance is the original quantized squared world distance from
the camera. Same-node opaque submesh anchors and equal-key ordering use
`m3g_draw_queue_order`; meshes and sprites are never separate last/first batches.

The Sprite3D helper supplies original NDC origin plus fixed16 offsets and UVs.
The planner converts the original four-vertex strip into triangles
`[0,1,2,2,1,3]`, adds the origin once and applies Metal depth `z=(z+w)/2` once.
Every corner has the original projected-origin depth and `w=1`. Its Metal matrix
is identity. No extra scale, Y flip, half-texel shift or camera substitution is
applied. A valid but rejected Sprite3D remains a queue record with its explicit
projection result and emits no geometry.

The plan retains source-asset identities so an encoder can reject accidentally
pairing a prepared frame with unrelated uploaded buffers. Preparation validates
the complete CPU frame before any GPU command is recorded. Frustum/GPU
rasterization parity and the game's attachment/camera schedule are separate
integration boundaries; this planner does not infer them from asset presence.

Render-pass clearing is also explicit caller work. All 27 inspected files have
a serialized World Background with a solid RGBA color, no image and both clear
flags enabled. Those authoring colors are retained in the independent data
inventory, not selected as an actual battle backdrop by this planner. The
original game extracts a child Group from a loaded container and attaches it to
the cinematic Group; that does not establish that each source file's World
Background is rendered. `render_asset` is a drawable-subtree API, not a complete
World render-pass command. The host must supply the recovered cinematic
clear/background policy before a battle output can be called faithful.

## Metal integration

`MetalM3GRenderer::upload(M3GRenderAsset)` retains immutable source metadata and
uploads real images and rigid vertex data. `MetalM3GModel::asset()` exposes that
retained source for constructing frame instances. `encode_frame(models, frame)`
validates source identities, object/index mappings, matrices, alpha and vertex
payloads for every record, then emits the single prepared draw stream.

Mesh state uses the existing native unlit M3G pipeline. Sprite3D uses its
recovered compositing and image state, disables face culling, and submits a
384-byte immediate six-vertex payload through Metal's copied vertex-bytes
binding. The original 112-byte uniform layout is unchanged. The shader source
is unchanged and still has one shared source for runtime and cloud compilation.

Sprite sampling is base-level NEAREST with CLAMP_TO_EDGE. Required RGBA images
use REPLACE at full inherited alpha and MODULATE below one; original RGB also
uses REPLACE. These choices are grounded in the callsites documented in
`m3g_validation/NATIVE_SPRITE3D.md`. Alpha testing retains equality at the original
threshold. The render pass remains single-sample with the caller's declared
color format and Depth32Float depth target.

Depth testing and writing follow the original shared CompositingMode helper,
not a generic assumption about disabling OpenGL depth testing. At `0xb5224`,
the helper `0xb5120` calls `glDepthFunc` with LEQUAL or ALWAYS according to the
mode's test flag; at `0xb522c` it independently calls `glDepthMask` with the write
flag. Both mesh and Sprite3D call this helper. Metal therefore uses LEQUAL or
ALWAYS plus the independent depth-write flag. This adapter represents the
original **globally depth-enabled** renderer capability. The separate global
capability switch at `0xb57f6..0xb580a`, render-pass clearing and target creation
are host policies, not silently recovered from one Appearance.

The existing rigid `upload/static encode` path retains its stable layer order.
An asset uploaded for stateful rendering is rejected by that older encoder, so
it cannot silently omit sprites or ignore hidden/current state. It must use
`encode_frame`.

## Validation and precise evidence boundaries

| Gate | Observed result |
| --- | ---: |
| Original enqueued mesh-classification cases | 51 cases / 153 exact field comparisons |
| Focused native frame contracts | 167 checks passed |
| New render-asset contracts | 14 passed |
| Existing Scene contracts plus hidden/zero-scope retention | 50 passed |
| Independent actual-file structure/metadata/traversal comparison | 19,930 checks, zero differences across 27 files |
| Retained actual rigid inventory | 783 Mesh nodes / 962 non-null submeshes / 305,748 expanded vertices |
| Actual Sprite3D inventory | 54 records, all compared with independent decoding and original authored metadata |
| Mixed traversal/image-reference inventory | 1,016 draw entries / 53 image references |
| Connected CPU frame preparation | 60 frames from five resources |
| Prepared mesh records / projected Sprite3D draws / culled sprites | 2,256 / 118 / 530 |
| Original Appearance index/rank comparisons | 32 exact |

The six original traversal cases that never enqueued a mesh are not mislabeled
as standalone predicate cases. Enable/scope and missing-required-data behavior
are covered by native planner/decoder contracts. The 51 numeric fixture cases
preserve the original supplied serial, including its non-default serial vector;
an absent CompositingMode is resolved before the predicate as it is in Scene.

The actual-file audit is frozen separately in
`m3g_validation/RENDER_ASSET_DECODING.md` and `render_asset_decoding.json`.
Its independent decoder observes no hidden/zero-scope/zero-alpha mesh in this
actual corpus; the synthetic contracts establish retention for those inputs.
The audit compares metadata and traversal, not rendered pixels or decoded image
byte equality through this particular probe.

The separate `render_pass_inventory.json` records all 27 serialized World
Backgrounds and CompositingModes. No image-backed Background or combination of
disabled depth testing with enabled depth writing occurs in these files. This
is a serialized-data observation, not an observation of the game's pass setup.

The 60 prepared frames use resources 486, 534, 1212, 867 and 829, each at six
explicit times with its first and last serialized Camera as explicit seeds.
They exercise the connected sampler, persistent node/camera pose, source assets,
opacity classification, queue and projected quad path. These are neither
observed original battle frames nor proof that those cameras are chosen by the
game at those times. The full current serialized hierarchy includes both forms
until the caller applies the real form state. The selected 480×320 viewport is
an explicit probe input, not an XR performance measurement.

Native builds use C++17 with `-O2 -Wall -Wextra -Wconversion -Werror`. The new
Sprite3D module separately passed its 1,557 original numeric comparisons and
30 contracts under optimized and undefined-behavior-sanitized builds. A bounded
read-only peer review found no concrete supported-path frame/Metal blocker;
that review did not run an Apple compiler or GPU.

**Apple compilation, actual Metal output, battle-controller integration,
physical XR frame pacing/audio and a full playthrough are not validated by this
checkpoint.** No presentation completion is synthesized by the frame planner.

## Integration and reproduction

Apply sampler v2, scene-pose v3, the frozen new Sprite3D files, then this v4
patch. Rebuild all consumers of `M3GScene.hpp`: per-surface metadata changed.
Add `src/M3GRenderFrame.cpp` to the native core. Existing `M3GScene.cpp` remains
the decoder source; the isolated patch updates it and the existing Scene test.

New host tests are `tests/m3g_render_asset_test.cpp` and
`tests/m3g_render_frame_test.cpp`, with no arguments. Optional tools are
`tools/m3g_render_asset_probe.cpp` and `tools/m3g_render_frame_probe.cpp`.
The Apple adapter updates the existing `platform/apple/MetalM3GRenderer` pair;
it requires the same Metal framework and ARC configuration as before.

```sh
python3 analysis_tools/m3g/mesh_queue_fixture.py \
  --input native/research/m3g_validation/original_mesh_classification.json \
  --output native/tests/m3g_mesh_queue_fixture.hpp

python3 analysis_tools/m3g/render_frame_validate.py \
  --manifest /private/asset_manifest.json \
  --extracted /private/extracted \
  --probe /build/g1_m3g_render_frame_probe \
  --appearance-reference native/research/m3g_validation/original_appearance_order.json \
  --resource 486 --resource 534 --resource 1212 --resource 867 --resource 829 \
  --output /private/render-frame-actual.json

python3 analysis_tools/m3g/render_pass_inventory.py \
  --manifest /private/asset_manifest.json \
  --extracted /private/extracted \
  --output /private/render-pass-inventory.json
```

The development references, recorders and private original assets remain
outside the native product. No ARM interpreter, JIT, original executable or
compatibility runtime is required by this renderer.
