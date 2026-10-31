#!/usr/bin/env python3
"""Build native mission decoding and check it against a compact independent oracle.

Uses Python standard-library ZIP/zlib extraction and a supplied local original
IPA. No research tree, guest CPU, compatibility runtime, or original payload
in the repository is required. This is not a gameplay or iOS device test.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import tempfile
import zipfile
import zlib

NATIVE = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(command, check=True):
    return subprocess.run(list(map(str, command)), check=check, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)


def canonical_bytes(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def payload(archive, reference):
    offset, size = reference["archive_offset"], reference["stored_size"]
    if not 0 <= offset <= len(archive) or not 4 <= size <= len(archive) - offset:
        raise RuntimeError("reference resource span exceeds archive")
    member = archive[offset:offset + size]
    flags = struct.unpack_from("<I", member)[0]
    if flags == 4 and reference["compression"] == "raw":
        result = member[4:]
    elif flags == 0x800004 and reference["compression"] == "zlib" and len(member) >= 12:
        expanded, stored = struct.unpack_from("<II", member, 4)
        if expanded != reference["size"] or stored != len(member) - 12:
            raise RuntimeError("original compressed member sizes differ from reference")
        decoder = zlib.decompressobj()
        result = decoder.decompress(member[12:], expanded + 1)
        if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
            raise RuntimeError("original compressed member has incomplete or trailing data")
    else:
        raise RuntimeError("original member flags differ from audited reference")
    if len(result) != reference["size"] or digest(result) != reference["payload_sha256"]:
        raise RuntimeError("independently extracted original payload hash differs from reference")
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("ipa", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--compiler", default="g++")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    reference_path = NATIVE / "tests/mission_reference.json"
    reference = json.loads(reference_path.read_text())
    provenance = reference["provenance"]
    expected_counts = {"mission_streams": 30, "instructions": 803, "argument_words": 2800}
    if reference["reference_schema_version"] != 1 or reference["expected_counts"] != expected_counts:
        raise RuntimeError("reference version/counts differ from reviewed original-data corpus")
    if provenance["expectations_derived_from_native_code"]:
        raise RuntimeError("native-derived expectations are not an independent oracle")
    streams = reference["streams"]
    if (len(streams) != 30 or sum(s["instruction_count"] for s in streams) != 803 or
            sum(s["argument_word_count"] for s in streams) != 2800 or
            {s["resource_id"] for s in streams} != set(range(310, 340))):
        raise RuntimeError("reference stream identities/counts are incomplete")
    source_hash = digest(args.ipa.read_bytes())
    if source_hash != provenance["input_ipa_sha256"]:
        raise RuntimeError("input IPA differs from the audited source")
    with zipfile.ZipFile(args.ipa) as original:
        entries = [e for e in original.infolist() if e.filename == provenance["archive_member"]]
        if len(entries) != 1 or entries[0].file_size != provenance["archive_size"]:
            raise RuntimeError("original archive member identity/size differs from reference")
        archive = original.read(entries[0])
    if digest(archive) != provenance["archive_sha256"]:
        raise RuntimeError("original FGIB hash differs from reference")
    compiler = shutil.which(args.compiler)
    if compiler is None:
        raise RuntimeError(f"compiler unavailable: {args.compiler}")
    executable = (args.out / "mission_probe").resolve()
    sources = [NATIVE / "src/Archive.cpp", NATIVE / "src/Mission.cpp", NATIVE / "tools/mission_probe.cpp"]
    command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wconversion", "-Werror",
               "-I", NATIVE / "include", *sources, "-lz", "-o", executable]
    build = run(command)
    matches, rejections = [], []
    with tempfile.TemporaryDirectory(prefix="g1-native-missions-", dir=args.out) as temporary:
        work = Path(temporary)
        archive_path, payload_path, bad_path = (work / name for name in ("iphone.big", "mission.bin", "malformed.bin"))
        archive_path.write_bytes(archive)
        originals = {}

        def reject(name, data):
            bad_path.write_bytes(data)
            attempt = run([executable, bad_path], check=False)
            if attempt.returncode != 1 or not attempt.stderr.startswith("ERROR:") or attempt.stdout:
                raise RuntimeError(f"malformed input not rejected cleanly: {name}: {attempt}")
            rejections.append({"case": name, "exit_code": attempt.returncode, "error": attempt.stderr.strip()})

        def replace_word(data, offset, value):
            changed = bytearray(data)
            struct.pack_into(">H", changed, offset, value)
            return bytes(changed)

        for expected in streams:
            resource_id = expected["resource_id"]
            data = payload(archive, expected)
            originals[resource_id] = data
            offsets = expected["instruction_offsets"]
            if (len(offsets) != expected["instruction_count"] or not offsets or offsets[0] != 2 or
                    offsets != sorted(set(offsets)) or any(x % 2 or not 2 <= x < len(data) for x in offsets)):
                raise RuntimeError("reference instruction boundaries are invalid")
            payload_path.write_bytes(data)
            direct = json.loads(run([executable, payload_path]).stdout)
            archive_result = json.loads(run([executable, "--archive", archive_path, resource_id, expected["key_hex"]]).stdout)
            actual_hash = digest(canonical_bytes(direct))
            if direct != archive_result or actual_hash != expected["canonical_decode_sha256"]:
                raise RuntimeError(f"native decode differs from independent original-data oracle: {resource_id}")
            if (direct["version"] != expected["version"] or
                    direct["declared_instruction_count"] != expected["instruction_count"] or
                    len(direct["commands"]) != expected["instruction_count"] or
                    sum(len(c["argument_words_u16"]) for c in direct["commands"]) != expected["argument_word_count"]):
                raise RuntimeError(f"native count fields differ from reference: {resource_id}")
            encoded = bytearray([direct["version"], direct["declared_instruction_count"]])
            for item in direct["commands"]:
                values = [item["opcode"], *item["argument_words_u16"]]
                encoded += struct.pack(">" + "H" * len(values), *values)
            if encoded != data:
                raise RuntimeError(f"native result does not reproduce original source bytes: {resource_id}")
            matches.append({"resource_id": resource_id, "resource_key": expected["key_hex"],
                            "member_index": expected["member_index"], "size": len(data),
                            "source_sha256": digest(data), "instruction_count": len(direct["commands"]),
                            "argument_word_count": expected["argument_word_count"], "decoded_json_sha256": actual_hash,
                            "standalone_matches": True, "archive_lookup_matches": True, "byte_exact_round_trip": True})
            for index, offset in enumerate(offsets):
                reject(f"id{resource_id}_truncated_at_instruction_{index}", data[:offset])
            reject(f"id{resource_id}_truncated_last_argument", data[:-2])

        sample = originals[310]
        reject("empty", b"")
        reject("one_byte_header", sample[:1])
        reject("unsupported_version", bytes([2]) + sample[1:])
        reject("partial_word", sample[:-1])
        reject("header_count_zero_with_body", bytes([1, 0]) + sample[2:])
        reject("header_count_too_small", bytes([1, sample[1] - 1]) + sample[2:])
        reject("header_count_too_large", bytes([1, sample[1] + 1]) + sample[2:])
        reject("trailing_whole_word", sample + bytes(2))
        reject("unobserved_opcode_11", replace_word(sample, 2, 11))
        reject("unknown_opcode_65535", replace_word(sample, 2, 65535))
        for mutation in reference["source_data_mutations"]:
            reject(mutation["case"], replace_word(originals[mutation["resource_id"]],
                                                  mutation["word_byte_offset"], mutation["replacement_u16"]))
        for label, resource_id, key in (("wrong_key", "310", "0"),
                                       ("missing_id", "4294967295", "0xf4e02223"),
                                       ("overflow_id", "4294967296", "0xf4e02223"),
                                       ("negative_id", "-1", "0xf4e02223")):
            attempt = run([executable, "--archive", archive_path, resource_id, key], check=False)
            if attempt.returncode != 1 or not attempt.stderr.startswith("ERROR:") or attempt.stdout:
                raise RuntimeError(f"invalid archive lookup accepted: {label}")
            rejections.append({"case": "lookup_" + label, "exit_code": attempt.returncode, "error": attempt.stderr.strip()})
    if len(rejections) != 855:
        raise RuntimeError("malformed-input coverage differs from reviewed case count")
    source_files = [*sources, NATIVE / "include/g1/Archive.hpp", NATIVE / "include/g1/Mission.hpp",
                    NATIVE / "CMakeLists.txt", Path(__file__).resolve(), reference_path]
    evidence = {
        "scope": "Native C++17 mission framing only; no guest CPU or compatibility runtime",
        "source_ipa_sha256": source_hash, "archive_sha256": digest(archive),
        "compact_reference_sha256": digest(reference_path.read_bytes()), "reference_provenance": provenance,
        "source_files_sha256": {str(p.relative_to(NATIVE)): digest(p.read_bytes()) for p in source_files},
        "host": platform.platform(), "compiler": run([compiler, "--version"]).stdout.splitlines()[0],
        "compiler_target": run([compiler, "-dumpmachine"]).stdout.strip(),
        "build_command": list(map(str, command)), "build_stderr": build.stderr,
        "executable_sha256": digest(executable.read_bytes()),
        "mission_streams_matched": len(matches), "instructions_matched": 803, "argument_words_matched": 2800,
        "opcode_counts": reference["opcode_counts"],
        "observed_layout_subtype_counts": reference["observed_layout_subtype_counts"],
        "source_matches": matches, "malformed_input_rejections": rejections,
        "rejection_count": len(rejections), "all_passed": True,
        "ios_arm64_compiled": False, "gameplay_implemented": False, "gameplay_tested": False,
        "original_machine_code_executed": False, "device_tested": False,
        "limitations": ["Opcode behavior is not implemented or proven by framing tests.",
                        "Thirty recovered streams do not establish thirty campaign levels.",
                        "Unobserved opcode 11 and unknown subtypes are rejected.",
                        "Host C++ compilation is not an Apple SDK build or iPhone XR performance test."]}
    (args.out / "native_mission_validation.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps({key: evidence[key] for key in ("compiler_target", "mission_streams_matched", "instructions_matched",
          "argument_words_matched", "rejection_count", "all_passed", "ios_arm64_compiled", "gameplay_implemented")}, indent=2))


if __name__ == "__main__":
    main()
