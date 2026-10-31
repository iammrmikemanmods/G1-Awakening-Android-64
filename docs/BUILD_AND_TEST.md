# Build, verify and maintain the Android port

## Pinned toolchain

| Component | Version |
| --- | --- |
| JDK |17 |
| Gradle |8.11.1, wrapper/distribution SHA-256 checked |
| Android Gradle Plugin |8.10.1 |
| Compile/target/minimum SDK |36 /36 /36 |
| Build tools |36.0.0 |
| NDK |28.2.13676358 |
| CMake for Android |3.22.1 |
| Default ABI |arm64-v8a |
| Native API floor |35, within the app's minimum API 36 |
| C++ |17, static libc++, floating-point contraction disabled |

The app requires OpenGL ES 3 and uses no network or external-storage permission.
Native ELF LOAD segments and uncompressed APK libraries must support16 KB pages.
See Android's [page-size guidance](https://developer.android.com/guide/practices/page-sizes)
and [Android 16 behavior changes](https://developer.android.com/about/versions/16/behavior-changes-16).

## Resource import and provenance

```bash
python3 tools/import_ios_assets.py /path/to/G1_Awakening_iOS_017R1_Remaster_Intro_AppSync.ipa
python3 tools/verify_port.py --require-assets
```

The importer verifies the full input IPA hash and ZIP integrity, copies only
the allowed original resource paths, then records every output hash. The native
host also verifies the main archive and terrain-table hashes before decoding.
Android verifies all installed resource files and repairs changed resource
copies atomically without writing into `files/saves`.

The committed resource manifest contains hashes and names, not the resource
payload. An arbitrary older/newer IPA is intentionally rejected. To change the
source revision, review the new source/package together, update provenance,
and rerun the relevant tests rather than weakening the hash checks.

## Compile

```bash
bash tools/bootstrap-android.sh
./gradlew :app:assembleDebug :app:lintDebug
```

The default ARM64 APK includes all imported assets. A developer can also build
an emulator variant with `-Pg1Abis=x86_64`; do not install that variant on A36.

For an optimized build:

```bash
./gradlew :app:assembleRelease :app:lintRelease
```

The unsigned output is `app/build/outputs/apk/release/app-release-unsigned.apk`.
Sign using a retained private keystore outside this checkout. Let apksigner
prompt for the password rather than placing credentials in shell history:

```bash
"$ANDROID_HOME/build-tools/36.0.0/zipalign" -P 16 -f 4 app/build/outputs/apk/release/app-release-unsigned.apk /tmp/g1-aligned.apk
"$ANDROID_HOME/build-tools/36.0.0/apksigner" sign --ks /private/path/maintainer.jks --out /tmp/G1-Awakening-Android-ARM64.apk /tmp/g1-aligned.apk
"$ANDROID_HOME/build-tools/36.0.0/apksigner" verify --verbose --print-certs /tmp/G1-Awakening-Android-ARM64.apk
"$ANDROID_HOME/build-tools/36.0.0/zipalign" -c -P 16 -v 4 /tmp/G1-Awakening-Android-ARM64.apk
python3 tools/verify_port.py --require-assets --apk /tmp/G1-Awakening-Android-ARM64.apk --abi arm64-v8a
```

The verification tool checks APK CRCs,
duplicate entries, every original resource byte, the ABI, ELF64 machine and
native segment alignment. Android build-tools independently check signing,
manifest metadata and APK ZIP alignment.

## Host tests

The 72 upstream tests require no IPA and use bundled source test vectors. A
Debug host build keeps assertions enabled:

```bash
cmake -S upstream/ios/native -B build/host -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

The renderer probe uses a real offscreen EGL/OpenGL ES context. On Debian/Ubuntu,
install a C++ compiler, zlib development files, Mesa EGL/GLES development files
and Python/Pillow for the host platform-font adapter. Then run:

```bash
bash tests/renderer/run-renderer-probe.sh
bash tools/run-ui-probe.sh
bash tools/run-game-probe.sh
```

The latter two probes require imported original resources and the built host
core library. `G1_GLES_PREFIX` (renderer) or `G1_GL_DEPS` (UI/game) can point at an
extracted dependency root when system headers/libraries are not installed. Inspect each script's supported
environment variables before changing build locations.

UI fixtures exercise source geometry, fonts, enabled/disabled controls, dialogue
receipts and long production lists. The game probe starts with a fresh private
save directory, uses actual public-touch menu/command paths, renders frames,
and restores a written checkpoint into a new host. Test screenshots identify
their offscreen origin; they are not device captures.

## A36 validation and recovery

Record the APK SHA-256, signer digest, phone model and Android build with each
device result. Check a clean launch through the original intro, menu and Story,
then save/Continue, movement, transform, undo, capture, combat, support, production,
Tech Specs, audio settings, Classic/Modern, rotation and background/foreground.
Complete the source campaign modes through natural unlocks before declaring
full campaign parity. Test Back, TalkBack, system interruptions and sustained
Modern operation separately.

Updates require the same application ID and signing key. `adb install -r` keeps
private saves when signer/version requirements are met. Do not uninstall or
clear storage to work around an update error if the save must be preserved.
The private directory contains original-codec campaign/session files plus
atomic backup/retired records. Debug builds permit `adb shell run-as
com.iammrmikeman.g1awakening` for inspecting those files and the real
`files/saves/android-diagnostics.json` report written at pause. Release builds
do not expose private files through run-as.

The development intent extra `skip_intro=true` exists only in debug builds for
automated tests. It is never enabled in the normal user flow, and release builds
ignore it. An intro decoding error is shown explicitly rather than substituting
another movie or silently asserting successful playback.
