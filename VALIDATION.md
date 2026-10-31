# Android installation and UI validation — 0.1.2-dev

Date: 31 October 2026. This update supports **64-bit Android 12/API 31 or later**, retains target SDK 36, and improves the Android control presentation. The Galaxy A36 was the original device target; the phone in the installation report was subsequently confirmed to run Android 12.

## Installation failure reproduced and repaired

The exact GitHub BETA asset `G1-Awakening-Android64-v0.1.1-dev.apk` has SHA-256 `2ff8d0052d75b6d5f897893e7ffb9a92eaab56739ae94c83e52d4febe4a97b37`. It is a valid signed package, but declares minimum API 36. An official Android 12/API 31 emulator rejects it with:

```text
Failed to parse APK file: Requires newer sdk version #36 (current version is #31)
```

The replacement changes both the application minimum and native compilation floor to API 31. Newer Back, receiver and accessibility APIs are used only on Android versions that provide them. The **exact signed replacement APK** was installed on the same Android 12 emulator using `adb install --no-streaming -r`:

```text
Performing Push Install
Success
```

The installed package independently reports versionCode 3, versionName `0.1.2-dev`, minimum API 31, target API 36 and x86_64 native execution. Its ARM64 library is included in that same APK for compatible phones.

Evidence: [old rejection](validation/install-0.1.2/old-apk-rejection.txt), [new installation](validation/install-0.1.2/api31-install.txt), [installed package](validation/install-0.1.2/api31-installed-package.txt), and [emulator platform](validation/install-0.1.2/api31-environment.json).

## Current verification

| Check | Result | Evidence |
| --- | --- | --- |
| Pinned iOS source and licence | All 769 files byte-identical | [Source/resource verification](validation/install-0.1.2/apk-verification.json) |
| Original game resources and intro | All 98 files byte-identical in the final APK | [APK verification](validation/install-0.1.2/apk-verification.json) |
| Release native/Java compilation | ARM64 and x86_64, native API 31 | [Build gates](validation/install-0.1.2/build-gates.json) |
| Android compatibility lint | Zero errors; eleven advisory warnings | [Lint report](validation/install-0.1.2/lint-release.xml) |
| Signature and upgrade identity | Valid; same certificate and application ID as 0.1.1 | [Signature](validation/install-0.1.2/apk-signature.txt) |
| APK/native alignment | 16 KB ZIP and ELF LOAD alignment passed | [ZIP alignment](validation/install-0.1.2/apk-zip-alignment.txt), [ELF verification](validation/install-0.1.2/apk-verification.json) |
| Android 12 installation | Exact delivered APK accepted | [Install log](validation/install-0.1.2/api31-install.txt) |
| Android 12 Activity launch | Cold launch accepted with Status: ok; actual updated G1 menu visually verified after system recovery | [Launch log](validation/install-0.1.2/api31-launch.txt), [runtime record](validation/install-0.1.2/android-runtime.json) |
| Android 12 menu, Settings, Back and accessibility | Actual APK menu controls presented; Settings opened; legacy Back returned to Main Menu | [Navigation check](validation/install-0.1.2/api31-navigation.txt), [menu hierarchy](validation/install-0.1.2/api31-menu-accessibility.xml), [Settings](validation/install-0.1.2/api31-settings-accessibility.xml), [Back result](validation/install-0.1.2/api31-back-menu-accessibility.xml) |
| UI rendering/input contracts | 510 source-font dialogue layouts; menu, production, accessibility and disabled-control checks passed | [UI contracts](validation/install-0.1.2/ui-contract-report.json) |
| Updated UI over actual game state | 1,832 frames / 83 public touches; Mission 1 completed, Mission 2 entered and checkpoint restored | [UI/game integration](validation/install-0.1.2/gameplay-ui-integration.json) |
| Transformed name and portrait | Final code: 238 frames / 24 public touches; Bumblebee's name and rendered portrait retained | [Focused regression](validation/install-0.1.2/transformed-hud-regression.json) |

## UI changes and test scope

Menu, result, production and tactical buttons now have opaque dark faces, steel borders and red accents. Enabled and disabled states are visually distinct. Tactical icons retain their original artwork and gain action captions. HUD names, health, combat statistics and Energon values use the original white font instead of low-contrast red text on red panels.

A real-game render exposed a separate name lookup defect: the second unit text entry is a quotation, not a vehicle-form name. Two Android host lookups now consistently use the canonical name for tactical and battle labels. This also restores the transformed unit's portrait lookup. The regression drives transformation through public touch input and checks the presented accessibility label and portrait framebuffer pixels.

The full 1,832-frame integration run used the new button presentation and completed immediately before the two final name-lookup corrections. The 238-frame focused regression ran afterward against the final application code. These are separate results; the focused run is not another whole campaign pass.

The main-menu preview supplied with this update is a capture from the Android 12 emulator running the exact delivered APK. The gameplay UI preview is an actual offscreen GLES render of AndroidGame with original resources. Neither is a physical Samsung capture. The unchanged core's 72 tests, 127 renderer checks and eight audio recovery scenarios were last run for 0.1.1; [that historical validation](docs/VALIDATION_0_1_1.md) is retained and is not relabelled as new testing.

## APK identity

- File: `G1-Awakening-Android64-v0.1.2-dev.apk`
- Size: **42,118,076 bytes**.
- Version: `0.1.2-dev`; versionCode `3`.
- Minimum/native API: `31`; target/compile API: `36`.
- Native ABIs: `arm64-v8a` and `x86_64`.
- SHA-256: `b0b79c6e2304a8f8fbce07f9445288967f02443994d330bfa9026b697a031939`.
- Signing certificate SHA-256: `303fcdd4074d308738d01d667dc2c6f1ccca8eee32fc077f367a2ccea91dbf29`.

The retained certificate and application ID permit updates without deleting private saves. Do not uninstall or clear storage to work around an update error when saves must be retained. Build inputs are recorded in [the source fingerprint](validation/install-0.1.2/android-source-fingerprint.json).

## Runtime limits

Installation and Activity startup are independently verified on the official Android 12 x86_64 emulator. This host has no KVM acceleration; the emulator's System UI displayed an ANR during the first launch. After system recovery, the actual G1 main menu was visually verified. Its accessibility controls, Settings button and legacy Android12 Back navigation passed. This does not establish every mission, audio route or Android lifecycle transition. See the final [Android runtime record](validation/install-0.1.2/android-runtime.json) for the exact observed scope.

No physical phone is attached. Full Story, Showdown and Arena playthroughs, physical ARM64 graphics-driver behaviour, audible audio/video, touch latency, TalkBack, sustained Modern performance and an Android 16 runtime regression remain device-validation work. No whole-game 100% verification claim is made.
