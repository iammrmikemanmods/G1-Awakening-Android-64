#!/usr/bin/env python3
"""Generate terrain hashes from original data and prior independent evidence.

No C++ decoder is run or inspected. Grid expectations come from the preceding
Python audit. Lookup/rule framing is independently read from the source payloads
according to 0x50390/0x2a2ba. Only hashes/counts are committed, never payloads.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

NATIVE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(NATIVE / "tools"))
from extract_terrain_tables import extract_tables_from_ipa, sha


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def decode(kind, data):
    if kind == "grid":
        if len(data) < 2 or not data[0] or not data[1] or len(data) != 2 + data[0] * data[1]:
            raise ValueError("invalid source grid")
        if any(x > 31 and x != 255 for x in data[2:]):
            raise ValueError("unsupported source grid tile")
        return {"width": data[0], "height": data[1], "tiles": list(data[2:])}
    if kind == "lookup":
        if len(data) != 33 or data[0] != 1:
            raise ValueError("unexpected original lookup framing")
        return list(data[1:])
    if kind == "rules":
        if not data or data[0] == 0:
            raise ValueError("missing source rule rows")
        cursor, rows = 1, []
        for _ in range(data[0]):
            if cursor >= len(data) or data[cursor] != 3 or cursor + 4 > len(data):
                raise ValueError("unsupported source rule row")
            rows.append(list(data[cursor + 1:cursor + 4]))
            cursor += 4
        if cursor != len(data):
            raise ValueError("source rules have trailing bytes")
        return rows
    if kind == "tables":
        if len(data) != 480:
            raise ValueError("invalid extracted data-table size")
        result = []
        for style in range(3):
            rows = []
            for tile in range(32):
                record = data[(style * 32 + tile) * 5:(style * 32 + tile + 1) * 5]
                if any(v >= 32 for v in record[:4]):
                    raise ValueError("unsupported original face selector")
                rows.append({"atlas_cells": list(record[:4]),
                             "height_steps": record[4] if record[4] < 128 else record[4] - 256})
            result.append(rows)
        return result
    raise ValueError("unknown terrain data kind")


def encode(kind, decoded):
    if kind == "grid":
        return bytes([decoded["width"], decoded["height"], *decoded["tiles"]])
    if kind == "lookup":
        return bytes([1, *decoded])
    if kind == "rules":
        return bytes([len(decoded), *(v for row in decoded for v in [len(row), *row])])
    if kind == "tables":
        return bytes(v for style in decoded for record in style
                     for v in [*record["atlas_cells"], record["height_steps"] & 255])
    raise ValueError("unknown terrain data kind")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--audit-dir", type=Path, required=True)
    ap.add_argument("--ipa", type=Path, required=True)
    ap.add_argument("--map-oracle", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    manifest_path = args.audit_dir / "results/asset_manifest.json"
    inventory_path = args.audit_dir / "results/game_data_inventory.json"
    manifest = json.loads(manifest_path.read_text())
    inventory = json.loads(inventory_path.read_text())
    members = {x["resource_id"]: x for x in manifest["resources"]}
    grids = {x["resource_id"]: x for x in inventory["grids"]}
    if set(grids) != set(range(258, 280)):
        raise ValueError("prior grid inventory is incomplete")
    resources = []
    cells = 0
    for resource_id in [*range(258, 283), 306]:
        member = members[resource_id]
        kind = "grid" if resource_id <= 279 else "lookup" if resource_id <= 282 else "rules"
        data = (args.audit_dir / "results/extracted/bin" / member["extracted_filename"]).read_bytes()
        if sha(data) != member["sha256"]:
            raise ValueError("original payload hash differs from audit")
        value = decode(kind, data)
        if encode(kind, value) != data:
            raise ValueError("independent terrain decoder does not consume source exactly")
        if kind == "grid":
            prior = grids[resource_id]
            prior_value = {"width": prior["width"], "height": prior["height"],
                           "tiles": [v for row in prior["rows"] for v in row]}
            if prior["sha256"] != sha(data) or value != prior_value:
                raise ValueError("independent grid parser differs from preceding audit")
            cells += len(value["tiles"])
        resources.append({"resource_id": resource_id, "kind": kind, "member_index": member["index"],
                          "key_hex": member["key_hex"], "archive_offset": member["offset"],
                          "stored_size": member["stored_size"], "size": member["size"],
                          "compression": member["compression"], "payload_sha256": sha(data),
                          "canonical_decode_sha256": sha(canonical(value)),
                          "element_count": len(value["tiles"]) if kind == "grid" else len(value)})
    tables, metadata = extract_tables_from_ipa(args.ipa)
    if encode("tables", decode("tables", tables)) != tables:
        raise ValueError("independent visual-table decode does not consume source exactly")
    original = json.loads(args.map_oracle.read_text())
    if (original["grid_resource_id"], original["cells"], original["quad_count"],
            original["all_native_positions_and_quantized_uv_match"]) != (268, 210, 255, True):
        raise ValueError("original first-grid oracle scope differs from reviewed case")
    if cells != 4510:
        raise ValueError("source grid cell count differs from reviewed corpus")
    result = {"reference_schema_version": 1,
              "expected_counts": {"grids": 22, "grid_cells": cells, "lookups": 3,
                                  "terrain_rules": 6, "visual_styles": 3, "visual_records": 96},
              "provenance": {"input_ipa_sha256": manifest["input"]["sha256"],
                             "archive_member": "Payload/G1awakening.app/iphone.big",
                             "archive_size": manifest["archive"]["size"],
                             "archive_sha256": manifest["archive"]["sha256"],
                             "prior_grid_inventory_sha256": sha(inventory_path.read_bytes()),
                             "prior_resource_manifest_sha256": sha(manifest_path.read_bytes()),
                             "original_map_oracle_sha256": sha(args.map_oracle.read_bytes()),
                             "generator_sha256": sha(Path(__file__).read_bytes()),
                             "expectations_derived_from_native_code": False},
              "resources": resources,
              "visual_tables": {"size": len(tables), "payload_sha256": sha(tables),
                                "canonical_decode_sha256": sha(canonical(decode("tables", tables))),
                                "original_tables": metadata["tables"]},
              "first_grid_original_geometry": {"resource_id": 268, "style": 0, "quad_count": 255,
                         "original_quad_sha256": original["original_and_native_quad_sha256"],
                         "original_neighbor_mask_sha256": original["original_neighbor_mask_sha256"],
                         "scope": "interior rows initialized previous_mask0x10; outside padding excluded"},
              "not_established": ["camera/padded full scene", "style1 animated terrain", "GPU pixel parity",
                                  "unit world anchor", "complete gameplay", "native iPhone execution"]}
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"reference": str(args.out), "sha256": sha(args.out.read_bytes()),
                      **result["expected_counts"]}))


if __name__ == "__main__":
    main()
