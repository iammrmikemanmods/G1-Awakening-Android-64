# Version-1 support overlay: recovered metadata and image mapping

This is an evidence-only research follow-up. Nothing here is linked into the
native app, and it does not change an IPA. The source under `include/`, `src/`
and `tests/` is an isolated, portable decoder and comparison probe.

The original support overlay uses **descriptor 256, image-properties resource
1000, and BMP resource 997**. Resource 1000 is a 24-byte property record. Its
property 14 resolves tagged image `0x020003e5` to resource 997, a 69 by 63 pixel,
8-bit paletted BMP. The metadata is version 1 and belongs to the separate 2D
effect bank at renderer offset `+0x28`.

The resource keys, lengths, SHA-256 values, exact source addresses and remaining
host work are recorded in [resource_256_mapping.json](resource_256_mapping.json).
No original executable, descriptor, BMP, archive or decoded pixel buffer is
included in this research delivery.

## What was verified

`original_overlay_descriptor.json` compares the isolated native decoder with
the unchanged original parser body at `0x5ecfe..0x5f7f6`, inside `0x5eb30`.
The original text and original ARM libgcc text are mapped read-only and checked
after execution. No instruction is replaced.

| Comparison | Result |
|---|---:|
| Real descriptor bytes consumed | 546 / 546 |
| Real rectangles / variants / frames / animations | 13 / 13 / 27 / 4 |
| Actual-asset scalar comparisons | 446 |
| Actual asset plus two synthetic boundary fixtures | 3 cases, 490 scalar comparisons |
| Native-only malformed or unsupported inputs rejected | 553 |

The compared outputs include every crop rectangle, every resolved identity
variant, every frame's part indices and signed offsets, and every animation
frame/duration pair. The synthetic fixtures cover an empty frame, reordered
crop references, signed byte offset endpoints and a big-endian 32767 duration.
The rejection set includes every truncated prefix of the real descriptor.

The substituted boundaries are allocation/free, exact byte filling, raw stream
byte copying, and image lookup returning dimensions independently read from the
pinned BMP. The original byte/word readers, endian conversion, signed coordinate
handling, vectors, crop constructors and variant constructors execute. This
comparison does not execute the image-properties loader or decode BMP pixels.

The optional private decoded fixture exported by the reproducer has SHA-256
`23413b5a8652206e53f1d5868f0fa8ce87fe5ef7f18c21d5232581085919d921`.
It is the same fixture used by the separate 2D clock comparison.

## Exact observed framing

All multibyte descriptor fields are big-endian. Counts and crop coordinates are
unsigned; narrow part offsets are signed bytes.

| Byte offset | Field in this asset |
|---|---|
| 0 | Version 1 |
| 1 | Narrow coordinate flag 0 |
| 2–3 | 13 rectangles |
| 4 | Zero extra images |
| 5–56 | Thirteen byte-sized `(x, y, width, height)` crops |
| 57–58 | 13 variants |
| 59–84 | Thirteen `(crop index, orientation)` pairs; all orientations 0 |
| 85–86 | 27 frames |
| 87–329 | Part counts, `(variant, signed dx, signed dy)` parts and absent-bounds flags |
| 330 | Zero extra points |
| 331 | Four animations |
| 332 / 411 / 427 / 488 | Animation 0 / 1 / 2 / 3 start; 26 / 5 / 20 / 19 steps |
| 546 | End of input |

Each animation step is `(unsigned byte frame index, unsigned 16-bit duration)`.
There is no version-2 opaque image tag at byte 4. The original version-1 crop
constructor preserves the exact crop origins; the M3G atlas's 512-pixel UV
conversion and one-pixel origin adjustment do not apply.

`OverlayDescriptor` reuses the existing `AtlasRectangle`, `AtlasVariant`,
`AtlasPart`, `AtlasFrame` and `AtlasStep` value types. It has no M3G geometry
method. Its strict supported profile matches this asset: a source image no
larger than 256 pixels per dimension, narrow coordinates, one source image,
identity variants, absent explicit bounds, no extra points, and nonempty
explicit animation sequences with positive signed-16-bit durations. Other
profiles are rejected. This is not a general version-1 format implementation.

## Support animation and separate evidence

The recovered caller starts animation 2 with repeat count 1 and reverse false.
Its visible frames 16 through 20 all use variant 11, the exact source crop
`(0, 0, 32, 14)`, at offsets `(0, -59)`, `(0, -63)`, `(0, -65)`, `(0, -67)` and
`(0, -69)`. Frame 21 is empty and supplies the blink steps. The 20 durations sum
to 1680 raw units; the separate original-player oracle establishes the timing
behavior. The original loader's overall bounds are `(-9, -90, 44, 100)`.

The support presentation follow-up owns these independent comparisons:

- `native/research/support_presentation_recovery/results/original_support_animation.json` compares
  the original 2D start/advance clock with the existing portable player. The
  host must also preserve original `0x5ca44` suppression of updates after the
  effect's completed byte is set; direct player continuation is not the wrapper's
  actual scheduling behavior. The separately executed wrapper completes animation 2
  at fixed tick 56 (1680 ms), then leaves the completed empty-frame state unchanged
  through the cue's handoff at tick 67 (2010 ms).
- `native/research/support_presentation_recovery/results/original_support_palette.json` verifies
  property 9 dispatch, propagation of the key-enabled byte/key value to the BMP
  load information, and the original palette loop. It owns the pixel/colorkey
  evidence; this parser report does not duplicate that claim.

The palette result describes lossless keyed source pixels. Original final
texture-format quantization and an Apple GPU upload are separate steps.

## Reproduction

Run from the repository directory containing `native/` and `analysis_tools/`.
The existing `native/include/g1/Atlas.hpp` supplies value types only; no production
build configuration needs changing.

```sh
c++ -std=c++17 -Wall -Wextra -Werror \
  -I native/research/overlay_validation/include -I native/include \
  native/research/overlay_validation/src/OverlayDescriptor.cpp \
  native/research/overlay_validation/tests/overlay_descriptor_probe.cpp \
  -o /tmp/g1_overlay_descriptor_probe

python analysis_tools/overlay/v1_descriptor_oracle.py \
  --binary /path/to/G1awakening.armv6 \
  --libgcc /path/to/libgcc_s.1.dylib \
  --unicorn-dir /path/to/unicorn/python \
  --base-oracle analysis_tools/combat/oracle.py \
  --descriptor /path/to/private/256.bin \
  --image-properties /path/to/private/1000.bin \
  --bmp /path/to/private/997.bmp \
  --native-probe /tmp/g1_overlay_descriptor_probe \
  --native-header native/research/overlay_validation/include/g1/OverlayDescriptor.hpp \
  --native-source native/research/overlay_validation/src/OverlayDescriptor.cpp \
  --native-probe-source native/research/overlay_validation/tests/overlay_descriptor_probe.cpp \
  --decoded-output /path/to/private/descriptor256.json \
  --output /tmp/original_overlay_descriptor.json
```

The three resource files are expanded original archive entries matching the
keys and hashes in the mapping report. The tool rejects different original
inputs. The decoded output is optional and should remain beside the private
original resources. The checked-in report contains hashes/counts and comparison
results, not the original resource bytes.

## Remaining native integration

1. Resolve the property record and decode its BMP into a native texture. The
   existing `Image.cpp` currently supports PNG input, so the BMP path still needs
   a bounded implementation and comparison.
2. Add the separate 2D effect bank to the native renderer and preserve the
   recovered support selection, active/completed guards and floating-text
   exclusion. Existing terrain and unit M3G geometry remain independent.
3. Project the recovered support anchor through the native tactical camera and
   explicitly map original 2D offsets to the actual viewport. The source offsets
   cannot be treated as UIKit points or drawable pixels without that mapping.
4. Advance the compared player from the original simulation clock, including
   `0x5ca44` completed-player suppression, and issue part draws in original
   reverse part order (`0x5e3aa..0x5e402`). Preserve the exact crop coordinates.
5. Verify projection, clipping, alpha blending, pause/transition behavior and
   visible timing in the Apple host and on the target device.

This research package adds no bitmap decoder, host draw path or device result.
