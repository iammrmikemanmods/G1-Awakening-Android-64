# Android port validation

This record separates source identity, compilation, offscreen rendering,
actual game-host behavior, Android runtime checks and physical-device testing.
Results apply to this development checkpoint. They are not a blanket claim
that every campaign path or every future Android version is verified.

## Completed host checks

| Check | Result | Evidence |
| --- | --- | --- |
| Pinned iOS source and licence | 769 files byte-identical |`SOURCE_PROVENANCE.json` |
| Original 017R1 resources | 98 files, 36,914,225 bytes; 88 clean PNGs; original intro |`validation/ios-resource-manifest.json` |
| Portable core contracts | 72/72 passed, Debug assertions enabled |`validation/host-core.xml` |
| GLES renderer | 51 shader/state/pixel checks passed |`validation/renderer-report.json` |
| UI contracts | Source art/font geometry, 511 English entries inspected, receipts, disabled controls and safe insets passed |`validation/ui-contract-report.json` |
| Production list | All 23 original unit definitions plus fixed Cancel; offscreen hit rejection |UI report |
| Accessibility mapping | Unique visible nodes, source labels/readouts, disabled state and normal-hit equivalence |UI report; Android Java compile |
| Actual AndroidGame integration | 993 rendered frames, 45 public touches |`validation/game-probe-report.json` |

The host uses real Mesa llvmpipe EGL/OpenGL ES, at 2340 × 1080 with a 72 px left
safe inset. It loads the exact game data and uses the same Android C++ host
compiled into the application. The font adapter for desktop tests uses
DejaVuSansMono; the Android app uses Canvas fonts.

The full public-touch integration run covered fresh profile→Play→Story→Mission 1,
the seven original opening dialogue commands, camera and world gates, and
normal player control. It rendered Classic and Modern, with zero Modern
sprite fallbacks. It opened real Tech Specs robot/vehicle models, transformed,
confirmed a move with two taps, completed movement and Undo, advanced two
normal turns with AI movement, attacked a legitimately adjacent opponent,
rendered the original M3G battle, and returned to tactical play.

The test deliberately rejected the completed final battle frame. The core
battle remained pending; the next matching final HUD and successful GPU frame
acknowledged it exactly once. It also rejected an ordinary failed-present
receipt, invalidated a pending receipt at pause, resumed without background
time catch-up, destroyed/recreated the actual GL context, and restored a
post-combat checkpoint into a newly constructed host through Continue.

Before that restart, diagnostics recorded 988 successful presentations,
352 simulation ticks, 4 moves, 1 transform, 1 attack, 2 End Turns, 1 Undo,
1 acknowledged battle and 16 checkpoint writes. Diagnostic counters reset
with the new host; both pre-restart and post-restart states are retained in
the report. The cadence check fed 90 intervals of 8,333,334 ns and observed
exactly 25 original 30 ms simulation ticks.

These are real game-host interactions, not direct `GameSession` mutations,
unlocked debug missions or synthetic acknowledgement of unseen battle frames.
They remain offscreen desktop execution, not an Android or Samsung device test.

## Android packaging and runtime

The Android Java host, JNI, native renderer and unchanged core compile with the
pinned Android 36 SDK/NDK toolchain. Both optimized ARM64 release and x86_64
development builds succeeded. Debug and release lint each report zero errors
and ten advisory warnings. The optimized ARM64 APK is development-signed,
39,529,529 bytes, with SHA-256:

`f7e8b271f011651bbbf334b6a5c6c8bd83c3682dc4b925a44522214efe632684`

The APK's v3 signature, API 36 minimum/target, ARM64-only library, ELF LOAD
alignment and APK ZIP alignment were independently verified. All 98 original
resource files inside that APK match their pinned hashes. Exact output is in
`validation/android/apk-verification.json`, the adjacent Android build-tools
reports, and `validation/source-and-assets.json`.

The final x86_64 development APK was installed in the official Android 16/API 36
software emulator. The native main menu rendered, a real Play Game touch opened
Game Modes, Home/resume retained that panel, and Back returned through Main Menu
to the Android launcher. Pause diagnostics recorded 861 successful presented
frames, the source Classic default, and no native game/audio error. The final
process log contained no fatal exception, signal, JNI or library-loading marker.
See `validation/android/emulator-smoke.json` for the exact test APK, image and
diagnostic state.

The unchanged original intro decoded and naturally completed into the menu on
the preceding development build. The final debug smoke used its debug-only
intro-skip option; that option is ignored by the delivered release APK. No
mission was played inside the Android emulator. Broader gameplay coverage is
the separate offscreen native integration run described above.

Without KVM, the emulator used CPU emulation and SwiftShader. The OS displayed
system ANR dialogs during boot. Two bounded native lifecycle waits also exceeded
two seconds during early backgrounding and final shutdown; subsequent surface
recovery/rendering and Back-to-launcher succeeded. Those warnings remain recorded,
so this smoke is not a performance or interruption-latency certification.

`tools/verify_port.py --apk ... --abi arm64-v8a` checks the actual packaged
resource inventory/hashes, ELF64 architecture, native 16 KB LOAD alignment,
duplicate ZIP entries and ZIP CRCs. Android `apksigner`, `zipalign` and `aapt2`
independently verify signer, APK alignment and SDK/manifest metadata.

No physical Galaxy A36 is attached to this environment. Android emulator
captures, if present, are identified separately from offscreen host captures.
Emulation is not evidence of Samsung GPU timing, thermal behavior or touch
latency. Audio stream creation is not evidence that a listener heard correct
audio. The original movie's hash is not evidence that a physical phone decoded
and displayed every frame.

## Remaining device acceptance

The target phone must run Android 16/API 36 or later. Record the APK SHA-256,
model, OS build and observed behavior for its launch/intro, source menus,
mission controls, sound/music, save/Continue, Back, TalkBack, backgrounding,
process recreation and Classic/Modern performance. Complete all campaign
modes through natural unlocks before marking whole-game device parity as
verified. Platform font rasterization and OS audio/navigation behavior are
documented differences in `PORT_PARITY.md`.
