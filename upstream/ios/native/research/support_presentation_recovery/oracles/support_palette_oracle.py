#!/usr/bin/env python3
"""Record original property-9 dispatch and BMP palette keying, without drawing.

The original switch helper, property block, load-info stores and palette loop
execute unchanged. Explicit register/stack inputs delimit those blocks. This
is not a complete BMP loader, final texture conversion, GPU or device test.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

IMAGE_SHA = "0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd"
BMP_SHA = "9ca143bb91d5bb8f8bfc2dce2244cb3df334e5ebf059931ba827990fb36069cb"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "bmp", "unicorn-dir", "base-oracle", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    image, bmp = args.binary.read_bytes(), args.bmp.read_bytes()
    if sha(image) != IMAGE_SHA or sha(bmp) != BMP_SHA:
        raise ValueError("original input hash mismatch")
    if (bmp[:2] != b"BM" or struct.unpack_from("<I", bmp, 14)[0] != 40 or
            struct.unpack_from("<ii", bmp, 18) != (69, 63) or
            struct.unpack_from("<HI", bmp, 28) != (8, 0)):
        raise ValueError("BMP 997 layout differs from the audited indexed source")
    offset, colors = struct.unpack_from("<I", bmp, 10)[0], struct.unpack_from("<I", bmp, 46)[0]
    if offset != 238 or colors != 46:
        raise ValueError("BMP 997 palette boundary differs")
    original_palette = list(struct.unpack_from("<" + "I" * colors, bmp, 54))
    sys.path.insert(0, str(args.unicorn_dir))
    import unicorn
    spec = importlib.util.spec_from_file_location("support_palette_base", args.base_oracle)
    base = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(base)
    original = base.OriginalAnimation(image)
    ar, u, sp = original.ar, original.u, 0x21ff000

    def block(start, end, registers):
        for index in range(13):
            u.reg_write(getattr(ar, "UC_ARM_REG_R" + str(index)), 0)
        u.reg_write(ar.UC_ARM_REG_CPSR, 0x30)
        u.reg_write(ar.UC_ARM_REG_SP, sp)
        u.reg_write(ar.UC_ARM_REG_LR, 0x3000001)
        for index, value in registers.items():
            u.reg_write(getattr(ar, "UC_ARM_REG_R" + str(index)), value)
        u.emu_start(start | 1, end, count=100000, timeout=1_000_000)
        if u.reg_read(ar.UC_ARM_REG_PC) != end or u.reg_read(ar.UC_ARM_REG_SP) != sp:
            raise RuntimeError(f"original block {start:#x}..{end:#x} did not stop at its boundary")
        if bytes(u.mem_read(0x1000, 0xd1000)) != original.text:
            raise RuntimeError("original executable instructions changed")

    surface, value_address, info = original.alloc(0x28), original.alloc(4), original.alloc(0xb8)
    # The derived-constructor body executes after its separately inspected base
    # constructor. Only its vtable installation is needed for this contract.
    block(0x20aaa, 0x20ace, {4: surface})
    table = original.read_word(surface)
    entry = original.read_word(table + 0x14)
    parser_entry = original.read_word(table + 0x40)
    if (table, entry, parser_entry) != (0xd3f48, 0x2058d, 0x205b7):
        raise RuntimeError("surface property parser vtable differs")
    property_cases = []
    for key, source_conflict, other_source in ((0x00ff00ff, 0, 0), (0xaaff00ff, 0, 0),
                                               (0x0000ffff, 0, 0), (0x00ff00ff, 1, 0),
                                               (0x00ff00ff, 0, 1)):
        u.mem_write(sp, b"\0" * 256)
        original.word(value_address, key)
        block(0x20604, 0x20724, {0: 9, 1: value_address, 2: source_conflict,
                                5: surface, 9: other_source})
        actual = [original.read_word(sp + 0x2c), original.read_word(sp + 0x28),
                  u.reg_read(ar.UC_ARM_REG_R4)]
        expected = [0, 0, 1] if source_conflict or other_source else [1, key, 0]
        if actual != expected:
            raise RuntimeError(f"property 9 dispatch differs: {actual!r} != {expected!r}")
        property_cases.append({"key": f"0x{key:08x}", "source_conflict": source_conflict,
                               "other_source": other_source, "enabled_key_invalid": actual})

    u.mem_write(sp, b"\0" * 256)
    original.word(sp + 0x2c, 1)
    original.word(sp + 0x28, 0x00ff00ff)
    block(0x2076a, 0x20796, {})
    load_info = [u.mem_read(sp + 0x34, 1)[0], original.read_word(sp + 0x44)]
    if load_info != [1, 0x00ff00ff]:
        raise RuntimeError("property 9 did not reach the original BMP load-info fields")

    cases = [("actual-997-key-enabled", original_palette, 0x00ff00ff, True),
             ("actual-997-key-disabled", original_palette, 0x00ff00ff, False),
             ("synthetic-high-byte-and-colors", [0x00ff00ff, 0x12ff00ff, 0xffff00ff,
                                                0x00000000, 0x0000ff00, 0x000000ff,
                                                0x00ff0000, 0x80ffffff, 0x00ff00fe],
              0xaaff00ff, True)]
    palette_cases = []
    for name, values, key, enabled in cases:
        buffer = original.alloc(len(values) * 4)
        u.mem_write(buffer, struct.pack("<" + "I" * len(values), *values))
        u.mem_write(sp, b"\0" * 256)
        original.word(sp + 0xc, buffer)
        original.word(sp + 0x54, len(values) * 4)
        original.word(surface + 4, 0x348301)
        u.mem_write(info, bytes([int(enabled)]))
        original.word(info + 0x10, key)
        block(0x1caa8, 0x1cb0e, {6: surface, 8: info})
        actual = list(struct.unpack("<" + "I" * len(values), u.mem_read(buffer, len(values) * 4)))
        expected = [0x00ff00ff if enabled and (word & 0xffffff) == (key & 0xffffff)
                    else word | 0xff000000 for word in values]
        if actual != expected:
            raise RuntimeError(f"original palette keying differs for {name}")
        palette_cases.append({"name": name, "key": f"0x{key:08x}", "enabled": enabled,
                              "input_words": [f"0x{word:08x}" for word in values],
                              "output_words": [f"0x{word:08x}" for word in actual],
                              "output_bgra_bytes_sha256": sha(bytes(u.mem_read(buffer, len(values) * 4)))})
    keyed = [int(word, 16) for word in palette_cases[0]["output_words"]]
    rgba = bytearray()
    stride = (69 + 3) & ~3
    for row in range(62, -1, -1):
        for index in bmp[offset + row * stride:offset + row * stride + 69]:
            if index >= len(keyed):
                raise ValueError("BMP palette index out of range")
            value = keyed[index]
            rgba.extend(((value >> 16) & 255, (value >> 8) & 255, value & 255, value >> 24))
    report = {
        "schema": 1, "scope": "Original property 9 dispatch and BMP palette keying before texture conversion",
        "original_image_sha256": IMAGE_SHA, "bmp_resource": 997, "bmp_sha256": BMP_SHA,
        "oracle_sha256": sha(Path(__file__).read_bytes()),
        "base_oracle_sha256": sha(args.base_oracle.read_bytes()), "unicorn_version": unicorn.__version__,
        "instruction_patches": 0, "original_code_readonly_and_unchanged": True,
        "executed_blocks": ["0x20aaa..0x20ace", "0x20604..0x20724 including ARM switch helper 0xb9c80",
                            "0x2076a..0x20796", "0x1caa8..0x1cb0e"],
        "vtable": {"table": "0xd3f48", "property_initializer": "0x2058d", "parser": "0x205b7"},
        "property_cases": property_cases, "load_info_key_enabled_and_value": load_info,
        "palette_cases": palette_cases, "palette_word_comparisons": sum(len(case[1]) for case in cases),
        "derived_top_down_rgba8888": {"width": 69, "height": 63, "sha256": sha(rgba),
                                      "transparent_pixels": sum(value == 0 for value in rgba[3::4]),
                                      "opaque_pixels": sum(value == 255 for value in rgba[3::4]),
                                      "derivation": "BMP bottom-up row order, palette BGR to RGB, original keyed alpha; no final GPU quantization"},
        "substituted_calls": dict(original.calls),
        "limitations": ["Block inputs are prepared explicitly; complete resource/BMP loader is not executed",
                        "The RGBA8888 hash is a lossless derived image, not a captured original GPU texture",
                        "Original selected hardware texture conversion, orientation, scaling and drawing are outside this proof"],
        "passed": True,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"property_cases": len(property_cases), "palette_word_comparisons": report["palette_word_comparisons"],
                      "derived_top_down_rgba8888": report["derived_top_down_rgba8888"], "passed": True}))


if __name__ == "__main__":
    main()
