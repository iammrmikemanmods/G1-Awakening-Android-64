# Original bitmap fonts — bounded native recovery

This pass adds a portable version-2 `CGraphicFont` descriptor, the original
width/fit rules, original un-clipped glyph draw plans, and the keyed indexed
BMP image family. It does not replace the game's full layout engine, prove
original GPU output, or restore all native UIKit text controls.

## Original identities

- Original executable SHA-256:
  `0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd`.
- Original `iphone.big` SHA-256:
  `663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4`.
- `CGraphicFont` RTTI string at `0xbb0c4`; vtable at `0xd3444`.
- Font-manager catalog at `0xcf4b4`. Manager initialization at `0x62776`
  clears twelve object slots; the loader at `0x6259c` indexes the catalog in
  eight-byte descriptor/image-property pairs.

| Slot | Descriptor ID | Image property ID | Image ID | Native image scope |
| --- | ---: | ---: | ---: | --- |
| 0 | 1134 | 608 | 483 | Keyed 8-bit BMP, 17px uppercase glyphs |
| 1 | 609 | 597 | 1112 | Keyed 4-bit BMP |
| 2 | 984 | 1276 | 748 | Keyed 8-bit BMP, 10px uppercase glyphs including `%` |
| 3 | 525 | 1005 | 1285 | Keyed 8-bit BMP, large numbers |
| 4 | 688 | 491 | 913 | Descriptor only; PNG conversion unverified |
| 5 | 1273 | 1017 | 1227 | Descriptor only; PNG conversion unverified |
| 6 | 449 | 1060 | 632 | Descriptor only; PNG conversion unverified |
| 7 | 469 | 542 | 858 | Descriptor only; PNG conversion unverified |
| 8 | 378 | 1290 | 578 | Descriptor only; PNG conversion unverified |
| 9 | 567 | 1108 | 918 | Descriptor only; PNG conversion unverified |
| 10 | 804 | 965 | 828 | Descriptor only; PNG conversion unverified |
| 11 | 775 | 707 | 664 | Keyed 8-bit BMP |

All descriptor resources use key `0xf4e02223`; image properties use
`0xe179f4be`, and the supported BMP resources use `0xb717945f`.
The twelve original descriptors contain 1,017 glyph records. Exact resource
identities, dimensions, hashes and independent pixel checksums are preserved
in `source_asset_inventory.json`.

Prior PC research independently identified font slot 0's bitmap and mapping.
It was useful discovery evidence, but its desktop text policy was not used as
the original layout oracle. The native implementation reads the supplied
archive at runtime and embeds no original glyph image or executable.

## Recovered behavior

The original loader `0x14ada` copies a twelve-byte header, reads a length-
prefixed UTF-8 character map and each glyph's ten-byte serialized record,
then reads blank-character identities and their two-byte records. Character
identities are stored in 16 bits in the original decoded objects. The native
decoder retains source order and rejects unsupported framing, invalid UTF-8,
out-of-image crops, missing fields and trailing data. Header byte 4 is not
used by the recovered metric/draw methods; slot 6 proves it need not equal
the glyph-height byte.

The original virtual methods establish these rules:

- `0x14668`: line metric = signed header height + signed leading, plus 10
  when height exceeds 15.
- `0x14682`: returns signed leading.
- `0x1468a`: midpoint binary search for a glyph. Native lookup deliberately
  keeps that search, because the actual slot-5 map contains duplicate `ñ`
  identities and changing to a first-match search can select another crop.
- `0x146c2`: linear first-match blank lookup.
- `0x146e6`: blank advance takes precedence over glyph advance. An unknown
  character returns zero. The API performs no case conversion or invented
  replacement.
- `0x14a54`: string measurement adds signed tracking after every accepted
  character, including unknown characters, and subtracts tracking once at
  the end. Width fitting compares the available space with the larger of
  advance and advance-plus-tracking.
- `0x148aa`: a blank advances the draw pen without a draw; an unknown glyph
  neither draws nor advances the pen. Each known glyph preserves its crop,
  offsets and advance. Heights above 15 add five pixels to draw Y. Requested
  right/bottom limits crop glyph width/height. The original 16.16 translation
  truncates positions to signed 16-bit pixel coordinates.

The measurement/draw discrepancy for unknown characters is original behavior,
not an excuse to substitute a glyph. Native hosts should check `contains()`
before choosing this path for a user-facing label. Slot 2 covers
`OPTIMUS PRIME +25%`; slot 0 does not contain `%`. Mixed-case native labels
require an explicitly supported source font or the existing host fallback.

External graphics clipping rectangles, word wrapping, multiline alignment,
text reveal scheduling, palette effects and original GPU sampling/blending
are outside this bounded layout API.

## Bitmap boundary

The five BMP sources include one 4-bit sheet and two 8-bit sheets carrying
two zero trailer bytes included in their declared image size. A separate
bounded font-image decoder handles those observed forms; the existing strict
support-overlay BMP decoder is unchanged. Dimensions, row spans, palettes,
compression, source indices, trailers and declared sizes are checked before
use. Output is top-down straight RGBA. Color-key comparison uses the original
low-24-bit rule and produces transparent magenta for a match; other palette
entries are opaque regardless of their reserved high byte.

PNG font images are not silently treated as this profile. Several contain
nontrivial source alpha or opaque magenta, and the corresponding original
property/loader conversion still needs its own evidence. Their native asset
loads currently throw `FormatError`; their descriptors remain inspectable.

## Verification and reproduction

`original_font_oracle.py` executes the unchanged original metric, lookup,
width/fit and draw functions under Unicorn. Original executable pages are
read-only. Prepared font records, a string-length routine and graphics
submission/transform calls are explicit substituted boundaries. No instruction
patches, gameplay, renderer pixels, device performance or full-port completion
are claimed by this oracle.

The fixture contains 240 width/draw cases, 1,052 original draw submissions,
36 advance queries and the original metrics for four synthetic profiles.
The cases cover positive/negative tracking, duplicate identities, blank
precedence, absent glyphs, UTF-32 identities and right/bottom extent limits.
The C++ suite compares those outputs and checks all truncated descriptor
prefixes, strict UTF-8 rejection, 4-bit/8-bit palette rows in both orientations,
color keying, and malformed BMP/property boundaries.

With the original archive supplied, the same executable checks all twelve
descriptors and compares every decoded supported bitmap's independent RGBA
checksum. Expected values were generated with Pillow from the original
archive and the documented color-key rule. These source-pixel checks are
separate from original-renderer or physical-device checks.

From the repository root:

```sh
python3 native/research/font_recovery/original_font_oracle.py \
  --binary /path/to/original/G1awakening \
  --unicorn-dir /path/to/unicorn/python \
  --output native/research/font_recovery/original_vectors.json \
  --fixture native/research/font_recovery/original_vectors.hpp

python3 native/research/font_recovery/inspect_font_assets.py \
  --binary /path/to/original/G1awakening --archive /path/to/iphone.big \
  --output native/research/font_recovery/source_asset_inventory.json
```

Use the CMake-registered bitmap-font test for portable fixture checks; pass
the original archive path to that executable for source-asset checks. The
original executable is used only by these offline recovery tools.
