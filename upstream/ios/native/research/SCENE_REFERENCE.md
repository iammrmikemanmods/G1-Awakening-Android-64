# Native first-scene CPU reference

This check executed the new native C++ `FirstMissionScene`, `initial_presentations`, and sprite-animation APIs, exported their geometry and decoded textures, then rasterized the result on the CPU. **The images are CPU references, not native Metal screenshots or iPhone captures.** No original executable, guest CPU, or compatibility runtime was used.

## Finding

The first mission's recovered terrain and both initial robot sprites are visible with the inspection camera at 896 × 414 and 1792 × 828. All exported vertices lie inside the clip volume. Visual review found upright, coherently assembled yellow and purple robots, with no obvious texture inversion or model-axis mismatch. The terrain's road, bridge, shore, and water imagery is coherent. The sprite-recovery owner independently inspected the 896 × 414 reference and agreed with that limited visual finding.

| Exported content | Count or identity |
|---|---:|
| Mission / grid / style | 315 / 268 / 0 |
| Grid | 15 × 14 |
| Native campaign cursor | 9, before the first dialogue |
| Terrain | 255 quads, 510 triangles, 1,530 vertices |
| Terrain texture | Resource 693, 512 × 256 |
| First unit | Type 11 at (1, 2), descriptor 868 |
| Second unit | Type 19 at (4, 7), descriptor 517 |
| Sprite texture | Resource 961, 512 × 512 |
| Initial animation / frame | 0 / 0 for each unit |
| Each sprite | 16 triangles, 48 vertices |

At 1792 × 828, the yellow unit is the last contributing draw on **1,706 pixels**, with visible bounds `[501,186,550,239)`. The purple unit contributes **1,634 pixels**, with bounds `[693,402,734,455)`. At 896 × 414 the corresponding pixel counts are 417 and 400. These are software-rasterizer diagnostics, not GPU reference values. Some sprite fragments fail the depth test; neither unit is fully hidden. The complete counts, projected bounds, hashes, input identities, and source snapshot are in [scene_reference_validation.json](scene_reference_validation.json).

## What was reproduced

`tools/scene_export.cpp` calls the same native scene and presentation APIs as `platform/apple/NativeApp.mm`. The exporter transcribes the app's inspection-camera float expressions and matrix multiplication. It uses the initial animation state with zero elapsed time, emits terrain first, then sprites in the same stable depth order. Source hashes pin the version reviewed.

The Python rasterizer consumes the exported C++ clip positions and **native `decode_png` RGBA output**. It approximates `MetalRenderer.mm`: linear normalized-coordinate texture filtering, clamp-to-edge addressing, no mipmaps, no culling, less-or-equal depth testing with writes, the same alpha discard threshold, and the same straight-alpha blend factors. Texture rows remain in native top-to-bottom order. Nothing flips UVs or relocates units to make the picture pass.

The current inspection camera is deliberately separate from recovered original-camera research. It frames the grid for inspection. This image does not establish the original game's camera, viewport hierarchy, or final presentation.

## Reproduce locally

Run the following from the `native/` directory with the original archive and previously extracted 480-byte terrain table available locally. The table extractor and its provenance are documented in [TERRAIN_DECODER.md](TERRAIN_DECODER.md). The compile below was successful with GCC 13.3.0, C++17, and the stated strict warning options.

```sh
mkdir -p ../research/scene_reference/bin
c++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -I include -I third_party/lodepng \
  tools/scene_export.cpp \
  src/Archive.cpp src/Mission.cpp src/Movement.cpp src/Atlas.cpp src/Image.cpp \
  src/SpriteGeometry.cpp src/SpriteAnimation.cpp src/SpritePlacement.cpp \
  src/Terrain.cpp src/World.cpp src/Scene.cpp src/InitialPresentation.cpp \
  third_party/lodepng/lodepng.cpp -lz \
  -o ../research/scene_reference/bin/scene_export

../research/scene_reference/bin/scene_export \
  ../research/assets/iphone.big \
  ../research/native_terrain/terrain_visual_tables.bin \
  ../research/scene_reference/export_new 1792 828

python3 ../analysis_tools/scene_reference/render.py \
  ../research/scene_reference/export_new/scene.json \
  --out ../research/scene_reference/render_new
```

Both output directories must be new. Python requires NumPy and Pillow. The validation record captures the versions used for this run. The exporter builds directly against native sources; CMake and the product app were not modified for this check.

Existing local previews are under `../research/scene_reference/render_896x414/` and `../research/scene_reference/render_1792x828/`. Each contains `cpu_reference_scene.png`, a terrain-only PNG, and a JSON diagnostic report.

## Limits and packaging

This is one static initial-scene integration check. It does not test Metal shader compilation, GPU state, UIKit controls, animation progression, display refresh, sustained frame pacing, gameplay, saving, campaign completion, or iPhone XR behavior. Software subpixel rules, filtering, and floating-point/depth rounding may differ from actual hardware. Known terrain omissions, including outside-map padding, remain unchanged. The terrain-only picture is diagnostic, not an independently established original-game image oracle.

Only the reusable exporter/rasterizer sources and evidence metadata belong in Git. Exported geometry, RGBA textures, PNG previews, and the compiled exporter are derived from the original game data and remain outside the native source repository.
