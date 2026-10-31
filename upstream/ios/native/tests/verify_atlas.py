#!/usr/bin/env python3
"""Build native atlas decoding and compare every original descriptor to its oracle.

Requires the user's audited original IPA locally. No game payload is bundled
with this repository. This is a native CPU metadata/geometry test, not a GPU
render, a full native game or an iPhone performance result.
"""
import argparse
import json
from pathlib import Path
import platform
import shutil
import tempfile
import zipfile

from generate_atlas_reference import canonical, decode, encode, sha
from verify_missions import payload, run

NATIVE = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("ipa", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--compiler", default="g++")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    reference_path = NATIVE / "tests/atlas_reference.json"
    reference = json.loads(reference_path.read_text())
    provenance = reference["provenance"]
    if reference["reference_schema_version"] != 1 or provenance["expectations_derived_from_cpp_output"]:
        raise RuntimeError("invalid atlas reference provenance")
    if len(reference["descriptors"]) != 38:
        raise RuntimeError("atlas reference corpus incomplete")
    if sha(args.ipa.read_bytes()) != provenance["input_ipa_sha256"]:
        raise RuntimeError("input IPA differs from the audited original")
    with zipfile.ZipFile(args.ipa) as original:
        entries = [e for e in original.infolist() if e.filename == provenance["archive_member"]]
        if len(entries) != 1 or entries[0].file_size != provenance["archive_size"]:
            raise RuntimeError("archive identity/size differs from reference")
        archive = original.read(entries[0])
    if sha(archive) != provenance["archive_sha256"]:
        raise RuntimeError("original archive hash differs from reference")
    compiler = shutil.which(args.compiler)
    if compiler is None:
        raise RuntimeError("C++ compiler unavailable")
    executable = (args.out / "atlas_probe").resolve()
    sources = [NATIVE / "src/Archive.cpp", NATIVE / "src/Atlas.cpp", NATIVE / "tools/atlas_probe.cpp"]
    command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wconversion", "-Werror",
               "-I", NATIVE / "include", *sources, "-lz", "-o", executable]
    build = run(command)
    matches, rejections = [], []
    with tempfile.TemporaryDirectory(prefix="g1-native-atlas-", dir=args.out) as temporary:
        work = Path(temporary)
        archive_path = work / "iphone.big"
        archive_path.write_bytes(archive)
        descriptor_path = work / "descriptor.bin"
        for expected in reference["descriptors"]:
            data = payload(archive, expected)
            descriptor_path.write_bytes(data)
            separately_decoded = decode(data)
            actual = json.loads(run([executable, descriptor_path]).stdout)
            via_archive = json.loads(run([executable, "--archive", archive_path,
                                          expected["resource_id"], expected["key_hex"]]).stdout)
            if (actual != separately_decoded or actual != via_archive or
                    sha(canonical(actual)) != expected["canonical_decode_and_quad_sha256"] or
                    encode(actual) != data):
                raise RuntimeError(f"atlas parity mismatch at resource {expected['resource_id']}")
            matches.append({"resource_id": expected["resource_id"], **expected["counts"],
                            "source_sha256": sha(data), "decode_and_quad_sha256": sha(canonical(actual)),
                            "all_fields_match": True, "all_frame_quads_match_python_transcription": True,
                            "archive_lookup_matches": True, "byte_exact_round_trip": True,
                            "empty_descriptor_path_unresolved": expected["empty_descriptor_path_unresolved"]})
            for label, malformed in (("truncated_header", data[:7]), ("truncated_last_byte", data[:-1]),
                                     ("trailing_byte", data + b"\0")):
                descriptor_path.write_bytes(malformed)
                attempt = run([executable, descriptor_path], check=False)
                if attempt.returncode != 1 or attempt.stdout or not attempt.stderr.startswith("ERROR:"):
                    raise RuntimeError("malformed original atlas not rejected cleanly")
                rejections.append({"resource_id": expected["resource_id"], "case": label,
                                   "error": attempt.stderr.strip()})
    paths = [*sources, NATIVE / "include/g1/Atlas.hpp", NATIVE / "include/g1/Archive.hpp",
             NATIVE / "tests/atlas_test.cpp", NATIVE / "tests/generate_atlas_reference.py",
             NATIVE / "tests/verify_missions.py", Path(__file__).resolve(), reference_path]
    evidence = {"scope": "Native C++17 sprite descriptor and raw quantized quad geometry",
                "source_ipa_sha256": provenance["input_ipa_sha256"], "archive_sha256": sha(archive),
                "host": platform.platform(), "compiler": run([compiler, "--version"]).stdout.splitlines()[0],
                "compiler_target": run([compiler, "-dumpmachine"]).stdout.strip(),
                "build_command": list(map(str, command)), "build_stderr": build.stderr,
                "executable_sha256": sha(executable.read_bytes()),
                "source_files_sha256": {str(p.relative_to(NATIVE)): sha(p.read_bytes()) for p in paths},
                "reference_provenance": provenance, "counts_matched": reference["expected_counts"],
                "source_matches": matches, "malformed_input_rejections": rejections,
                "rejection_count": len(rejections), "all_passed": True,
                "original_machine_code_executed": False, "gpu_rendered": False,
                "ios_arm64_compiled": False, "device_tested": False,
                "limitations": reference["not_established"]}
    out = args.out / "native_atlas_validation.json"
    out.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps({"report": str(out), "all_passed": True,
                      "counts_matched": reference["expected_counts"], "rejection_count": len(rejections)}))


if __name__ == "__main__":
    main()
