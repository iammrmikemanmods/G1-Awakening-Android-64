# Android port validation — 0.1.1-dev

Date: 31 October 2026. This record distinguishes source identity, compilation,
desktop execution of the Android host, emulator installation and physical-device
acceptance. The target remains Samsung Galaxy A36 on Android 16/API 36 or later.

## Current verified results

| Check | Result | Evidence |
| --- | --- | --- |
| Pinned iOS source and licence | All 769 files byte-identical to Build 017 | [Source provenance](SOURCE_PROVENANCE.json) |
| Original 017R1 resources | All 98 files byte-identical, including 88 clean PNGs and the iOS intro | [APK resource verification](validation/resume-0.1.1/apk-verification.json) |
| Portable core tests | 72/72 passed with Debug assertions | [CTest report](validation/resume-0.1.1/host-core.xml) |
| GLES renderer | 127 pixel/state checks passed | [Renderer report](validation/resume-0.1.1/renderer-report.json) |
| Original UI contracts | Fonts, menus, dialogue, controls, scrolling production choices, safe-area geometry and accessibility mapping passed | [UI report](validation/resume-0.1.1/ui-contract-report.json) |
| Audio failure recovery | Eight scenarios passed with original planner/decoder and injected AAudio device calls | [Audio report](validation/resume-0.1.1/audio-recovery-report.json) |
| Public-touch game integration | 1,832 rendered frames; 83 touches; Mission 1 complete; Mission 2 control/unlock/checkpoint | [Gameplay report](validation/resume-0.1.1/game-probe-report.json) |
| Supersampling allocation failure | Native 2340 × 1080 frame successfully presented, then 3510 × 1620 supersampling recovered | [Gameplay report](validation/resume-0.1.1/game-probe-report.json) |
| Android native/Java builds | Optimized ARM64 release and x86_64 debug passed | [Build gates](validation/resume-0.1.1/build-gates.json) |
| Android lint | Zero errors; nine advisory warnings in each variant | [Debug](validation/resume-0.1.1/lint-debug.xml), [release](validation/resume-0.1.1/lint-release.xml) |
| Missing resource package | Actual Gradle package task rejected; source-only compile/lint still passed | [Rejection](validation/resume-0.1.1/package-without-assets.txt), [source-only build](validation/resume-0.1.1/source-only-no-assets.txt) |
| Resource corruption | A single changed intro byte and an extra unmanifested file were rejected | [Asset rejection report](validation/resume-0.1.1/asset-rejection-report.json) |
| APK integrity/signing/alignment | ZIP CRCs, original resources, signature, ARM64 ELF64 and 16 KB ELF/APK alignment passed | [APK verification](validation/resume-0.1.1/apk-verification.json), [signature](validation/resume-0.1.1/apk-signature.txt), [alignment](validation/resume-0.1.1/apk-zip-alignment.txt) |

## Gameplay coverage and its limits

The gameplay probe runs the actual Android C++ host with the exact original
resources on Mesa llvmpipe EGL/OpenGL ES, at 2340 × 1080 with a 72 px safe inset.
Desktop system-font rendering uses DejaVuSansMono; the Android app uses Canvas.

The test starts a fresh profile, enters Story and Mission 1 through real menus,
handles original dialogue and presentation gates, and uses enabled public touch
targets. It covers Classic/Modern, Tech Specs robot/vehicle models, transformation,
two-tap movement, Undo, normal end turns and AI movement, real M3G battles,
objective capture, Mission Complete results and the player's Continue action.
The campaign controller then installs Mission 2. Mission 2 is naturally unlocked
while Mission 3 remains locked, and Continue restores the Mission 2 checkpoint.

Presentation checks reject a failed final battle frame, prevent stale receipts
at pause, preserve the 30 ms simulation cadence at 120 Hz, replace the actual
host GL context, and resume without catching up background time. A host-only
linker wrapper rejects one framebuffer allocation; the game presents a native
frame successfully and returns to supersampling on the next frame. The wrapper
is not compiled into the Android application.

No game stats, mission progress, profile unlocks or RNG were injected. These are
real host interactions, but they do not exercise Android Java lifecycle or a
Samsung driver. Heal, upgrade, production and the support-tutorial action path
are outside this probe's newly exercised gameplay. Entire Story, Showdown and
Arena playthroughs remain unverified on Android.

## APK and update identity

The delivered optimized development APK is:

- `G1-Awakening-Android64-v0.1.1-dev.apk`
- 39,533,625 bytes; ARM64 only; minimum and target API 36.
- Version name `0.1.1-dev`, version code `2`.
- SHA-256: `2ff8d0052d75b6d5f897893e7ffb9a92eaab56739ae94c83e52d4febe4a97b37`.
- Signing certificate SHA-256: `303fcdd4074d308738d01d667dc2c6f1ccca8eee32fc077f367a2ccea91dbf29`.

The certificate and application ID match the previous 0.1.0 development build.
The private key was recovered locally and was not committed. The Android source
and build inputs used for this pass are fingerprinted in
[android-source-fingerprint.json](validation/resume-0.1.1/android-source-fingerprint.json).

## Current Android emulator result: incomplete

The official Android 16 x86_64 emulator completed boot and accepted the final
same-signer update over the previous installation, without uninstalling or
clearing application data. The MainActivity launch request was accepted.

The emulator then displayed **Process system isn't responding** and later
**Quickstep isn't responding**. This environment has no KVM; the emulator was
using software CPU emulation. A settled game menu or mission could not be
verified in this pass. The result is **incomplete runtime acceptance**, not a
passed Android gameplay test and not proof of an application defect.

Exact scope and observations are recorded in
[emulator-runtime.json](validation/resume-0.1.1/emulator-runtime.json).
The successful menu/Home/Back smoke from 0.1.0 is historical evidence, preserved
in [the initial validation record](docs/VALIDATION_0_1_0.md). It is not reused as
a new runtime pass for 0.1.1. The current original-intro check is byte identity;
this pass does not claim a new physical-device movie playback test.

## Remaining device acceptance

No physical Galaxy A36 is attached. Test the delivered ARM64 APK on the target
phone: opening cinematic, menus, missions, sound/music, save/Continue, Back,
TalkBack, interruptions, process recreation and sustained Classic/Modern use.
Record the APK hash and Android build with the result. Complete the original
campaign modes through natural unlocks before declaring whole-game device
parity. Hardware audibility, touch latency, driver behavior and thermal
performance require their own observations.

The [update record](docs/DEVELOPMENT_0_1_1.md) explains the changes. Platform
substitutions remain documented in [PORT_PARITY.md](PORT_PARITY.md).
