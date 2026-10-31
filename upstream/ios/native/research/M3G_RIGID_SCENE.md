# Native rigid M3G scene decoder

## Verified checkpoint

`M3GScene` decodes the original JSR184 object bodies into native C++ scene data and a 64-byte vertex layout suitable for a GPU upload. It does not execute an original game instruction or require an original executable at runtime. The existing `M3G` module still owns container framing and section checksums.

The supplied Bumblebee asset (archive member 336, resource 867, key `0xf4e02223`, SHA-256 `aa08afabedbe636d7cedaec480070d3d051b5d2c950c5e40191d8c239f9b48a1`) has 233 objects. Selecting the authored `robotTransform` user ID 838542512 selects object 221 and retains its actual ancestor transforms. Its selected subtree contains 22 meshes, 34 drawable surfaces, 6,960 expanded vertices and two decoded images. This is the asset's **serialized pose**, not a claim that the original game selects this pose as its initial idle state.

An independent Python body decoder and geometry builder compared all 445,440 native vertex bytes and both RGBA images exactly. A separate double-precision Rodrigues/TRSM hierarchy calculation compared 1,424 matrix scalars with maximum absolute error `8.213497313391827e-7` (tolerance `2e-5`). This is an independent data/mathematics reference, not original-engine bit-identity. Detailed per-surface and image hashes are in `m3g_validation/bumblebee_rigid.json`.

The focused native test executable passes 47 contracts. These cover strip expansion and alternating winding, signed 8/16-bit delta overflow, position scale/bias, unsigned RGB colors, normal ranges, default alpha, ancestor matrices and alpha, row-major serialization, disabled nodes and null appearances, all five image channel formats in paletted/direct form, reference/index bounds and malformed inputs.

## Format and rendering contract

The implementation follows Nokia's JSR184 1.1 file-format and API specification, dated June 22, 2005. The [JCP maintenance release page](https://jcp.org/aboutJava/communityprocess/mrel/jsr184/index.html) identifies the release; the original specification is available in this [preserved documentation copy](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/file-format.html). Class contracts for [Transformable](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/Transformable.html), [Node](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/Node.html), [VertexBuffer](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/VertexBuffer.html), [TriangleStripArray](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/TriangleStripArray.html), [Image2D](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/Image2D.html) and [Texture2D](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/Texture2D.html) supply the specific semantics. The preserved pages are the original specification, not a third-party interpretation; they are not redistributed in the native source tree.

The native contract keeps the following details explicit:

- Matrices use column vectors and column-major storage. Serialized general matrices are row-major and are transposed once. Component composition is `T * R * S * M`.
- Vertex arrays preserve signed 8/16-bit values and wrapping delta accumulation. Position and texture scale/bias are applied from the actual vertex buffer. Texture coordinates remain homogeneous; texture transforms are retained.
- Triangle strips expand with alternating winding. Legal degenerate triangles are retained. No coordinate-axis, winding or UV inversion is introduced.
- Node render enable and alpha compose through ancestors. Scope is not inherited. Zero alpha alone does not suppress geometry because the actual texture/blending state can still render it.
- A null appearance disables that submesh. Appearance layer, culling, winding, shading, depth/color/alpha writes, blending, alpha threshold, depth offsets, material and one texture unit are decoded into explicit native fields.
- Image formats ALPHA, LUMINANCE, LUMINANCE_ALPHA, RGB and RGBA expand from direct or paletted bytes. Mutable images have no supplied pixel data and fail when a rendered surface requires them.
- The selected Bumblebee surfaces use unlit vertex/default color and textures with original `REPLACE`, `REPEAT`, `BASE_LEVEL`, `NEAREST` state. Both image origins and the serialized texture coordinates remain unchanged.

## Public API and build handoff

`include/g1/M3GScene.hpp` exposes `M3GScene`, `M3GObjectInfo`, `M3GStaticModel`, `M3GSurface`, `M3GAppearanceState`, and `M3GVertex`. `static_model(root)` returns local expanded vertex data, per-surface model matrices and decoded images. A root of zero selects the unique serialized World; a selected subtree retains its authored ancestors. `find_user_id` does not invent game-specific object names or form mappings.

Add `src/M3GScene.cpp` to `g1_resources`. Link `tools/m3g_scene_probe.cpp` and `tests/m3g_scene_test.cpp` against that library; the test takes no private fixture. The probe takes `MODEL.m3g [--user-id ID] [--export DIRECTORY]`. Its optional export contains copyrighted source-derived geometry/images and is a local development artifact, not a repository payload.

Reproduce the independent asset comparison with:

```sh
python analysis_tools/m3g/validate.py \
  --model /path/to/0336_g0363_000_bumblebee_tk.m3g \
  --native-probe /path/to/m3g_scene_probe \
  --user-id 838542512 \
  --private-export /private/output/bumblebee \
  --output native/research/m3g_validation/bumblebee_rigid.json
```

The Python tools use the standard library. The C++ module uses the existing archive/M3G code and zlib; no CPU emulator, external interpreter, proprietary asset or downloaded SDK is added by this checkpoint.

## Exact remaining boundary

This checkpoint has **no Metal M3G draw, device capture, evaluated animation or battle cinematic**. Track/controller/keyframe framing is decoded, but tracks are not evaluated. Runtime alignment is not applied. Morphing meshes, skinned meshes, Sprite3D nodes, fog and multiple texture units fail explicitly when their unsupported rendering is required. Material state is retained but no new GPU lighting implementation is provided.

The next rigid rendering step needs a separate Metal path carrying per-vertex color, appearance state and homogeneous texture coordinates; the tactical sprite pipeline currently carries only position and UV. Before presenting a battle pose as original gameplay, the game-selected object/form, controller time and animation/choreography must also be recovered. Existing native battle UI remains an explicitly bounded health/outcome presentation. The selected subtree data is not used as a substitute for that missing behavior.
