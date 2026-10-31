# External decompilation workflow review

Reviewed 26 October 2026 for the G1 Awakening native iPhone XR port.

Source: [Game-Decompilation-Skill-For-Claude](https://github.com/lemiur/Game-Decompilation-Skill-For-Claude/tree/eedfe281d4fab7d8cdaf98b9f72f6a6ac4db04be),
revision `eedfe281d4fab7d8cdaf98b9f72f6a6ac4db04be`.

The inspected repository contains seven Markdown skills and four Markdown
references, plus its README and Git attributes. It supplies methodology, not
a compiler, decompiler, ARMv6 Mach-O adapter, recompiler backend or Apple SDK.
The bundle has not been copied into this repository.

## Applied guidance

The `native-platform-porting` guidance fits the existing architecture: typed
asset readers, fixed-width serialized fields, native meshes and audio requests,
preserved gameplay arithmetic, and separate simulation/display clocks. The
offline original-function tools remain development tools and never become
dependencies of the iPhone app. Game mission-command and asset-format readers
are data decoders, not ARM instruction execution.

The helper-recovery approach is useful for the remaining audio paths. Recover
the complete family of original caller guards, actual playback-state query,
shared style selector and emitted request before connecting a native helper.
Mission/battle music maintenance was recovered this way, with all observed
guards preserved and original-body comparisons recorded separately.

The diagnostic method also makes external boundaries explicit. The first
Apple run exposed a camera fixture that included results from the development
host's `tanf`. Those values must not be described as historical iOS math-library
bits. Exact original arithmetic comparisons and target-library numerical
validation require separate evidence; globally relaxing all comparisons would
hide this distinction.

## Project-specific limits

The matching template assumes the original compiler and binary architecture.
Its original-byte fallback linking is useful only in an offline matching
project. The ARM64 release continues to require native source compilation,
functional comparisons, actual graphics checks and physical-device acceptance.
ARMv6 and ARM64 executables are not expected to match byte-for-byte.

The template's replacement of tests with a binary-equality gate does not apply
to this port. Existing tests remain. Temporary stubs also cannot count as a
complete implementation under the user's full-game requirement.

No public license file was present in the inspected tree. This review records
independently written application notes and source links, not redistributed
skill text or a new license assignment.

## Audio interruption review

The native audio host stops retained voice buffers when an interruption ends
without a suggested resume. A subsequent original game maintenance request is
a fresh request. Apple explicitly allows games that start audio without user
input to resume after interruption regardless of `shouldResume`; no additional
user-action latch is needed for this behavior. See Apple's
[shouldResume documentation](https://developer.apple.com/documentation/avfaudio/avaudiosession/interruptionoptions/shouldresume).

This source-level decision does not establish target-device audibility or
interruption behavior; those remain physical iPhone tests.
