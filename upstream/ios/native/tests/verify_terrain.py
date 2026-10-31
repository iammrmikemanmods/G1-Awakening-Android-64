#!/usr/bin/env python3
"""Compile native terrain decoding and verify the user's original local IPA.

Requires only Python stdlib, a C++17 compiler, zlib, and the supplied original
IPA. Expected hashes came from a preceding Python resource audit and an
original-instruction development oracle. No interpreter is loaded by this
verification script or linked into the native product.
"""
import argparse
import json
from pathlib import Path
import platform
import shutil
import sys
import tempfile
import zipfile

from generate_terrain_reference import decode, encode, canonical, sha
from verify_missions import payload, run

NATIVE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(NATIVE / "tools"))
from extract_terrain_tables import extract_tables_from_ipa, file_sha


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("ipa", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--compiler", default="g++")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    reference_path = NATIVE / "tests/terrain_reference.json"
    reference = json.loads(reference_path.read_text())
    counts = {"grids": 22, "grid_cells": 4510, "lookups": 3, "terrain_rules": 6,
              "visual_styles": 3, "visual_records": 96}
    provenance = reference["provenance"]
    if (reference["reference_schema_version"] != 1 or reference["expected_counts"] != counts or
            provenance["expectations_derived_from_native_code"] or
            {x["resource_id"] for x in reference["resources"]} != {*range(258, 283), 306}):
        raise ValueError("terrain reference identities/counts/provenance are incomplete")
    if file_sha(args.ipa) != provenance["input_ipa_sha256"]:
        raise ValueError("source IPA differs from the reviewed corpus")
    with zipfile.ZipFile(args.ipa) as source:
        entries = [e for e in source.infolist() if e.filename == provenance["archive_member"]]
        if len(entries) != 1 or entries[0].file_size != provenance["archive_size"]:
            raise ValueError("original archive member is absent, duplicated or has unexpected size")
        archive = source.read(entries[0])
    if sha(archive) != provenance["archive_sha256"]:
        raise ValueError("source archive hash differs")
    tables, extraction = extract_tables_from_ipa(args.ipa)
    if sha(tables) != reference["visual_tables"]["payload_sha256"]:
        raise ValueError("source visual-table hash differs")
    compiler = shutil.which(args.compiler)
    if compiler is None:
        raise RuntimeError("C++17 compiler unavailable")
    executable = (args.out / "terrain_probe").resolve()
    sources = [NATIVE / "src/Archive.cpp", NATIVE / "src/Terrain.cpp", NATIVE / "tools/terrain_probe.cpp"]
    command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wconversion", "-Werror", "-pedantic",
               "-I", NATIVE / "include", *sources, "-lz", "-o", executable]
    build = run(command)
    matches, rejections = [], []
    with tempfile.TemporaryDirectory(prefix="g1-native-terrain-", dir=args.out) as directory:
        work = Path(directory)
        archive_path, data_path, tables_path, grid_path = (work / name for name in
                                                         ("iphone.big", "resource.bin", "tables.bin", "grid.bin"))
        archive_path.write_bytes(archive)
        tables_path.write_bytes(tables)

        def check(kind, data, expected, resource_id=None):
            data_path.write_bytes(data)
            actual = json.loads(run([executable, kind, data_path]).stdout)
            independent = decode(kind, data)
            if actual != independent or sha(canonical(actual)) != expected["canonical_decode_sha256"]:
                raise RuntimeError(f"native terrain decode differs: {kind}/{resource_id}")
            if encode(kind, actual) != data:
                raise RuntimeError("native terrain result does not round-trip original bytes")
            if resource_id is not None:
                via_archive = json.loads(run([executable, kind, "--archive", archive_path,
                                               resource_id, expected["key_hex"]]).stdout)
                if via_archive != actual:
                    raise RuntimeError("native terrain archive lookup differs from direct input")
            matches.append({"kind": kind, "resource_id": resource_id, "payload_sha256": sha(data),
                            "canonical_decode_sha256": sha(canonical(actual)),
                            "all_fields_match": True, "byte_exact_round_trip": True})
            semantic = bytearray(data)
            if kind == "grid": semantic[2] = 32
            elif kind == "lookup": semantic[0] = 0
            elif kind == "rules": semantic[1] = 4
            else: semantic[0] = 32
            for label, malformed in (("last_byte_missing", data[:-1]), ("trailing_byte", data + b"\0"),
                                     ("unsupported_semantic_field", semantic)):
                data_path.write_bytes(malformed)
                attempt = run([executable, kind, data_path], check=False)
                if attempt.returncode != 1 or attempt.stdout or not attempt.stderr.startswith("ERROR:"):
                    raise RuntimeError("malformed original terrain resource was not rejected cleanly")
                rejections.append({"kind": kind, "resource_id": resource_id, "case": label,
                                   "error": attempt.stderr.strip()})

        for expected in reference["resources"]:
            data = payload(archive, expected)
            check(expected["kind"], data, expected, expected["resource_id"])
            if expected["resource_id"] == 268:
                grid_path.write_bytes(data)
        check("tables", tables, reference["visual_tables"])
        first = json.loads(run([executable, "mesh", grid_path, tables_path, "0"]).stdout)
        raw = [{k: v for k, v in quad.items() if k != "normalized_uv"} for quad in first["quads"]]
        original_mesh = reference["first_grid_original_geometry"]
        if len(raw) != original_mesh["quad_count"] or sha(canonical(raw)) != original_mesh["original_quad_sha256"]:
            raise RuntimeError("native first-grid mesh differs from original-instruction reference")
        first_mesh = {"resource_id": 268, "style": 0, "quads": len(raw),
                      "original_and_native_quad_sha256": sha(canonical(raw)),
                      "matches_original_instruction_reference": True}
    paths = [*sources, NATIVE / "include/g1/Terrain.hpp", NATIVE / "tests/terrain_test.cpp",
             NATIVE / "tests/terrain_cases.txt", NATIVE / "tests/generate_terrain_reference.py",
             NATIVE / "tests/verify_missions.py", NATIVE / "tools/extract_terrain_tables.py",
             Path(__file__).resolve(), reference_path]
    report = {"scope": "Native terrain resources and bounded first-mission interior mesh",
              "source_ipa_sha256": provenance["input_ipa_sha256"], "archive_sha256": sha(archive),
              "host": platform.platform(), "compiler": run([compiler, "--version"]).stdout.splitlines()[0],
              "compiler_target": run([compiler, "-dumpmachine"]).stdout.strip(),
              "build_command": list(map(str, command)), "build_stderr": build.stderr,
              "native_probe_sha256": sha(executable.read_bytes()),
              "source_files_sha256": {str(p.relative_to(NATIVE)): sha(p.read_bytes()) for p in paths},
              "counts_matched": counts, "source_matches": matches, "rejection_count": len(rejections),
              "malformed_input_rejections": rejections, "table_extraction_provenance": extraction,
              "first_grid_geometry": first_mesh, "all_passed": True,
              "original_instructions_executed_by_this_script": False,
              "original_instruction_reference_used": True, "guest_execution_in_native_product": False,
              "gpu_rendered": False, "ios_arm64_compiled": False, "device_tested": False,
              "limitations": reference["not_established"]}
    output = args.out / "native_terrain_validation.json"
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"report": str(output), "counts_matched": counts, "rejections": len(rejections),
                      "first_grid_quads": len(raw), "all_passed": True}))


if __name__ == "__main__":
    main()
