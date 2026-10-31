#!/usr/bin/env python3
"""Import the exact owner-supplied iOS 017R1 resources into an Android build.

No game logic, media transcoding, palette conversion or atlas rewriting occurs.
The original executable, signing material and user data are never imported.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

SOURCE_SHA = "b035acfe0c08aafa715c54f60a59423191046c271328abbca4cd72689dfaed46"
ARCHIVE_SHA = "663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4"
TABLES_SHA = "b870e3d74244741003c8068178e7f58bb5fbe35599599c918e024227463725a8"
PREFIX = "Payload/G1AwakeningNative.app/"

def sha(data):
    return hashlib.sha256(data).hexdigest()

def import_assets(source, output):
    source_bytes = source.read_bytes()
    if sha(source_bytes) != SOURCE_SHA:
        raise ValueError("Input is not the pinned iOS 017R1 package; refusing an unverified resource revision")
    entries = []
    with zipfile.ZipFile(source) as archive:
        bad = archive.testzip()
        if bad:
            raise ValueError("Corrupt IPA entry: " + bad)
        for info in archive.infolist():
            if not info.filename.startswith(PREFIX) or info.is_dir():
                continue
            relative = info.filename[len(PREFIX):]
            permitted = relative.startswith(("GameData/", "CleanTextures017/", "Licenses/")) or relative in (
                "G1OpeningXR.mp4", "G1ResourceRevision.json", "Icon-60@3x.png")
            if not permitted:
                continue
            path = PurePosixPath(relative)
            if path.is_absolute() or ".." in path.parts or "\\" in relative:
                raise ValueError("Unsafe archive member")
            data = archive.read(info)
            dest = output.joinpath(*path.parts)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            entries.append({"path": relative, "size": len(data), "sha256": sha(data)})
    found = {item["path"]: item for item in entries}
    for name, expected in (("GameData/iphone.big", ARCHIVE_SHA), ("GameData/terrain-tables.bin", TABLES_SHA)):
        if found.get(name, {}).get("sha256") != expected:
            raise ValueError("Required original game data mismatch: " + name)
    if not found.get("G1OpeningXR.mp4") or sum(item["path"].endswith(".png") and item["path"].startswith("CleanTextures017/") for item in entries) != 88:
        raise ValueError("Incomplete iOS intro/remaster package")
    manifest = {"schema": 1, "source_ipa_sha256": SOURCE_SHA,
        "ios_source_commit": "be203a911bc26313615ea450758413e5c9cf6462",
        "resource_revision": "017R1", "files": sorted(entries, key=lambda item: item["path"])}
    output.mkdir(parents=True, exist_ok=True)
    (output / "asset-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipa", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "app/src/main/assets/game")
    args = parser.parse_args()
    result = import_assets(args.ipa, args.output)
    print(json.dumps({"source_sha256": SOURCE_SHA, "files": len(result["files"]),
        "bytes": sum(item["size"] for item in result["files"]), "output": str(args.output)}))

if __name__ == "__main__":
    main()
