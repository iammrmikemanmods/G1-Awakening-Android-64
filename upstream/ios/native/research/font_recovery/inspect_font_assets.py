#!/usr/bin/env python3
"""Reproduce the original font table/resource inventory and independent pixels."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import zlib

from PIL import Image

IMAGE_SHA256 = '0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd'
ARCHIVE_SHA256 = '663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    archive, image = args.archive.read_bytes(), args.binary.read_bytes()
    if hashlib.sha256(image).hexdigest() != IMAGE_SHA256 or hashlib.sha256(archive).hexdigest() != ARCHIVE_SHA256:
        raise ValueError('original input hash mismatch')
    groups, group_count, members, member_count, body, body_size = struct.unpack_from('<6I', archive, 8)
    resources = {}
    for i in range(group_count):
        resource_id, count, first = struct.unpack_from('<IHH', archive, groups + 8 * i)
        for j in range(count):
            index = first + j
            key, start = struct.unpack_from('<II', archive, members + 8 * index)
            end = struct.unpack_from('<I', archive, members + 8 * (index + 1) + 4)[0]
            flags = struct.unpack_from('<I', archive, start)[0]
            data = zlib.decompress(archive[start + 12:end]) if flags == 0x800004 else archive[start + 4:end]
            resources[resource_id + j] = (index, key, data)
    rows = []
    for slot in range(12):
        tag, property_id = struct.unpack_from('<II', image, 0xcf4b4 - 0x1000 + slot * 8)
        descriptor_id = tag & 0xffffff
        _, property_key, properties = resources[property_id]
        if property_key != 0xe179f4be:
            raise ValueError('font image property type changed')
        values = dict(struct.iter_unpack('<II', properties))
        image_id = values[14] & 0xffffff
        _, image_key, pixels = resources[image_id]
        _, descriptor_key, descriptor = resources[descriptor_id]
        if descriptor_key != 0xf4e02223:
            raise ValueError('font descriptor type changed')
        bitmap = Image.open(io.BytesIO(pixels))
        rgba = bytearray(bitmap.convert('RGBA').tobytes())
        is_bmp = pixels[:2] == b'BM'
        if is_bmp:
            for i in range(0, len(rgba), 4):
                rgb_word = rgba[i + 2] | (rgba[i + 1] << 8) | (rgba[i] << 16)
                if rgb_word == (values[9] & 0xffffff):
                    rgba[i:i + 4] = bytes([255, 0, 255, 0])
                else:
                    rgba[i + 3] = 255
        row = {'slot': slot, 'descriptor_id': descriptor_id,
            'image_property_id': property_id, 'image_id': image_id,
            'image_type': 'bmp' if is_bmp else 'png',
            'image_width': bitmap.width, 'image_height': bitmap.height,
            'descriptor_bytes': len(descriptor),
            'descriptor_sha256': hashlib.sha256(descriptor).hexdigest(),
            'image_sha256': hashlib.sha256(pixels).hexdigest(),
            'glyph_count': struct.unpack_from('<H', descriptor, 8)[0],
            'blank_count': struct.unpack_from('<H', descriptor, 10)[0],
            'header': list(descriptor[:12])}
        if is_bmp:
            row['independent_rgba_adler32'] = zlib.adler32(rgba) & 0xffffffff
            row['independent_rgba_sha256'] = hashlib.sha256(rgba).hexdigest()
        rows.append(row)
    report = {'schema': 1, 'image_sha256': IMAGE_SHA256, 'archive_sha256': ARCHIVE_SHA256,
        'font_table': '0xcf4b4', 'slots': rows,
        'bmp_pixel_reference': 'Pillow decode then original low-24-bit keyed palette alpha; original magenta RGB retained',
        'png_native_load_supported': False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'font_slots': len(rows), 'glyph_records': sum(r['glyph_count'] for r in rows),
        'keyed_bmp_sheets': sum(r['image_type'] == 'bmp' for r in rows)}))


if __name__ == '__main__':
    main()
