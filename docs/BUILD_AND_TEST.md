# Build, verify and maintain the Android port

## Pinned toolchain

| Component | Version |
| --- | --- |
| JDK | 17 |
| Gradle | 8.11.1, wrapper/distribution SHA-256 checked |
| Android Gradle Plugin | 8.10.1 |
| Compile/target/minimum SDK | 36 / 36 / 31 |
| Build tools | 36.0.0 |
| NDK | 28.2.13676358 |
| CMake for Android | 3.22.1 |
| Default source-build ABI | arm64-v8a |
| 0.1.2 release APK ABIs | arm64-v8a and x86_64 |
| Native API floor | 31, matching the application's Android 12 minimum |
| Application version | 0.1.2-dev; versionCode 3 |
| C++ | 17, static libc++, floating-point contraction disabled |

The app requires a 64-bit Android 12+ environment and OpenGL ES 3, and uses no
network or external-storage permission. Native ELF LOAD segments and uncompressed
APK libraries must support 16 KB pages, including when compiling for API 31.
See Android's [page-size guidance](https://developer.android.com/guide/practices/page-sizes)
and [Android 16 behavior changes](https://developer.android.com/about/versions/16/behavior-changes-16).

## Android 12 installation repair

The reported phone ran Android 12/API 31. The downloaded 0.1.1 APK declared
minimum API 36, which excluded that phone before the game could start. Its ZIP,
manifest and signature were intact. Version 0.1.2 changes both minimum SDK and
native API floor to 31 while retaining target SDK 36.

| Host feature | Newer Android path | Android 12 path |
| --- | --- | --- |
| Back | API 33+ dispatcher, isolated callback helper | `onBackPressed()` invokes the existing game Back action |
| Power-save broadcast receiver | API 33+ receiver export flags | Protected system broadcast with the earlier receiver overload |
| Accessibility geometry | API 34+ window bounds | Parent-relative bounds plus screen bounds |

The API floor must be changed in the actual Java/native build, not only in the
final manifest. Keep newer callback types out of unconditional activity field
initializers. Lint and runtime checks are required for both the minimum OS and
the target OS. Current results belong in [VALIDATION.md](../VALIDATION.md), with
0.1.1 results retained as historical evidence rather than credited to this build.

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

The default ARM64 APK includes all imported assets and requires Android 12/API 31
or later. A developer can also build an x86_64-only emulator variant with
`-Pg1Abis=x86_64`; that variant cannot run on an ARM64 phone.

For the optimized universal 64-bit development release:

```bash
./gradlew :app:assembleRelease :app:lintRelease -Pg1Abis=arm64-v8a,x86_64
```

Both architectures are included in one APK so the package used for an x86_64
emulator test can be the exact file delivered for ARM64 phone installation.
Omit `-Pg1Abis` for an ARM64-only source build. No 32-bit ABI is included.

The unsigned output is `app/build/outputs/apk/release/app-release-unsigned.apk`.
Sign using a retained private keystore outside this checkout. Let apksigner
prompt for the password rather than placing credentials in shell history.
Keep the existing signer to preserve update compatibility; a newly generated
debug key is not interchangeable with the development release's retained key.

```bash
"$ANDROID_HOME/build-tools/36.0.0/zipalign" -P 16 -f 4 app/build/outputs/apk/release/app-release-unsigned.apk /tmp/g1-aligned.apk
"$ANDROID_HOME/build-tools/36.0.0/apksigner" sign --ks /private/path/maintainer.jks --out /tmp/G1-Awakening-Android64-v0.1.2-dev.apk /tmp/g1-aligned.apk
"$ANDROID_HOME/build-tools/36.0.0/apksigner" verify --verbose --print-certs --min-sdk-version 31 --max-sdk-version 36 /tmp/G1-Awakening-Android64-v0.1.2-dev.apk
"$ANDROID_HOME/build-tools/36.0.0/zipalign" -c -P 16 -v 4 /tmp/G1-Awakening-Android64-v0.1.2-dev.apk
"$ANDROID_HOME/build-tools/36.0.0/aapt" dump badging /tmp/G1-Awakening-Android64-v0.1.2-dev.apk
python3 tools/verify_port.py --require-assets --apk /tmp/G1-Awakening-Android64-v0.1.2-dev.apk
```

The keystore path above is supplied by the maintainer. The verification tool
checks APK CRCs, duplicate entries, every original resource byte, each native
library's ABI/ELF64 machine and native segment alignment. Its `--abi` option
asserts a single-ABI package, so omit it for the universal package. Use
`--abi arm64-v8a` only when verifying an ARM64-only build.

Inspect the `aapt` output for minimum SDK `31`, target SDK `36`, versionCode `3`,
versionName `0.1.2-dev` and both native-code entries `arm64-v8a` and `x86_64`.
Verify the release signer certificate SHA-256 remains:

```text
303fcdd4074d308738d01d667dc2c6f1ccca8eee32fc077f367a2ccea91dbf29
```

Android build-tools independently check signing, manifest metadata and APK ZIP
alignment. Signature verification across API levels is not an installation or
gameplay test.

## Android 12 and Android 16 runtime checks

Test the signed universal package on API 31 and API 36. Record the package hash,
device/emulator identity and architecture for each result. With one test device
connected, these commands identify the runtime and install/start the package:

```bash
adb shell getprop ro.build.version.sdk
adb shell getprop ro.product.cpu.abilist
adb install -r /tmp/G1-Awakening-Android64-v0.1.2-dev.apk
adb shell am start -W -n com.iammrmikeman.g1awakening/.MainActivity
```

Exercise the actual intro, Back-to-skip, the game menu, a submenu's Back action,
and Home/resume. An accessibility hierarchy dump while the menu is visible
exercises the version-dependent node bounds:

```bash
adb shell uiautomator dump /sdcard/g1-ui.xml
adb pull /sdcard/g1-ui.xml /tmp/g1-ui.xml
```

Check logs for startup exceptions, missing Android API classes/methods and
native linker errors. The old 0.1.1 package can serve as a rejection control
on API 31; record its actual installer error rather than inferring a diagnostic
code from a generic installer dialog.

An x86_64 emulator exercises the x86_64 library and Android platform adapters.
It does not establish physical ARM64 performance, GPU behavior or audio output.
Label Android emulator screenshots, desktop offscreen GLES fixtures and physical
phone screenshots distinctly. Use the applicable entry in `VALIDATION.md` for
completed checks; a planned test or a successful compile is not a runtime pass.

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
bash tools/run-audio-recovery-probe.sh
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

The game probe introduced for 0.1.1 finishes Mission 1 and opens Mission 2 through normal public
touches, including results, natural unlocks and Continue. Its GNU linker wrapper
rejects one framebuffer allocation to exercise native-resolution recovery; the
wrapper exists only in the host probe and is never linked into the Android app.

The audio recovery probe uses the original audio dispatcher, Vorbis decoder and
game archive with an injected AAudio device boundary. It tests successful PCM
output, unavailable-device retries, retained preloads, foreground/background,
disconnects and pause errors. It does not establish physical audibility.

APK and AAB packaging depend on `:app:verifyGameAssets`. The verifier is run for
each packaging invocation and checks actual bytes rather than trusting timestamps.
Use `-Pg1Python=python` if that is the Python 3 executable name on the build host.
The public CI source compile/lint tasks intentionally remain independent of this
packaging-only gate; the self-contained 127-check renderer probe also runs in CI.

## Physical-phone validation and recovery

Record the APK SHA-256, signer digest, phone model and Android build with each
device result, including Android 12 phones and the original A36 target. Check
a clean launch through the original intro, menu and Story,
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

## Focused transformed-HUD regression

After the host core and original resources are available, run:

```bash
G1_UI_NAME_SMOKE=1 bash tools/run-game-probe.sh validation/runtime/name-portrait
```

This bounded public-touch run checks the presented character name and rendered portrait before and after transformation. Without `G1_UI_NAME_SMOKE`, the full gameplay probe continues through its existing campaign/checkpoint sequence. The final 0.1.2 focused run and the preceding full UI integration run are reported separately in `VALIDATION.md`.
