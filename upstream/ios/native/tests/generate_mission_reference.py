#!/usr/bin/env python3
"""Derive compact reference metadata from the independent original-data audit.

This tool reads only the earlier Python inventory/manifest and their source
files. It does not run or inspect the native decoder. No original payloads or
argument-word lists are copied to the reference output.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--audit-dir", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    audit = args.audit_dir.resolve()
    inventory_path = audit / "results/game_data_inventory.json"
    manifest_path = audit / "results/asset_manifest.json"
    inventory = json.loads(inventory_path.read_text())
    manifest = json.loads(manifest_path.read_text())
    members = {e["index"]: e for e in manifest["resources"]}
    streams = []
    mutations = []
    mutated = set()
    opcodes = Counter()
    subtypes = {"12": Counter()}
    site_group_counts = Counter()
    for item in inventory["mission_streams"]:
        member = members[item["member_index"]]
        if member["resource_id"] != item["resource_id"] or member["sha256"] != item["sha256"]:
            raise ValueError("audit manifest/inventory identity mismatch")
        commands = []
        for index, command in enumerate(item["commands"]):
            end = (item["commands"][index + 1]["byte_offset"]
                   if index + 1 < len(item["commands"]) else item["size"])
            commands.append({**command, "byte_size": end - command["byte_offset"]})
            opcode = command["opcode"]
            opcodes[opcode] += 1
            words = command["argument_words_u16"]
            offset = command["byte_offset"]

            def mutation(label, word_byte_offset, replacement):
                if label not in mutated:
                    mutations.append({"case": label, "resource_id": item["resource_id"],
                                      "word_byte_offset": word_byte_offset,
                                      "replacement_u16": replacement})
                    mutated.add(label)

            if opcode in (7, 15, 17, 21):
                mutation(f"opcode{opcode}_count_exceeds_original_resource", offset + 2, 65535)
            if opcode == 12:
                subtypes["12"][words[0]] += 1
                mutation("opcode12_unknown_layout_subtype", offset + 2, 4)
            elif opcode == 7:
                position = 1
                for _ in range(words[0]):
                    groups = words[position + 2]
                    site_group_counts[groups] += 1
                    mutation("opcode7_group_count_exceeds_resource", offset + 2 + 2 * (position + 2), 65535)
                    position += 3
                    for _ in range(groups):
                        mutation(f"opcode7_{groups}groups_pair_count_exceeds_resource",
                                 offset + 2 + 2 * position, 65535)
                        position += 1 + words[position] * 2
                if position != len(words):
                    raise ValueError("prior opcode 7 framing does not consume all argument words")
        expected = {"version": item["version"], "declared_instruction_count": item["declared_instruction_count"],
                    "commands": commands}
        if len(commands) != item["declared_instruction_count"]:
            raise ValueError("prior instruction count mismatch")
        streams.append({
            "resource_id": item["resource_id"], "key_hex": member["key_hex"],
            "member_index": item["member_index"], "archive_offset": member["offset"],
            "stored_size": member["stored_size"], "size": item["size"],
            "compression": member["compression"], "payload_sha256": item["sha256"],
            "version": item["version"], "instruction_count": item["declared_instruction_count"],
            "instruction_offsets": [c["byte_offset"] for c in commands],
            "argument_word_count": sum(len(c["argument_words_u16"]) for c in commands),
            "canonical_decode_sha256": sha(json.dumps(expected, sort_keys=True, separators=(",", ":")).encode()),
        })
    counts = {"mission_streams": len(streams), "instructions": sum(opcodes.values()),
              "argument_words": sum(s["argument_word_count"] for s in streams)}
    if counts != {"mission_streams": 30, "instructions": 803, "argument_words": 2800}:
        raise ValueError("prior audit counts differ from the reviewed corpus")
    result = {
        "reference_schema_version": 1,
        "scope": "Hashes and framing metadata from the independent original-data Python audit; no mission payloads",
        "provenance": {"input_ipa_sha256": manifest["input"]["sha256"],
                       "archive_member": "Payload/G1awakening.app/iphone.big",
                       "archive_sha256": manifest["archive"]["sha256"],
                       "archive_size": manifest["archive"]["size"],
                       "original_inventory_sha256": sha(inventory_path.read_bytes()),
                       "original_manifest_sha256": sha(manifest_path.read_bytes()),
                       "oracle_files_sha256": {name: sha((audit / name).read_bytes())
                                               for name in ("inspect_assets.py", "inspect_game_data.py")},
                       "generator_sha256": sha(Path(__file__).read_bytes()),
                       "expectations_derived_from_native_code": False},
        "expected_counts": counts,
        "opcode_counts": dict(sorted(opcodes.items())),
        "observed_layout_subtype_counts": {k: dict(sorted(v.items())) for k, v in subtypes.items()},
        "observed_opcode7_group_counts": dict(sorted(site_group_counts.items())),
        "streams": streams, "source_data_mutations": mutations,
        "not_established": ["opcode gameplay semantics", "30 campaign levels", "iOS build", "device behavior"],
    }
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"path": str(args.out), "sha256": sha(args.out.read_bytes()), **counts}))


if __name__ == "__main__":
    main()
