# Native sprite atlas metadata and quad geometry

## Implemented scope

`include/g1/Atlas.hpp` and `src/Atlas.cpp` are portable C++17 code. They decode the original sprite descriptor fields and build the original local quad positions and quantized texture coordinates. They have no CPU interpreter, JIT, guest address space, UIKit compatibility implementation, or dependency on the original executable at runtime.

This is a native rendering component, not a complete renderer or playable game. It does not select a campaign, update game state, normalize the texture coordinates for Metal, build terrain, project the world through a camera, or submit GPU commands. Animation sequences are decoded with their original duration words; their timing units and state transitions are not inferred here.

The caller supplies an original descriptor through `Archive::read`, constructs `AtlasDescriptor`, then requests `quads_for_frame(frame_index)`. The returned quads are in original reverse part order. `positions` holds four signed 16-bit XYZ vertices per part; `quantized_uv` holds the corresponding original signed 16-bit texture-coordinate pairs. These UV values still require the original M3G scale/bias transform to be recovered before a Metal sampler can use them correctly.

## Original evidence and identities

The source executable is the user's G1 Awakening iOS 1.1.3 ARMv6 image, SHA-256 `0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`. Addresses below are original virtual addresses. They are evidence identifiers, not addresses used by the native component.

| Original location | Recovered behavior |
| --- | --- |
| `0x66d58`, descriptor table `0xcf5b0` | Sprite factory selects one of 38 descriptor resource IDs and uses common image resource 961 (`0x030003c1` in the original resource lookup namespace). |
| `0x7315c` | Sprite descriptor loader. Its v2 nonempty path establishes the byte layout used here. |
| `0x735d4`–`0x735f4` | Reads version, coordinate-width flag, and big-endian rectangle count. |
| `0x7364e`, `0x73666`–`0x736ba` | Reads opaque 32-bit value and rectangle coordinates; adds one pixel to X and normally Y, except the Y adjustment is undone if adjusted Y plus height would exceed 512. |
| `0x73832`–`0x7388c` | Reads variant count, rectangle index and orientation byte. |
| `0x73950`, `0x739ec`, `0x73a3e`–`0x73a82` | Reads frames, part counts, variant references and signed part offsets. |
| `0x73b20`–`0x73c46` | Optional explicit bounds; narrow X/Y signed, width/height unsigned. Wide fields retain signed 16-bit interpretation. |
| `0x73f5e`, `0x73f84` | Extra point records. |
| `0x74014`, `0x740bc`, `0x740f2`, `0x74100` | Animation count, step count, frame index and big-endian duration word. |
| `0x728f0` | Builds original local quad positions and quantized UVs. This function's output was also checked by executing its unchanged original instructions in a development-only oracle. |

The common image is original resource 961, a 512×512 RGBA PNG. Its payload SHA-256 is `fec01805fd0e58b17085dfaccfb7c7e74591d5347cc128d420bd70622aad9979`. The original image remains a user-supplied asset; it is not included in this native source tree.

## Descriptor framing

All multi-byte fields below are big-endian. The decoded object owns its values and preserves the original fields.

| Field | Encoding |
| --- | --- |
| Version / coordinate-width flag | `u8`, `u8`; current decoder accepts version 2 and flag 0 or 1. |
| Rectangle count / opaque tag | `u16`, `u32`; all 38 observed files contain the tag. The tag is preserved, not assigned an invented meaning. |
| Rectangles | Count × `(i16 x, i16 y, i16 width, i16 height)`. |
| Variant list | `u16 count`, then `(u8 rectangle_index, u8 orientation)`. |
| Frame list | `u16 count`; each frame has `u8 part_count` and parts `(u8 variant_index, signed x, signed y)`. Signed coordinates are 8-bit when flag 0, 16-bit when flag 1. |
| Explicit bounds | One boolean byte per frame, optionally four fields. Narrow bounds are `(i8 x, i8 y, u8 width, u8 height)`; wide bounds retain four 16-bit fields. |
| Extra points | `u8 count`, then signed coordinate pairs with the same width flag. |
| Animations | `u8 count`; each has `u8 step_count`, then `(u8 frame_index, u16 duration_units)`. |

The actual corpus uses only narrow coordinates, no explicit bounds, and no extra points. The alternative width/bounds/point branches are supported by the original loader and covered by constructed contract fixtures; they are not claimed as observed original asset data.

Unknown versions, nonboolean flags, orientations above 7, missing referenced rectangles/variants/frames, truncation, overlarge declared lists, and trailing bytes are rejected. The original resource byte limit is enforced. Rectangle words are preserved as signed values; validating a future GPU texture's dimensions is a separate renderer responsibility.

### Seven empty descriptors

Resources **539, 607, 769, 781, 809, 933 and 1037** contain zero rectangles and zero variants, while retaining frames and animations. Every observed file still contains the same four-byte opaque tag after the rectangle count. Parsing that tag consumes each file exactly and round-trips all bytes.

The identified original loader branches directly from `0x7363a` to `0x73828` for zero rectangle counts, bypassing the tag read. Its original consumption path for these seven files is therefore unresolved. The native decoder handles their observed framing and emits no quads for their empty frames. They were excluded from the original-function geometry comparison. No claim is made that executing this loader on them would match the native parser.

## Recovered geometry

For a part at `(dx, dy)` with rectangle width `w` and height `h`, orientations 0–3 produce these local XY vertices:

```text
(dx, -dy), (dx, -dy-h), (dx+w, -dy), (dx+w, -dy-h)
```

Orientations 4–7 all enter the same original branch:

```text
(dx, -dy-w), (dx+h, -dy-w), (dx, -dy), (dx+h, -dy)
```

All Z values are zero. The routine does not treat these orientation bytes as a conventional independent flip-bit mask. In particular, orientation 3 does not take both UV-reversal branches.

After the loader's rectangle-origin adjustment, the original UV arithmetic is:

```text
u = adjusted_x * 128 - 32767
v = adjusted_y * 128 - 32767
du = width * 128 - 4
dv = height * 128 - 4

orientation 2: u += du; du = -du
orientation 1: v += dv; dv = -dv

UV order: (u,v), (u,v+dv), (u+du,v), (u+du,v+dv)
```

The native code explicitly retains the low 16 bits for original halfword stores, avoiding undefined negative shifts and implementation-defined signed narrowing. The reverse part traversal, vertex order, orientation branches, offsets, and quantized UV arithmetic are checked against the original routine. They have not yet been checked as GPU-rendered pixels.

## Validation completed

Local validation used GCC 13.3.0 targeting `x86_64-linux-gnu`, C++17 and strict warnings. The original-data verifier builds with `-Wall -Wextra -Wconversion -Werror`. These local results do not establish an Apple SDK build or iPhone behavior.

| Check | Result |
| --- | --- |
| Synthetic parser/geometry contracts | 71 passed, including signed/narrow/wide fields, orientation branches, bottom-edge adjustment, reverse draw order, invalid references, truncation and empty metadata. |
| All original descriptors | 38 matched a separate Python parser; every decoded field round-trips to the original bytes. Standalone and archive-ID/key lookup results match. |
| Metadata counts | 311 rectangles, 416 variants, 700 frames, 3,639 frame parts, 170 animations, 728 steps. |
| Malformed original-data variants | 114 rejected cleanly. |
| Original arithmetic comparison | 31 nonempty descriptors, 617 frames, all 3,639 quads matched the unchanged original `0x728f0` output. |

Records: `research/atlas_validation/native_atlas_validation.json`, `research/atlas_validation/original_quad_oracle.json`, and compact `tests/atlas_reference.json`. The reference contains identifiers, counts and hashes rather than original descriptor/image payloads.

Reproduce the portable checks from this directory:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
python3 tests/verify_atlas.py /path/to/G1awakening_v1.1.3_os30.ipa --out /path/to/atlas-check
```

The development-only original arithmetic oracle lives outside the product source in `g1_port/research/native_renderer/quad_oracle.py`. It uses external Unicorn 2.1.4 and the supplied original executable only as a test oracle. It is not linked into the native library and is not needed to run the native component.

That oracle preserves original code pages as read-only and unchanged. It substitutes six named infrastructure boundaries: unwind registration/unregistration, read-only vector copy and paired teardown, and two M3G GPU-upload calls. None computes coordinates. Original vector indexing, geometry arithmetic and VFP register save/restore execute unchanged. The oracle seeds decoded descriptor vectors, so it does **not** validate the original descriptor parser, memory allocator, unwind implementation, GPU upload, or full original engine.

## Next native rendering milestone

The next safe renderer step is an Objective-C++ `MTKView` host around this C++ core, with a small Metal quad pipeline and texture loading for the original PNG. Before claiming visual fidelity, recover the M3G UV scale/bias passed by `0x72100`/`0x72340`; compare a fixed native frame against original output using the same descriptor and frame selection. Screen refresh should drive presentation while animation/game time uses the recovered timing rules.

The tactical world also needs its own recovered geometry rules. Original terrain atlases 693, 761 and 777 are 512×256 images, but no guessed tile-to-UV mapping is included here. Original terrain construction uses 32-unit cells, per-terrain five-byte face/height records, side faces and a perspective camera. The source indexes grid bytes as `y * map_width + x` in `0x6aba0`–`0x6abb4`; that does not yet establish campaign-to-grid associations or complete terrain orientation. These remain separate, evidence-driven implementation tasks.
