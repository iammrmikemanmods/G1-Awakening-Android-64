#!/usr/bin/env python3
"""Execute unchanged original CGraphicFont lookup, metrics, width and draws.

Prepared decoded records are the input boundary. Native-parser equivalence is
checked separately against the original loader. Rendering calls record source
rectangles and fixed-point positions; no GPU or original texture conversion is
claimed. No original executable or game image is embedded in generated output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

IMAGE_SHA256 = "0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd"


def s32(value):
    return struct.unpack('<i', struct.pack('<I', value & 0xffffffff))[0]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--unicorn-dir', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--fixture', type=Path, required=True)
    args = p.parse_args()
    sys.path.insert(0, str(args.unicorn_dir))
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE, UC_PROT_READ, UC_PROT_EXEC
    from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
        UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)
    image = args.binary.read_bytes()
    if hashlib.sha256(image).hexdigest() != IMAGE_SHA256:
        raise ValueError('unexpected original executable SHA-256')
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.mem_map(0x1000, 0x100000)
    uc.mem_write(0x1000, image)
    uc.mem_protect(0x1000, 0xd1000, UC_PROT_READ | UC_PROT_EXEC)
    uc.mem_map(0x1000000, 0x200000)
    uc.mem_map(0x2000000, 0x20000)
    uc.mem_map(0x3000000, 0x1000)
    registers = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
    heap = 0x1000000

    def allocate(data):
        nonlocal heap
        address = heap
        heap += (len(data) + 15) & ~15
        uc.mem_write(address, bytes(data))
        return address

    def word(address):
        return struct.unpack('<I', uc.mem_read(address, 4))[0]

    # Synthetic descriptor values include negative tracking/offsets, a duplicate
    # identity with different metrics, a blank override and missing characters.
    characters = [ord('!'), ord('A'), ord('B'), ord('B'), ord('Z'), 0xc9]
    glyphs = [
        [1, 2, 3, 9, -1, 2, 3, 0],
        [5, 2, 7, 12, 0, 1, 8, 0],
        [15, 2, 6, 11, 2, -1, 7, 0],
        [25, 2, 5, 10, -2, 3, 6, 0],
        [35, 2, 9, 13, 1, 0, 10, 0],
        [45, 2, 7, 14, 0, -2, 8, 0],
    ]
    blanks = [[9, 2, 3], [32, 1, 4], [ord('!'), 3, 2]]
    profile_rows = [(10, 1, 1), (17, 1, 1), (21, -3, -5), (15, -1, 1)]
    font_objects = []
    descriptors = []
    for height, tracking, leading in profile_rows:
        header = struct.pack('<Ibbbbhh', 2, height, height, tracking, leading, len(glyphs), len(blanks))
        glyph_data = b''.join(struct.pack('<HHHBBbbbB', c, *g) for c, g in zip(characters, glyphs))
        blank_data = b''.join(struct.pack('<HBb', *b) for b in blanks)
        hp, gp, bp = allocate(header), allocate(glyph_data), allocate(blank_data)
        obj = allocate(struct.pack('<7I', 0xd3444, 0, 0x12345678, hp, gp, bp, 0))
        font_objects.append(obj)
        mapped = ''.join(chr(c) for c in characters).encode('utf8')
        bmapped = ''.join(chr(b[0]) for b in blanks).encode('utf8')
        descriptors.append(header + struct.pack('>H', len(mapped)) + mapped +
            b''.join(struct.pack('<HHBBbbbB', *g) for g in glyphs) +
            struct.pack('>H', len(bmapped)) + bmapped +
            b''.join(struct.pack('<Bb', b[1], b[2]) for b in blanks))

    vtable = bytearray(0x90)
    for offset, call in [(0x70, 0x3000011), (0x64, 0x3000021), (0x8c, 0x3000031), (0x74, 0x3000041)]:
        struct.pack_into('<I', vtable, offset, call)
    renderer = allocate(struct.pack('<I', allocate(vtable)))
    draws, position = [], [0, 0]
    original_regions = [(0x14668, 0x1470e), (0x148aa, 0x14ada)]

    def hook(instance, address, size, user):
        if address == 0x3000000:
            instance.emu_stop()
            return
        r0, r1, r2, r3 = [instance.reg_read(reg) for reg in registers]
        handled = True
        if address == 0x14dd8:
            instance.reg_write(UC_ARM_REG_R0, renderer)
        elif address == 0xbac30:
            n = 0
            while word(r0 + n * 4):
                n += 1
                if n > 1000000:
                    raise RuntimeError('unterminated original string boundary')
            instance.reg_write(UC_ARM_REG_R0, n)
        elif address in (0x3000010, 0x3000040):
            pass
        elif address == 0x3000020:
            position[:] = [s32(r1) // 65536, s32(r2) // 65536]
        elif address == 0x3000030:
            if r1 != 0x12345678 or r2 != 0:
                raise RuntimeError('unexpected original image draw boundary')
            source = struct.unpack('<4h', instance.mem_read(r3, 8))
            draws.append([*source, *position])
        else:
            handled = False
            if not any(start <= address < end for start, end in original_regions):
                raise RuntimeError(f'unhandled original call 0x{address:x}')
        if handled:
            instance.reg_write(UC_ARM_REG_PC, instance.reg_read(UC_ARM_REG_LR))

    uc.hook_add(UC_HOOK_CODE, hook)

    def call(address, values, stack=()):
        uc.reg_write(UC_ARM_REG_SP, 0x2010000)
        uc.reg_write(UC_ARM_REG_LR, 0x3000001)
        for reg, value in zip(registers, values):
            uc.reg_write(reg, value & 0xffffffff)
        for index, value in enumerate(stack):
            uc.mem_write(0x2010000 + index * 4, struct.pack('<I', value & 0xffffffff))
        uc.emu_start(address | 1, 0, count=1000000)
        if uc.reg_read(UC_ARM_REG_PC) != 0x3000000:
            raise RuntimeError('original function did not return within bound')
        return s32(uc.reg_read(UC_ARM_REG_R0))

    metrics = []
    advances = []
    for slot, obj in enumerate(font_objects):
        metrics.append([call(0x14668, [obj]), call(0x14682, [obj])])
        for c in [9, 32, 33, 65, 66, 90, 201, 97, 0x1f600]:
            advances.append([slot, c, call(0x146e6, [obj, c])])
    rng = random.Random(0x14668)
    alphabet = [9, 32, 33, 65, 66, 90, 201, 97, 0x1f600]
    cases = []
    count_out = allocate(bytes(4))
    for index in range(240):
        slot = index % 4
        chars = [] if index == 0 else [rng.choice(alphabet) for _ in range(rng.randrange(1, 20))]
        width = rng.choice([-1, 0, 1, 3, 7, 10, 20, 40, 100])
        height = rng.choice([-1, 0, 1, 5, 9, 13, 25])
        x, y = rng.randrange(-30, 31), rng.randrange(-30, 31)
        text = allocate(struct.pack('<' + 'I' * (len(chars) + 1), *chars, 0))
        measured = call(0x14a54, [font_objects[slot], text, -1, width], [count_out])
        accepted = word(count_out)
        draws.clear()
        call(0x148aa, [font_objects[slot], text, -1, x], [y, width, height])
        cases.append({'profile': slot, 'characters': chars, 'x': x, 'y': y,
            'max_width': width, 'max_height': height, 'width': measured,
            'accepted': accepted, 'draws': list(draws)})
    record = {'schema': 1, 'original_image_sha256': IMAGE_SHA256,
        'oracle_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'unicorn_version': __import__('unicorn').__version__,
        'instruction_patches': 0, 'original_code_readonly': True,
        'entry_points': ['0x14668', '0x14682', '0x146e6', '0x14a54', '0x148aa'],
        'boundaries': ['prepared decoded font records', 'UTF-32 string length',
            'renderer acquisition', 'renderer transform push/pop', 'image draw submission'],
        'external_clip_rectangle': False, 'gpu_execution': False,
        'descriptors': [list(d) for d in descriptors], 'metrics': metrics,
        'advances': advances, 'cases': cases}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2) + '\n')
    lines = ['// Generated from unchanged original CGraphicFont functions; synthetic input data only.',
        '#pragma once', '#include <cstdint>', '#include <vector>', 'namespace g1_font_fixture {',
        'struct Draw { int sx,sy,width,height,x,y; };',
        'struct Case { unsigned profile; std::vector<char32_t> characters; int x,y,max_width,max_height,width; unsigned accepted; std::vector<Draw> draws; };',
        'inline const std::vector<std::vector<std::uint8_t>> descriptors{']
    lines += ['{' + ','.join(map(str, d)) + '},' for d in descriptors]
    lines += ['};', 'inline const std::vector<Case> cases{']
    for c in cases:
        lines.append('{%d,{%s},%d,%d,%d,%d,%d,%d,{%s}},' % (c['profile'],
            ','.join(map(str, c['characters'])), c['x'], c['y'], c['max_width'], c['max_height'],
            c['width'], c['accepted'], ','.join('{' + ','.join(map(str, d)) + '}' for d in c['draws'])))
    lines += ['};', '} // namespace g1_font_fixture', '']
    args.fixture.parent.mkdir(parents=True, exist_ok=True)
    args.fixture.write_text('\n'.join(lines))
    print(json.dumps({'passed': True, 'original_cases': len(cases),
        'original_draws': sum(len(c['draws']) for c in cases), 'advance_checks': len(advances)}))


if __name__ == '__main__':
    main()
