# Native original-audio decoding

The portable `Audio` module decodes the original archive's Ogg Vorbis resources
to interleaved float PCM, preserving channel count, sample rate and the declared
number of frames. It does not resample the input, choose game events, mix voices,
set loop policy, or drive an output device. Those are separate game and platform
responsibilities. This component alone does not establish working game audio.

The product dependency is the unmodified `stb_vorbis.c` version 1.22 from official
upstream commit `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5`. The exact source blob,
SHA256, full license and provenance are preserved in `third_party/stb`. It is
compiled as native C++17. No original executable or legacy audio library is
loaded. Input, format and allocation bounds are checked before decoding; short
streams, inconsistent sample counts and nonfinite output cause explicit errors.

## Actual source-corpus check

`tests/verify_audio.py` authenticates the original IPA and archive, extracts each
resource through the native archive reader, authenticates every encoded Ogg,
and compares decoded PCM with the independently installed libsndfile 1.2.2
Vorbis-file decoder. The libsndfile C API is documented at
<https://libsndfile.github.io/libsndfile/api.html>. This development dependency is
not linked into the iOS product.

The resulting `audio_decode_validation.json` records all 24 original files,
7,261,392 mono frames, exact per-file frame counts, 22,050 Hz sample rates,
PCM hashes and numerical differences. The largest absolute difference was
`3.5762786865234375e-7`; the largest per-file RMS difference was
`3.8048885895845966e-8`. The declared tolerances, set before comparison, were
one PCM16 step (`1/32768`) peak and one-quarter step (`1/131072`) RMS. This is a
floating-point decoder agreement check, not a claim of identity with the old
game's decoder or audible playback on an iPhone.

The first attempted reference was FFmpeg 6.1.1's direct float-PCM CLI output.
That attempt correctly failed the exact-length requirement on archive entry98:
native output had 8,676 frames and FFmpeg emitted 8,704. The independent
libsndfile decoder agreed with 8,676. The reproducible verifier keeps FFmpeg's
unmodified output hashes and lengths as secondary diagnostics: 18 of the 24
lengths match, while six CLI outputs include additional final packet samples.
No padding is silently trimmed, no frame-count requirement is weakened, and
FFmpeg is not the acceptance reference for those lengths.

Run from the repository root after building native host tools:

```bash
python3 native/tests/verify_audio.py /local/path/G1awakening.ipa \
  --build build/native --out /local/path/audio_decode_validation.json
```

This requires NumPy, libsndfile and FFmpeg on the development host. The original
IPA and decoded audio remain local; only metadata, implementation and the
reproducer belong in Git. The small CTest `vorbis_format_contract` additionally
checks rejection of malformed headers. Apple compilation and actual output
playback require their own evidence.
