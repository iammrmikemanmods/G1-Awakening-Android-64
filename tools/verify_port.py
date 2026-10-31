#!/usr/bin/env python3
"""Verify pinned source/resources and optional APK without third-party Python packages."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_path(value):
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or "\\" in value or not path.parts:
        raise ValueError("Unsafe manifest path: " + value)
    return path


def verify_source():
    manifest = json.loads((ROOT / "SOURCE_PROVENANCE.json").read_text())
    expected = set()
    for item in manifest["files"]:
        path = safe_path(item["path"])
        if str(path) in expected:
            raise ValueError("Duplicate source path: " + str(path))
        expected.add(str(path))
        data = (ROOT / path).read_bytes()
        if len(data) != item["size"] or digest(data) != item["sha256"]:
            raise ValueError("Pinned source modified: " + str(path))
    actual = {str(p.relative_to(ROOT)) for p in (ROOT / "upstream/ios").rglob("*") if p.is_file()}
    if actual != expected:
        raise ValueError("Unmanifested/missing upstream files: " + str(sorted(actual ^ expected)))
    return {"files": len(expected), "commit": manifest["ios_source_commit"], "byte_identical": True}


def verify_assets(required):
    root = ROOT / "app/src/main/assets/game"
    path = root / "asset-manifest.json"
    if not path.exists():
        if required:
            raise ValueError("Import the pinned iOS017R1 package before building a playable APK")
        return None
    manifest = json.loads(path.read_text())
    pinned = json.loads((ROOT / "validation/ios-resource-manifest.json").read_text())
    if manifest != pinned:
        raise ValueError("Local resource manifest differs from the pinned 017R1 inventory")
    if manifest["source_ipa_sha256"] != "b035acfe0c08aafa715c54f60a59423191046c271328abbca4cd72689dfaed46":
        raise ValueError("Unexpected source resource package")
    paths = set()
    for item in manifest["files"]:
        relative = safe_path(item["path"])
        if str(relative) in paths:
            raise ValueError("Duplicate asset path")
        paths.add(str(relative))
        data = (root / relative).read_bytes()
        if len(data) != item["size"] or digest(data) != item["sha256"]:
            raise ValueError("Changed source asset: " + str(relative))
    actual = {str(p.relative_to(root)) for p in root.rglob("*") if p.is_file()}
    if actual != paths | {"asset-manifest.json"}:
        raise ValueError("Unexpected or missing packaged game files")
    if len(paths) != 98 or sum(p.startswith("CleanTextures017/") and p.endswith(".png") for p in paths) != 88:
        raise ValueError("Incomplete original resource set")
    return manifest


def elf_info(data, abi):
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("Expected a little-endian ELF64 library")
    machine = struct.unpack_from("<H", data, 18)[0]
    if machine != {"arm64-v8a": 183, "x86_64": 62}[abi]:
        raise ValueError("ELF architecture does not match APK ABI directory")
    table = struct.unpack_from("<Q", data, 32)[0]
    entry_size, count = struct.unpack_from("<HH", data, 54)
    alignments = []
    for i in range(count):
        offset = table + i * entry_size
        if struct.unpack_from("<I", data, offset)[0] == 1:
            alignment = struct.unpack_from("<Q", data, offset + 48)[0]
            if alignment < 16384:
                raise ValueError("Native LOAD segment is not aligned for 16 KB pages")
            alignments.append(alignment)
    if not alignments:
        raise ValueError("Native library contains no loadable segments")
    return {"abi": abi, "elf_bits": 64, "machine": machine, "load_segment_alignment": alignments}


def verify_apk(path, assets, expected_abi):
    if assets is None:
        raise ValueError("APK verification requires the complete original asset manifest")
    with zipfile.ZipFile(path) as apk:
        bad = apk.testzip()
        if bad:
            raise ValueError("APK CRC failed: " + bad)
        names = apk.namelist()
        if len(names) != len(set(names)):
            raise ValueError("Duplicate APK ZIP entries")
        embedded = json.loads(apk.read("assets/game/asset-manifest.json"))
        if embedded != assets:
            raise ValueError("APK resource provenance differs from the imported manifest")
        expected = {"assets/game/asset-manifest.json"}
        for item in assets["files"]:
            name = "assets/game/" + item["path"]
            expected.add(name)
            data = apk.read(name)
            if len(data) != item["size"] or digest(data) != item["sha256"]:
                raise ValueError("APK changed an original resource: " + item["path"])
        actual = {name for name in names if name.startswith("assets/game/") and not name.endswith("/")}
        if actual != expected:
            raise ValueError("APK game asset inventory differs from the pinned package")
        libraries = []
        for name in names:
            if not name.startswith("lib/") or not name.endswith(".so"):
                continue
            parts = PurePosixPath(name).parts
            if len(parts) != 3 or parts[1] not in ("arm64-v8a", "x86_64") or parts[2] != "libg1awakening.so":
                raise ValueError("Unexpected packaged native library: " + name)
            libraries.append(elf_info(apk.read(name), parts[1]))
        if not libraries or (expected_abi and {x["abi"] for x in libraries} != {expected_abi}):
            raise ValueError("APK does not contain exactly the requested native ABI")
    return {"file": path.name, "size": path.stat().st_size, "sha256": digest(path.read_bytes()),
            "asset_files_byte_identical": len(assets["files"]), "native_libraries": libraries,
            "note": "Signature, min/target SDK and ZIP alignment are checked with Android build-tools separately."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-assets", action="store_true")
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--abi", choices=("arm64-v8a", "x86_64"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = {"source": verify_source()}
    assets = verify_assets(args.require_assets or bool(args.apk))
    report["resources"] = None if assets is None else {"files": len(assets["files"]),
        "bytes": sum(item["size"] for item in assets["files"]), "revision": assets["resource_revision"],
        "source_ipa_sha256": assets["source_ipa_sha256"], "byte_identical": True}
    if args.apk:
        report["apk"] = verify_apk(args.apk, assets, args.abi)
    value = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(value)
    print(value, end="")


if __name__ == "__main__":
    main()
