#!/usr/bin/env python3
"""Compare native straight RGBA8 output against Pillow for every original PNG."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

from PIL import Image, __version__ as pillow_version


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(command):
    return subprocess.run(list(map(str, command)), check=True, capture_output=True,
                          text=True, timeout=60)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ipa', type=Path)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    reference = json.loads(Path(__file__).with_name('resource_reference.json').read_text())
    if sha(args.ipa.read_bytes()) != reference['input']['sha256']:
        raise ValueError('input does not match the reviewed source IPA')
    with zipfile.ZipFile(args.ipa) as source:
        archive = source.read('Payload/G1awakening.app/iphone.big')
    if sha(archive) != reference['archive']['sha256']:
        raise ValueError('archive differs from the reviewed data')
    results = []
    with tempfile.TemporaryDirectory(prefix='g1-rgba-') as tmp:
        work = Path(tmp)
        big = work / 'iphone.big'
        big.write_bytes(archive)
        extracted = work / 'resources'
        run([args.build / 'g1_asset_probe', big, extracted])
        rgba_path = work / 'pixels.rgba'
        for entry in reference['resources']:
            if entry['kind'] != 'png':
                continue
            source_path = extracted / f"{entry['index']:04d}.bin"
            if sha(source_path.read_bytes()) != entry['sha256']:
                raise ValueError(f"PNG source hash mismatch: {entry['index']}")
            native = run([args.build / 'image_probe', source_path, rgba_path])
            width, height = map(int, native.stdout.split())
            with Image.open(source_path) as image:
                reference_rgba = image.convert('RGBA')
                if reference_rgba.size != (width, height):
                    raise ValueError('PNG dimensions disagree')
                expected = reference_rgba.tobytes()
            actual = rgba_path.read_bytes()
            if actual != expected:
                raise ValueError(f"native/Pillow pixel mismatch: {entry['index']}")
            results.append({'resource_index': entry['index'], 'width': width, 'height': height,
                            'source_sha256': entry['sha256'], 'rgba_sha256': sha(actual),
                            'rgba_bytes': len(actual)})
    report = {'source_ipa_sha256': reference['input']['sha256'],
              'pillow_version': pillow_version, 'pngs_compared': len(results),
              'rgba_bytes_compared': sum(row['rgba_bytes'] for row in results),
              'result': 'passed', 'images': results,
              'scope': 'Native PNG pixel decoding; does not verify GPU rendering or device execution'}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'images'}))


if __name__ == '__main__':
    main()
