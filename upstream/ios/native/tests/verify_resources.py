#!/usr/bin/env python3
"""Build and independently compare C++ resource output with the Python audit.

This verifies only archive and M3G container decoding on the current host.
It is not an iOS build, gameplay test, or device-performance test.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import tempfile
import zipfile
import zlib


def checked(command, **kwargs):
    return subprocess.run([str(v) for v in command], check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipa", type=Path)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    manifest = json.loads((root/"native/tests/resource_reference.json").read_text())
    models = manifest["models"]
    source_hash = hashlib.sha256(args.ipa.read_bytes()).hexdigest()
    if source_hash != manifest["input"]["sha256"]:
        raise RuntimeError("IPA does not match the verified source manifest")
    with zipfile.ZipFile(args.ipa) as source:
        archive = source.read("Payload/G1awakening.app/iphone.big")
    if hashlib.sha256(archive).hexdigest() != manifest["archive"]["sha256"]:
        raise RuntimeError("FGIB archive hash mismatch")
    args.out.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which(args.compiler)
    if compiler is None:
        raise RuntimeError(f"compiler not found: {args.compiler}")
    common = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wconversion", "-Werror",
              "-I", root/"native/include", root/"native/src/Archive.cpp", root/"native/src/M3G.cpp"]
    builds = {}
    for tool in ("g1_asset_probe", "m3g_probe"):
        executable = args.out/tool
        command = common+[root/f"native/tools/{tool}.cpp", "-lz", "-o", executable]
        completed = checked(command)
        builds[tool] = {"command": list(map(str, command)), "stdout": completed.stdout,
                        "stderr": completed.stderr, "exit_code": completed.returncode}
    m3g_counts = {m["member_index"]: m["object_count"] for m in models}
    rejection_results = []
    with tempfile.TemporaryDirectory(prefix="g1-native-") as tmp:
        work = Path(tmp)
        source_big = work/"iphone.big"
        source_big.write_bytes(archive)
        extraction = work/"cpp-output"
        run = checked([args.out/"g1_asset_probe", source_big, extraction])
        rows = list(csv.DictReader(io.StringIO(run.stdout), delimiter="\t"))
        if len(rows) != len(manifest["resources"]):
            raise RuntimeError("native resource count mismatch")
        for row, expected in zip(rows, manifest["resources"]):
            index = expected["index"]
            data = (extraction/f"{index:04d}.bin").read_bytes()
            validations = [int(row["index"]) == index,
                int(row["group_id"]) == expected["group_id"],
                int(row["resource_id"]) == expected["group_id"]+expected["group_ordinal"],
                int(row["key"]) == int(expected["key_hex"], 16),
                int(row["size"]) == expected["size"] == len(data),
                int(row["adler32"]) == zlib.adler32(data)&0xffffffff,
                row["kind"] == expected["kind"],
                hashlib.sha256(data).hexdigest() == expected["sha256"],
                int(row["m3g_objects"]) == m3g_counts.get(index, 0)]
            if not all(validations):
                raise RuntimeError(f"native/Python differential mismatch at resource {index}: {validations}")

        def rejected(name, data, tool, suffix):
            path = work/(name+suffix)
            path.write_bytes(data)
            attempt = subprocess.run([str(args.out/tool), str(path)], text=True,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
            if attempt.returncode != 1 or not attempt.stderr.startswith("ERROR:"):
                raise RuntimeError(f"malformed case was not cleanly rejected: {name}: {attempt.returncode}")
            rejection_results.append({"case": name, "exit_code": attempt.returncode,
                                      "error": attempt.stderr.strip()})

        rejected("empty", b"", "g1_asset_probe", ".big")
        rejected("truncated_header", archive[:16], "g1_asset_probe", ".big")
        rejected("truncated_body", archive[:-1], "g1_asset_probe", ".big")
        broken = bytearray(archive); struct.pack_into("<I", broken, 20, 0xffffffff)
        rejected("invalid_member_count", broken, "g1_asset_probe", ".big")
        broken = bytearray(archive); struct.pack_into("<I", broken, 24, 0xffffffff)
        rejected("invalid_body_offset", broken, "g1_asset_probe", ".big")
        compressed = next(r for r in manifest["resources"] if r["compression"] == "zlib")
        broken = bytearray(archive); struct.pack_into("<I", broken, compressed["offset"]+4, 0x7fffffff)
        rejected("inflation_limit", broken, "g1_asset_probe", ".big")
        broken = bytearray(archive); broken[compressed["offset"]+compressed["stored_size"]-1] ^= 1
        rejected("zlib_checksum", broken, "g1_asset_probe", ".big")
        sample_model = (extraction/f"{models[0]['member_index']:04d}.bin").read_bytes()
        rejected("m3g_truncation", sample_model[:-1], "m3g_probe", ".m3g")
        broken = bytearray(sample_model); broken[-1] ^= 1
        rejected("m3g_checksum", broken, "m3g_probe", ".m3g")
        broken = bytearray(sample_model); struct.pack_into("<I", broken, 13, 0xffffffff)
        rejected("m3g_section_length", broken, "m3g_probe", ".m3g")
        # Invalid M3G object length with a VALID section checksum catches a
        # parser error independently of the checksum validation path.
        broken = bytearray(sample_model)
        section_length = struct.unpack_from("<I", broken, 13)[0]
        struct.pack_into("<I", broken, 22, 0xffffffff)
        struct.pack_into("<I", broken, 12+section_length-4,
                         zlib.adler32(broken[12:12+section_length-4])&0xffffffff)
        rejected("m3g_object_bounds_valid_checksum", broken, "m3g_probe", ".m3g")
        # Header-only file with all declared sizes/checksums made consistent.
        broken = bytearray(sample_model[:12+section_length])
        struct.pack_into("<I", broken, 29, len(broken))
        struct.pack_into("<I", broken, len(broken)-4, zlib.adler32(broken[12:-4])&0xffffffff)
        rejected("m3g_header_only_valid_checksum", broken, "m3g_probe", ".m3g")
        # Add an object to section zero and update every outer checksum/size.
        # Its body is irrelevant: the first section may contain only Header.
        extra_object = struct.pack("<BI", 1, 0)
        broken = bytearray(sample_model[:12+section_length-4] + extra_object +
                           sample_model[12+section_length-4:])
        struct.pack_into("<I", broken, 13, section_length+5)
        struct.pack_into("<I", broken, 17, section_length-13+5)
        struct.pack_into("<I", broken, 29, len(broken))
        checksum_pos = 12+section_length+5-4
        struct.pack_into("<I", broken, checksum_pos, zlib.adler32(broken[12:checksum_pos])&0xffffffff)
        rejected("m3g_extra_object_in_header_section", broken, "m3g_probe", ".m3g")
        (args.out/"native_resource_inventory.tsv").write_text(run.stdout)
    evidence = {"source_ipa_sha256": source_hash, "platform": platform.platform(),
                "compiler": checked([compiler, "--version"]).stdout.splitlines()[0],
                "builds": builds, "resource_payload_sha256_matches": len(rows),
                "m3g_container_object_count_matches": len(models),
                "malformed_input_rejections": rejection_results,
                "ios_compiled": False, "device_tested": False, "gameplay_tested": False}
    (args.out/"native_validation.json").write_text(json.dumps(evidence, indent=2)+"\n")
    print(json.dumps({k: v for k, v in evidence.items() if k != "builds"}, indent=2))


if __name__ == "__main__":
    main()
