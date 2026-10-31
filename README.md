# G1 Awakening — Android ARM64

Native Android port of the iOS **Build 017 / 017R1** game, targeting the
**Samsung Galaxy A36 on Android 16 or later**. The original portable engine
is compiled directly; Android supplies the display, input, audio, video and
application lifecycle adapters.

**Status: development build with specific automated verification.** An APK
build and host gameplay checks do not establish that every mission works on
a physical A36. Current evidence and remaining device checks are recorded in
[VALIDATION.md](VALIDATION.md) and [PORT_STATUS.json](PORT_STATUS.json).

## Development update 0.1.1

This update keeps the complete pinned iOS source and all original resources.
It fixes startup when the Android audio device is temporarily unavailable,
matches the iOS four-tap supersampling resolve, and falls back to native
resolution if a supersampling target cannot be allocated. APK/AAB packaging
now requires the complete verified asset set.

The extended native gameplay probe completes Mission 1 through normal controls,
opens Mission 2 through the original campaign controller, and verifies its
unlock and Continue checkpoint. It passes **1,832 rendered frames and 83 touches**.
The renderer passes **127 checks**, and the audio recovery probe passes
**eight failure/recovery scenarios**. These host tests are recorded separately
from Android runtime and physical-phone validation.

See [the update record](docs/DEVELOPMENT_0_1_1.md) for the changes and evidence.

## Authoritative source and assets

- iOS source: [G1-Awakening-IOS](https://github.com/iammrmikemanmods/G1-Awakening-IOS).
- Pinned commit: [`be203a911bc26313615ea450758413e5c9cf6462`](https://github.com/iammrmikemanmods/G1-Awakening-IOS/tree/be203a911bc26313615ea450758413e5c9cf6462),
  from `feature/clean-motion-017`. This is the Build 017 source identified by
  the iOS project, rather than the older engine on its main branch.
- `upstream/ios/native/` and `upstream/ios/LICENSE` are **769 unmodified files**.
  [SOURCE_PROVENANCE.json](SOURCE_PROVENANCE.json) records every file's size and SHA-256.
- The owner-supplied `G1_Awakening_iOS_017R1_Remaster_Intro_AppSync.ipa` supplies
  **98 original resources**, including `iphone.big`, `terrain-tables.bin`,
  **88 CleanTextures017 PNGs**, licences, icon and **the actual iOS intro**.
- `G1OpeningXR.mp4` is copied byte for byte. It is not transcoded, regenerated
  or replaced. The inspected file is 1108 × 512, 30 fps, approximately 23 seconds.

The full IPA's SHA-256 is
`b035acfe0c08aafa715c54f60a59423191046c271328abbca4cd72689dfaed46`.
The import script rejects a different resource package. The exact resource
inventory is [validation/ios-resource-manifest.json](validation/ios-resource-manifest.json).
Original game data and signing keys are kept out of this public source repository.
The owner APK contains the imported resources; a source-only CI build does not.

## What the Android host implements

The unchanged C++ core supplies mission scripts, unit definitions, movement,
capture, transformation, combat, AI turns, production, healing, upgrades,
campaign unlocks, RNG, profile persistence and checkpoint codecs.

The Android adapter connects that core to the source menus, Help, dialogue,
HUD, Tech Specs model viewer, terrain, animated units, support presentation,
3D battles, music and sound. It preserves the source's presentation gates:
world/camera/transform/battle completion is acknowledged only for matching
rendered content after GPU completion and a successful EGL buffer swap.

The opening video plays before game creation. Tap it or press Back to skip,
as supported by the platform host. The game starts in **Classic** mode,
matching the iOS default; Modern is available in Settings. Both use the
same enhanced source UI. Modern uses the source clean textures, M3G tactical
models, soft shadows and the source supersampling budget policy.

Android integration includes app-private atomic saves, safe-area insets,
landscape orientation, predictive Back, touch/pan/pinch, display-paced
rendering, audio focus, pause/resume and GPU resource recreation. The engine
retains its original 30 ms simulation step; fractional frame time is carried
between callbacks so 120 Hz does not slow the simulation.

See [PORT_PARITY.md](PORT_PARITY.md) for platform differences and intentional
host fixes. Android fonts, OS dialogs, audio routing and graphics drivers
cannot be described as byte-identical iOS platform implementations.

## Build an installable owner APK

Requirements: JDK 17, Python 3, Android SDK 36, build-tools36.0.0,
NDK 28.2.13676358 and CMake 3.22.1. Gradle 8.11.1 and AGP 8.10.1 are pinned.

```bash
python3 tools/import_ios_assets.py /path/to/G1_Awakening_iOS_017R1_Remaster_Intro_AppSync.ipa
python3 tools/verify_port.py --require-assets
bash tools/bootstrap-android.sh
./gradlew :app:assembleDebug :app:lintDebug
```

`bootstrap-android.sh` installs the pinned SDK components and writes local
SDK configuration. It requires an existing JDK 17 and prompts for Android SDK
licences unless `G1_ACCEPT_ANDROID_LICENSES=1` was deliberately set.

The default APK is `app/build/outputs/apk/debug/app-debug.apk`, with only
`arm64-v8a`, minimum API 36 and target API 36. Install it on the A36:

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Packaging runs `:app:verifyGameAssets` automatically. Missing, changed or extra
game resources reject the build before an installable package is produced.
Source-only compilation and lint remain available without the original data.
On a host where Python 3 is named `python`, pass `-Pg1Python=python`.

An x86_64 emulator build is available with `-Pg1Abis=x86_64`. That APK is for
an emulator, not the A36. For an optimized package, use `:app:assembleRelease`
and sign the result with the maintainer's retained private key using Android
`apksigner`. Keep the same signer for future upgrades; keys are never committed.
See [docs/BUILD_AND_TEST.md](docs/BUILD_AND_TEST.md) for verification and signing commands.

Save data stays in the application's private `files/saves` directory. Installing
an update with the same application ID and signer preserves it. Clearing app
data or uninstalling deletes it. The port keeps the source checkpoint boundary
semantics: Continue resumes the last completed safe action/objective boundary.

## Source-only verification

```bash
python3 tools/verify_port.py
cmake -S upstream/ios/native -B build/host -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

The public GitHub workflow runs the self-contained core tests and Android
native/Java compilation plus lint. It does not advertise an APK without the
original resources as playable. Linux GLES/UI/game probes and their exact
scope are documented in [docs/BUILD_AND_TEST.md](docs/BUILD_AND_TEST.md).

## Licences and provenance

The native port source retains the upstream MPL-2.0 licence. LodePNG and
stb_vorbis retain their original licence files in `upstream/ios/native/third_party`.
Original game assets retain their original ownership. No original ARM32
executable, iOS signing material or compatibility interpreter is packaged.

Preserve [README_MEMORY_ZIP_POLICY.md](README_MEMORY_ZIP_POLICY.md) in all
source and development archive exports.
