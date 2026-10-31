# Native mission stream decoder

## What this implements

`g1::MissionStream` is a native C++17 decoder for the mission resource framing
recovered from the supplied G1 Awakening iOS 1.1.3 IPA. It produces command
records with the original opcode, byte offset, byte length, and unsigned
16-bit argument words. The existing native FGIB reader can resolve a resource
by its numeric ID and key and pass the decompressed bytes directly to this
decoder.

The implementation uses no guest CPU, compatibility layer, UIKit emulation,
or original executable instructions. It is intended as a source module for
the native rebuild. It does **not** execute commands, implement mission rules,
or make the game playable. A portable C++ source implementation and a Linux
host build do not establish that an ARM64 iOS executable has been built.

## Source evidence and format

The audited IPA SHA256 is
`b24fbe5cc4d5cb603189a476c4f81dd2a6ecc18b025eae913262055a7fc443fd`.
Its `iphone.big` SHA256 is
`663d2888982093ce66b7811ccb7a6ad3acd37fc02ffa85144775b88c39e83cb4`.
Mission resources have derived numeric IDs **310–339**, key **0xf4e02223**,
and archive member indices **55–84**. These are archive identities, not
recovered original filenames. Their uncompressed lengths range from 140 to
400 bytes. Thirty resource streams do not establish thirty campaign levels.

The empirical format was documented in the original research checkpoint's
`research/assets/AUDIT.md` and decoded by its independent Python tool
`research/assets/inspect_game_data.py`. This native package retains compact
reference metadata and decode hashes derived from that audit in
`tests/mission_reference.json`; it does not require the research tree to test
the decoder against a locally supplied IPA.
The header is one byte containing version 1 and one byte containing the
instruction count. Every subsequent opcode and argument is a big-endian u16.

| Opcode | Framing of arguments |
|---|---|
| 0, 1, 2, 13, 14 | No argument words |
| 5, 8, 10, 16 | One word |
| 3, 9, 18, 19, 20 | Two words |
| 4 | Three words |
| 6 | Five words |
| 15 | Count followed by that many one-word records |
| 17 | Count followed by that many three-word records |
| 21 | Count followed by that many four-word records |
| 12 | Leading subtype selects total argument length: 0 → 4, 1 → 6, 2 → 3, 3 → 5 words |
| 7 | Site count; each site has two words followed by a group count; each group has its own pair count followed by that many two-word pairs |

All unsigned argument values, including `0xffff` and `0xfffe`, are preserved.
The decoder does not reinterpret these values as signed integers, unit IDs,
coordinates, text references, or runtime flags. Those meanings need separate
evidence from the original native implementation.

The original corpus covers every supported opcode and opcode-12 subtype. Opcode 7
contains 69 zero-group, four one-group, and one two-group records; opcode 12
contains three subtype-0, one subtype-1, 25 subtype-2, and two subtype-3
instructions. Opcode 11 does not occur and has no established layout.

The initial empirical opcode-7 decoder interpreted the third site word as a
subtype and happened to match every supplied stream. Direct inspection of the
original parser at **0x48698–0x487ac** establishes a more general grammar: that
word is the number of independently counted groups. The native decoder now
implements that loop. Original decoded words and byte spans remain unchanged
for all 30 streams; new source-only cases include three groups of differing
lengths and reject incomplete groups. This correction does not infer what a
site or group does during gameplay.

## Bounds and rejection behavior

The decoder caps input at `Archive::max_resource_size` (32 MiB), checks every
word read, validates record counts against the remaining words before
multiplication or allocation, and requires exact consumption of the resource.
Its command count is bounded by the one-byte header. Construction throws
`FormatError` for unsupported versions, odd byte lengths, incomplete records,
unknown opcodes or subtypes, and trailing data. No partial stream is returned
after a format error. In particular, opcode 11 is rejected rather than given
a speculative length or behavior.

The probe's archive mode requires exactly one matching ID/key pair. A wrong
key, missing ID, or invalid unsigned numeric argument is an error. Archive
decompression and source range validation remain the responsibility of the
existing `g1::Archive` implementation.

## Reproduce the validation

From the native source directory, with the original supplied IPA available
locally:

```sh
python3 tests/verify_missions.py /path/to/G1awakening_v1.1.3_os30.ipa \
  --out build/mission_validation
```

This command compiles `mission_probe` with C++17 and strict compiler warnings,
checks the original IPA and FGIB hashes, extracts each resource independently
with Python's standard-library ZIP/zlib support, and compares the native
decode with the expected canonical decode hash from the earlier Python
audit. That hash covers every opcode, argument word, instruction index, byte
offset, and command byte length. The test also re-encodes the native output
and requires exact equality with each original resource's bytes. It exercises
standalone input and the complete native archive → ID/key lookup →
decompression → mission decode path.

The compact reference was generated with `tests/generate_mission_reference.py`
from the original audit inventory and manifest. Its provenance records their
hashes, the earlier Python decoder source hashes, the original IPA hash, and
the generator hash. The generator does not read or execute the native decoder
and does not derive expected values from its output. The refreshed independent inventory SHA256 is
`860d473eb5d644fac3e6b1460c8cb0e813ac27e75b34dd1b62679c4ff4e1f9cf`.
The earlier inventory hash
`b3a879163cf22db03a4e0406bf15827ca9d72ed91fc41cbd627a08d1cca01ff4`
remains a historical audit reference. The refresh corrects the Python
opcode-7 grammar and its metadata labels; all 30 canonical command decode
hashes and all stream identity records remain exactly unchanged. That
comparison is recorded in `research/mission_validation_opcode7/reference_refresh.json`.
To regenerate this metadata from the complete original research checkpoint:

```sh
python3 tests/generate_mission_reference.py \
  --audit-dir /path/to/original_checkpoint/research/assets \
  --out tests/mission_reference.json
```

Original payloads exist only in a temporary test directory, which is removed
at the end of the run. The retained report contains hashes and result metadata,
not copied original resource files. The test does not require or launch the
old ARM executable.

Recorded Linux host result, using GCC 13.3.0:

| Check | Result |
|---|---:|
| Original streams matching both reference decoders | 30 / 30 |
| Commands with matching opcode, arguments, and byte offsets | 803 / 803 |
| Argument words preserved exactly | 2,800 / 2,800 |
| Native archive lookup results matching standalone decode | 30 / 30 |
| Byte-for-byte re-encoded original payloads | 30 / 30 |
| Original-data-derived malformed input and invalid lookup cases rejected | 855 / 855 |

The rejection set includes every original instruction-boundary truncation,
each original stream with its last word removed, malformed headers, trailing
data, unobserved/unknown opcodes, unknown layout subtypes, overlong nested and
top-level lists, and invalid resource lookups. This is bounded parser evidence,
not a proof that every arbitrary input or every gameplay path has been tested.
Detailed source hashes, compiler command, per-resource matches, and rejection
messages are in the retained `research/mission_validation/native_mission_validation.json`.
The same full original-data check after the opcode-7 correction is recorded in
`research/mission_validation_opcode7/native_mission_validation.json`.

The normal CMake build also includes this module and probe:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
build/mission_probe --archive /path/to/iphone.big 310 0xf4e02223
```

`mission_probe path/to/mission.bin` decodes an already extracted resource.
Neither probe executes gameplay. `g1_mission_tests` provides a small CTest
format-contract suite without requiring the original IPA. Its deliberately
constructed inputs check malformed framing, nested record bounds, unsigned
word fidelity, byte offsets, result ownership, and the input-size cap; they
contain no original game payloads and are not a gameplay oracle. The separate
original-IPA differential check above supplies source-data evidence.
Host tools and CTest executables are omitted when CMake targets iOS; the
decoder remains part of the native static library.

## What remains for native mission execution

The decoder remains a framing component. The separate native `World` module
connects a bounded first-mission initialization prefix to recovered dispatch
behavior, retaining the next command and stopping before the first dialogue
handler. Other objective and trigger semantics still need recovery. An
executor must report unsupported behavior explicitly rather than silently
skip it. Rendering, AI, combat, save/load, audio, campaign completion, and
iPhone XR performance are outside this decoder milestone.
