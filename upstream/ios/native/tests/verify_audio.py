#!/usr/bin/env python3
"""Compare native float PCM with libsndfile for every original Ogg file."""
import argparse
import ctypes as c
import ctypes.util
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

import numpy as np


class SoundFileInfo(c.Structure):
    _fields_ = [('frames', c.c_int64), ('samplerate', c.c_int),
                ('channels', c.c_int), ('format', c.c_int),
                ('sections', c.c_int), ('seekable', c.c_int)]


class ReferenceDecoder:
    """Development-only use of the documented libsndfile C API."""
    def __init__(self):
        path = ctypes.util.find_library('sndfile')
        if not path:
            raise RuntimeError('libsndfile is required for the independent decoder')
        self.lib = c.CDLL(path)
        signatures = {
            'sf_open': ([c.c_char_p, c.c_int, c.POINTER(SoundFileInfo)], c.c_void_p),
            'sf_readf_float': ([c.c_void_p, c.POINTER(c.c_float), c.c_int64], c.c_int64),
            'sf_error': ([c.c_void_p], c.c_int),
            'sf_close': ([c.c_void_p], c.c_int),
            'sf_version_string': ([], c.c_char_p),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.lib, name)
            function.argtypes, function.restype = arguments, result
        self.version = self.lib.sf_version_string().decode('utf8')

    def read(self, path):
        info = SoundFileInfo()
        stream = self.lib.sf_open(str(path).encode(), 0x10, c.byref(info))  # SFM_READ
        if not stream:
            raise ValueError('independent decoder could not open Ogg stream')
        try:
            if info.format != 0x200060 or not (0 < info.frames <= 16 * 1024 * 1024):
                raise ValueError('reference is not bounded Ogg Vorbis')
            if info.channels not in (1, 2):
                raise ValueError('unsupported reference channel count')
            samples = np.empty(info.frames * info.channels, dtype=np.float32)
            count = self.lib.sf_readf_float(stream, samples.ctypes.data_as(c.POINTER(c.c_float)), info.frames)
            extra = (c.c_float * info.channels)()
            if (count != info.frames or self.lib.sf_readf_float(stream, extra, 1) != 0
                    or self.lib.sf_error(stream) != 0):
                raise ValueError('independent decoder did not match its exact frame count')
            return info.samplerate, info.channels, samples
        finally:
            self.lib.sf_close(stream)


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
    parser.add_argument('--ffmpeg', default='ffmpeg')
    args = parser.parse_args()
    reference = json.loads(Path(__file__).with_name('resource_reference.json').read_text())
    if sha(args.ipa.read_bytes()) != reference['input']['sha256']:
        raise ValueError('input does not match the reviewed source IPA')
    with zipfile.ZipFile(args.ipa) as source:
        archive = source.read('Payload/G1awakening.app/iphone.big')
    if sha(archive) != reference['archive']['sha256']:
        raise ValueError('archive differs from the reviewed data')

    # Different conforming float Vorbis decoders may round differently. Set
    # tolerances before measuring: one PCM16 step peak, one-quarter step RMS.
    peak_limit, rms_limit = 1 / 32768, 1 / 131072
    decoder = ReferenceDecoder()
    results = []
    with tempfile.TemporaryDirectory(prefix='g1-vorbis-') as tmp:
        work = Path(tmp)
        big = work / 'iphone.big'
        big.write_bytes(archive)
        extracted = work / 'resources'
        run([args.build / 'g1_asset_probe', big, extracted])
        native_path, reference_path = work / 'native.f32le', work / 'ffmpeg.f32le'
        for entry in reference['resources']:
            if entry['kind'] != 'ogg':
                continue
            source_path = extracted / f"{entry['index']:04d}.bin"
            if sha(source_path.read_bytes()) != entry['sha256']:
                raise ValueError(f"Ogg source hash mismatch: {entry['index']}")
            native = run([args.build / 'audio_probe', source_path, native_path])
            sample_rate, channels, frames = map(int, native.stdout.split())
            reference_rate, reference_channels, expected = decoder.read(source_path)
            if (reference_rate, reference_channels) != (sample_rate, channels):
                raise ValueError('native/reference audio format mismatch')
            # Keep the initial FFmpeg comparison reproducible. Its CLI path
            # emits packet padding on some of these old files. It is reported
            # separately, without trimming it or weakening the exact count
            # comparison with libsndfile's Vorbis-file decoder.
            run([args.ffmpeg, '-v', 'error', '-nostdin', '-y', '-i', source_path,
                 '-map', '0:a:0', '-f', 'f32le', '-acodec', 'pcm_f32le', reference_path])
            actual_bytes = native_path.read_bytes()
            expected_bytes = expected.astype('<f4', copy=False).tobytes()
            ffmpeg_bytes = reference_path.read_bytes()
            actual = np.frombuffer(actual_bytes, dtype='<f4')
            if actual.size != frames * channels or actual.shape != expected.shape:
                raise ValueError(f"sample-count mismatch at resource {entry['index']}: "
                                 f"native {actual.size}, libsndfile {expected.size}")
            if not np.isfinite(actual).all() or not np.isfinite(expected).all():
                raise ValueError('decoder produced nonfinite PCM')
            error = actual.astype(np.float64) - expected.astype(np.float64)
            peak = float(np.max(np.abs(error)))
            rms = float(np.sqrt(np.mean(error * error)))
            row = {'resource_index': entry['index'],
                   'resource_id': entry['group_id'] + entry['group_ordinal'],
                   'source_sha256': entry['sha256'], 'sample_rate': sample_rate,
                   'channels': channels, 'frames': frames, 'seconds': frames / sample_rate,
                   'native_pcm_sha256': sha(actual_bytes),
                   'libsndfile_pcm_sha256': sha(expected_bytes),
                   'ffmpeg_cli_frames': len(ffmpeg_bytes) // (4 * channels),
                   'ffmpeg_cli_pcm_sha256': sha(ffmpeg_bytes),
                   'maximum_absolute_difference': peak, 'rms_difference': rms}
            if peak > peak_limit or rms > rms_limit:
                raise ValueError(f"PCM difference exceeds declared tolerance: {row}")
            results.append(row)

    report = {
        'source_ipa_sha256': reference['input']['sha256'],
        'archive_sha256': reference['archive']['sha256'],
        'ffmpeg_version': run([args.ffmpeg, '-version']).stdout.splitlines()[0],
        'reference_decoder': decoder.version,
        'reference_api': 'https://libsndfile.github.io/libsndfile/api.html',
        'ffmpeg_cli_exact_length_matches': sum(row['ffmpeg_cli_frames'] == row['frames'] for row in results),
        'ffmpeg_cli_role': 'Secondary diagnostic only; initial direct comparison exposed trailing packet padding',
        'numpy_version': np.__version__,
        'native_decoder': 'stb_vorbis 1.22',
        'native_decoder_upstream_commit': '003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5',
        'exact_sample_count_required': True,
        'peak_difference_limit': peak_limit, 'rms_difference_limit': rms_limit,
        'ogg_files_compared': len(results),
        'pcm_samples_compared': sum(row['frames'] * row['channels'] for row in results),
        'maximum_absolute_difference': max(row['maximum_absolute_difference'] for row in results),
        'maximum_file_rms_difference': max(row['rms_difference'] for row in results),
        'result': 'passed', 'audio': results,
        'scope': ('Native Vorbis data decoding only. Does not verify event selection, '
                  'mixing, output-device playback, timing, loop policy, or original '
                  'decoder bit identity.'),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'audio'}))


if __name__ == '__main__':
    main()
