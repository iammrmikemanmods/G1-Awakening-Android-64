# Native rigid M3G Metal path

`platform/apple/MetalM3GRenderer.hpp/.mm` and `MetalM3GShader.hpp` implement a dedicated rigid, unlit rendering path for decoded original M3G assets. This source checkpoint has **not yet compiled on Apple, executed on a GPU or produced a device frame**. It is separate from the captured campaign UI 0.4.0/build 4; no battle-completion acknowledgement is connected to it.

## Actual model requirements

`m3g_validation/bumblebee_render_states.json` independently reads the supplied resource 867 model with `analysis_tools/m3g/reference.py`. Root user ID 838542512 resolves to object 221. Its 34 rigid surfaces use three appearance objects:

| State | Actual requirement |
| --- | --- |
| Lighting and fog | No Material or Fog object |
| Layer | 0 for every appearance |
| Compositing | REPLACE; color/alpha/depth writes and depth testing enabled |
| Polygon | Back-face culling, counterclockwise front, smooth shading |
| Alpha test / depth offset | Threshold 0, both depth-offset terms 0 |
| Textures | RGB images 40 and 54, 256×256 and256×128 |
| Texture environment | REPLACE, repeat on both axes, base level, nearest image filtering |

The untextured appearance remains untextured and uses the decoded vertex/default color. The renderer does not substitute a sprite or generated model. The rigid decoder's earlier validation accounts for 6,960 real expanded triangle vertices and exact image bytes. An independent state audit finds no required rejected path for this subtree; that is an input-coverage result, not successful rendering.

## Host API and state

`upload(M3GStaticModel)` validates the complete required path before allocating GPU resources. It retains immutable vertex buffers, images and appearance state. `encode(encoder, gpu_model, view_projection, pose_matrices)` records into the caller's existing encoder. A supplied node-matrix map must contain every mesh; omitting it draws the authored pose. All pose matrices and camera products are checked before the first draw. The caller retains ownership of the camera, viewport, render pass and actual presentation acknowledgement. The pass must be single-sample and match the renderer's selected normalized color and Depth32Float formats.

The 64-byte vertex layout carries original position, normal, color and homogeneous texture coordinates. Texture transforms have already been applied by `M3GScene`; the shader performs the projective S/Q, T/Q division once. There is no implicit axis flip, model scale or camera conversion. The view-projection matrix must use Metal's 0..1 clip-depth convention.

The shader applies effective node alpha to incoming vertex alpha before texturing. This matters because an RGBA REPLACE texture can replace that alpha. Texture REPLACE, MODULATE, DECAL, BLEND and ADD follow their source-format-specific definitions. Undefined DECAL formats fail on upload. The alpha comparison retains equality. Flat shading reads the actual third expanded triangle vertex, while smooth shading interpolates colors. Perspective-correct interpolation is the native policy; the original hint does not require a specific interpolation implementation.

Pipeline state expresses all five decoded compositing equations, including their alpha-channel factors and independent RGB/alpha write masks. Depth testing uses LEQUAL; disabling the test while retaining depth writes uses an always-pass comparison. Culling and winding use the decoded enumeration values. Surfaces are stable-sorted by appearance layer; equal-layer order preserves decoded traversal. No original battle transparent-surface sorter is claimed.

Lighting, nonzero polygon depth offset and mipmapped levels are rejected explicitly. The upstream decoder continues to reject required fog, nonrigid meshes and multiple texture units. These limits are sufficient for the currently audited Bumblebee subtree but are not a full JSR184 engine. This class expects serialized calls; its state caches are not shared-thread synchronization primitives.

## Apple build gate

Add `platform/apple/MetalM3GRenderer.mm` to the Apple app source list under the existing ARC/Metal settings. It requires no additional framework beyond Metal. The included shader is the single runtime source; an extractor writes those exact bytes for a separate compiler gate:

```sh
python3 analysis_tools/m3g/extract_metal_shader.py \
  --header native/platform/apple/MetalM3GShader.hpp \
  --output /tmp/g1-m3g-shader.metal
xcrun --sdk iphoneos metal -std=ios-metal2.4 \
  -c /tmp/g1-m3g-shader.metal -o /tmp/g1-m3g-shader.air
```

The extraction ran locally. The Apple compiler command remains pending until the hosted Apple job actually runs. A successful compiler gate would establish source/shader compilation, not device rendering, culling correctness, visual parity or speed.

Before gameplay integration, the original type/form model lookup, root selection, action intervals/controller changes, camera choreography and battle-event boundaries still need concrete binding. Sampling a complete authored timeline cannot substitute for those requirements. The current native battle panel remains the only connected battle presentation.

## Primary references

- Author-issued Nokia M3G 1.1 [Texture2D contract](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/Texture2D.html), including format-specific texture environments.
- [CompositingMode](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/CompositingMode.html), alpha/depth comparisons and blending equations.
- [PolygonMode](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/PolygonMode.html) and [Node](https://wupp.cc/j2meapi/JSR_184_Mobile_3D_Graphics_API_1_1/javax/microedition/m3g/Node.html), shading/winding and inherited alpha.
- Apple [render pipeline color attachment](https://developer.apple.com/documentation/metal/mtlrenderpipelinecolorattachmentdescriptor?language=objc) and [Metal render command guide](https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Render-Ctx/Render-Ctx.html), native pipeline and encoder mechanisms.
