#!/usr/bin/env python3
"""Derive compact atlas reference hashes from original assets with Python struct.

This separate parser does not run the C++ implementation. It preserves every
descriptor field and round-trips it to the original bytes before hashing the
decoded result. Quad arithmetic is a separate transcription of original
0x728f0, not a claim of original-machine-code execution or rendered fidelity.
No original image or descriptor bytes are written to the reference JSON.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

RESOURCE_IDS = (1298, 1111, 1119, 1020, 1106, 1041, 426, 522, 1281, 1088,
                1155, 868, 876, 377, 985, 1036, 1196, 1032, 815, 517, 1027,
                1183, 1069, 607, 769, 708, 933, 809, 641, 1037, 539, 781,
                638, 825, 581, 1095, 1132, 982)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def signed16(value):
    return (value + 32768) % 65536 - 32768


def decode(data):
    offset = 0

    def take(fmt):
        nonlocal offset
        values = struct.unpack_from(">" + fmt, data, offset)
        offset += struct.calcsize(">" + fmt)
        return values[0] if len(values) == 1 else list(values)

    version, wide, rectangle_count, tag = take("BBHI")
    if version != 2 or wide not in (0, 1):
        raise ValueError("unobserved atlas header")
    rectangles = [take("hhhh") for _ in range(rectangle_count)]
    variants = [take("BB") for _ in range(take("H"))]
    frames = []
    for _ in range(take("H")):
        parts = [take("Bhh" if wide else "Bbb") for _ in range(take("B"))]
        present = take("B")
        if present not in (0, 1):
            raise ValueError("unobserved explicit-bounds flag")
        bounds = take("hhhh" if wide else "bbBB") if present else None
        frames.append({"parts": parts, "explicit_bounds": bounds})
    points = [take("hh" if wide else "bb") for _ in range(take("B"))]
    animations = [[take("BH") for _ in range(take("B"))] for _ in range(take("B"))]
    if offset != len(data):
        raise ValueError("atlas descriptor trailing bytes")
    for rectangle, orientation in variants:
        if not 0 <= rectangle < len(rectangles) or not 0 <= orientation <= 7:
            raise ValueError("atlas variant invalid")
    for frame in frames:
        if any(not 0 <= p[0] < len(variants) for p in frame["parts"]):
            raise ValueError("atlas part reference invalid")
    if any(not 0 <= step[0] < len(frames) for animation in animations for step in animation):
        raise ValueError("atlas animation reference invalid")
    result = {"version": version, "wide_coordinates": bool(wide), "opaque_tag": tag,
              "rectangles": rectangles, "variants": variants, "frames": frames,
              "extra_points": points, "animations": animations}
    if encode(result) != data:
        raise ValueError("separate Python decode does not round-trip original bytes")
    frame_quads = []
    for frame in frames:
        quads = []
        for part_index in reversed(range(len(frame["parts"]))):
            variant_index, dx, dy = frame["parts"][part_index]
            rectangle_index, orientation = variants[variant_index]
            x, y, w, h = rectangles[rectangle_index]
            x = signed16(x + 1)
            y = signed16(y if y + 1 + h > 512 else y + 1)
            positions = ([(dx, -dy-w), (dx+h, -dy-w), (dx, -dy), (dx+h, -dy)]
                         if orientation in (4, 5, 6, 7) else
                         [(dx, -dy), (dx, -dy-h), (dx+w, -dy), (dx+w, -dy-h)])
            u0, v0, du, dv = x * 128 - 32767, y * 128 - 32767, w * 128 - 4, h * 128 - 4
            if orientation == 2:
                u0, du = u0 + du, -du
            elif orientation == 1:
                v0, dv = v0 + dv, -dv
            uv = [(u0, v0), (u0, v0+dv), (u0+du, v0), (u0+du, v0+dv)]
            quads.append({"source_part_index": part_index,
                          "positions": [[signed16(px), signed16(py), 0] for px, py in positions],
                          "quantized_uv": [[signed16(u), signed16(v)] for u, v in uv]})
        frame_quads.append(quads)
    result["frame_quads"] = frame_quads
    return result


def encode(value):
    wide = value["wide_coordinates"]
    output = bytearray(struct.pack(">BBHI", value["version"], wide,
                                   len(value["rectangles"]), value["opaque_tag"]))
    for rectangle in value["rectangles"]:
        output += struct.pack(">hhhh", *rectangle)
    output += struct.pack(">H", len(value["variants"]))
    for variant in value["variants"]:
        output += struct.pack(">BB", *variant)
    output += struct.pack(">H", len(value["frames"]))
    for frame in value["frames"]:
        output += struct.pack(">B", len(frame["parts"]))
        for part in frame["parts"]:
            output += struct.pack(">Bhh" if wide else ">Bbb", *part)
        output += struct.pack(">B", frame["explicit_bounds"] is not None)
        if frame["explicit_bounds"] is not None:
            output += struct.pack(">hhhh" if wide else ">bbBB", *frame["explicit_bounds"])
    output += struct.pack(">B", len(value["extra_points"]))
    for point in value["extra_points"]:
        output += struct.pack(">hh" if wide else ">bb", *point)
    output += struct.pack(">B", len(value["animations"]))
    for animation in value["animations"]:
        output += struct.pack(">B", len(animation))
        for step in animation:
            output += struct.pack(">BH", *step)
    return bytes(output)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--audit-dir", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    manifest_path = args.audit_dir / "results/asset_manifest.json"
    manifest = json.loads(manifest_path.read_text())
    descriptors, totals, orientations = [], Counter(), Counter()
    for table_index, resource_id in enumerate(RESOURCE_IDS):
        members = [r for r in manifest["resources"] if r["resource_id"] == resource_id and
                   r["key_hex"] == "0xf4e02223"]
        if len(members) != 1:
            raise ValueError("descriptor lookup ambiguous or missing")
        member = members[0]
        data = (args.audit_dir / "results/extracted/bin" / member["extracted_filename"]).read_bytes()
        if sha(data) != member["sha256"]:
            raise ValueError("extracted descriptor differs from original audit manifest")
        value = decode(data)
        counts = {"rectangles": len(value["rectangles"]), "variants": len(value["variants"]),
                  "frames": len(value["frames"]), "parts": sum(len(f["parts"]) for f in value["frames"]),
                  "animations": len(value["animations"]),
                  "steps": sum(len(a) for a in value["animations"])}
        totals.update(counts)
        orientations.update(v[1] for v in value["variants"])
        descriptors.append({"table_index": table_index, "resource_id": resource_id,
                            "key_hex": member["key_hex"], "member_index": member["index"],
                            "archive_offset": member["offset"], "stored_size": member["stored_size"],
                            "size": member["size"], "compression": member["compression"],
                            "payload_sha256": sha(data), "counts": counts,
                            "canonical_decode_and_quad_sha256": sha(canonical(value)),
                            "empty_descriptor_path_unresolved": not bool(value["rectangles"])})
    result = {"reference_schema_version": 1,
              "scope": "Separate original-data Python framing/quad transcription; hashes only, no original payloads",
              "provenance": {"input_ipa_sha256": manifest["input"]["sha256"],
                             "archive_member": "Payload/G1awakening.app/iphone.big",
                             "archive_sha256": manifest["archive"]["sha256"],
                             "archive_size": manifest["archive"]["size"],
                             "original_manifest_sha256": sha(manifest_path.read_bytes()),
                             "generator_sha256": sha(Path(__file__).read_bytes()),
                             "expectations_derived_from_cpp_output": False,
                             "original_machine_code_executed": False,
                             "original_descriptor_table_va": "0xcf5b0",
                             "original_parser_va": "0x7315c", "original_quad_routine_va": "0x728f0"},
              "expected_counts": {"descriptors": len(descriptors), **totals},
              "orientation_counts": dict(sorted(orientations.items())), "descriptors": descriptors,
              "not_established": ["normalized Metal UVs", "GPU-rendered fidelity", "terrain/camera rules",
                                  "original consumption path of seven empty descriptors", "animation time units",
                                  "iOS build", "device performance"]}
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"path": str(args.out), "sha256": sha(args.out.read_bytes()),
                      **result["expected_counts"]}))


if __name__ == "__main__":
    main()
