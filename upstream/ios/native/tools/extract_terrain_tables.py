#!/usr/bin/env python3
"""Extract the pinned game's terrain data tables; never extract its executable.

The native product consumes only the 480-byte data output. This build-time
utility reads a bounded ZIP member into memory and translates verified original
virtual addresses through the Mach-O segment table. It does not execute code.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

EXPECTED_IPA_SHA256 = "b24fbe5cc4d5cb603189a476c4f81dd2a6ecc18b025eae913262055a7fc443fd"
EXPECTED_IMAGE_SHA256 = "0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd"
IMAGE_MEMBER = "Payload/G1awakening.app/G1awakening"
TABLE_ADDRESSES = (0xD9084, 0xD8F38, 0xD8FE2)
ATLAS_RESOURCE_IDS = (693, 777, 761)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def macho_segments(image):
    if len(image) < 28:
        raise ValueError("truncated Mach-O header")
    magic, cpu, subtype, _, ncmds, sizecmds, _ = struct.unpack_from("<7I", image)
    if magic != 0xFEEDFACE or cpu != 12 or subtype != 6:
        raise ValueError("expected the pinned little-endian ARMv6 Mach-O")
    if ncmds > 4096 or sizecmds > len(image) - 28:
        raise ValueError("invalid Mach-O command extent")
    offset, end = 28, 28 + sizecmds
    segments = []
    for _ in range(ncmds):
        if offset + 8 > end:
            raise ValueError("truncated Mach-O load command")
        command, size = struct.unpack_from("<2I", image, offset)
        if size < 8 or size > end - offset:
            raise ValueError("invalid Mach-O load-command size")
        if command == 1:  # LC_SEGMENT, 32-bit.
            if size < 56:
                raise ValueError("truncated LC_SEGMENT")
            name = image[offset + 8:offset + 24].split(b"\0", 1)[0].decode("ascii")
            vmaddr, vmsize, fileoff, filesize, _, _, nsects, _ = struct.unpack_from("<8I", image, offset + 24)
            if 56 + nsects * 68 != size or fileoff + filesize > len(image) or filesize > vmsize:
                raise ValueError("invalid Mach-O segment extent")
            segments.append((name, vmaddr, fileoff, filesize))
        elif command == 0x21:  # LC_ENCRYPTION_INFO.
            if size < 20 or struct.unpack_from("<I", image, offset + 16)[0] != 0:
                raise ValueError("encrypted or malformed input is unsupported")
        offset += size
    if offset != end:
        raise ValueError("Mach-O command count does not consume its extent")
    return segments


def extract_tables_from_image(image):
    if sha(image) != EXPECTED_IMAGE_SHA256:
        raise ValueError("original executable SHA256 differs from the reviewed source")
    segments = macho_segments(image)
    parts, records = [], []
    for style, (address, atlas) in enumerate(zip(TABLE_ADDRESSES, ATLAS_RESOURCE_IDS)):
        matches = [(name, off + address - base) for name, base, off, size in segments
                   if base <= address and address + 160 <= base + size]
        if len(matches) != 1:
            raise ValueError("terrain table is not contained in exactly one file-backed segment")
        name, offset = matches[0]
        part = image[offset:offset + 160]
        if any(part[i + j] >= 32 for i in range(0, 160, 5) for j in range(4)):
            raise ValueError("terrain table contains an unreviewed atlas-selector form")
        parts.append(part)
        records.append({"style": style, "original_virtual_address": address,
                        "original_virtual_address_hex": hex(address), "segment": name,
                        "original_file_offset": offset, "output_offset": style * 160,
                        "record_count": 32, "record_size": 5, "sha256": sha(part),
                        "atlas_resource_id": atlas, "atlas_width": 512, "atlas_height": 256,
                        "atlas_cell_width": 64, "atlas_cell_height": 64})
    data = b"".join(parts)
    metadata = {"schema_version": 1, "data_format": "G1 native terrain visual tables, raw style-major v1",
                "size": len(data), "sha256": sha(data), "original_executable_sha256": sha(image),
                "record_fields": ["top_atlas_cell_u8", "front_atlas_cell_u8", "left_atlas_cell_u8",
                                  "right_atlas_cell_u8", "signed_height_steps_i8"],
                "world_units_per_height_step": 32, "tables": records,
                "contains_original_machine_code": False,
                "scope": "Original data tables only; no campaign linkage, camera or executable payload"}
    return data, metadata


def extract_tables_from_ipa(path):
    """Return (480 data bytes, provenance dict); perform no filesystem writes."""
    if file_sha(path) != EXPECTED_IPA_SHA256:
        raise ValueError("IPA SHA256 differs from the reviewed G1 Awakening 1.1.3 source")
    with zipfile.ZipFile(path) as archive:
        matches = [entry for entry in archive.infolist() if entry.filename == IMAGE_MEMBER]
        if len(matches) != 1:
            raise ValueError("expected exactly one original executable ZIP member")
        entry = matches[0]
        if entry.file_size != 956496 or entry.flag_bits & 1 or entry.is_dir():
            raise ValueError("unexpected executable ZIP entry")
        with archive.open(entry) as source:
            image = source.read(entry.file_size + 1)
        if len(image) != entry.file_size:
            raise ValueError("executable ZIP member size mismatch")
    data, metadata = extract_tables_from_image(image)
    metadata.update({"original_ipa_sha256": EXPECTED_IPA_SHA256, "original_member": IMAGE_MEMBER,
                     "extractor_sha256": sha(Path(__file__).read_bytes())})
    return data, metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipa", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--metadata", required=True, type=Path)
    args = parser.parse_args()
    if args.out.resolve() in (args.ipa.resolve(), args.metadata.resolve()) or args.metadata.resolve() == args.ipa.resolve():
        parser.error("input, data output and metadata output must have distinct paths")
    data, metadata = extract_tables_from_ipa(args.ipa)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.metadata.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    args.metadata.write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps({"data": str(args.out), "metadata": str(args.metadata),
                      "size": len(data), "sha256": sha(data)}))


if __name__ == "__main__":
    main()
