# iOS-to-Android parity record

Authority is the pinned iOS 017 source plus the inspected 017R1 resource package.
The goal is to preserve that implementation's behavior, including its existing
inspection camera and recovered gameplay rules. This is not a claim of new
reverse-engineering parity with every behavior of the historical 32-bit game.

## Preserved inputs and logic

| Area | Android treatment | Evidence |
| --- | --- | --- |
| Portable native engine | Compile original C++ source without edits | 769 source/licence hashes; 72 core tests |
| Game archive and terrain tables | Copy original bytes; verify at import and startup | Pinned IPA and resource SHA-256 manifests |
| 017R1 clean textures | All 88 PNGs copied without processing | Resource manifest and source-pixel-key lookup |
| Opening movie | Original `G1OpeningXR.mp4`, before game creation | Exact file hash; Android MediaPlayer adapter |
| Source art and bitmap fonts | Decode from original archive; use original UI geometry | Offscreen pixel, font and UI tests |
| Campaign and rules | Original scripts, unlock gates, AI/RNG, commands and save codecs | Upstream contract tests; public-touch integration probe |
| Presentation gates | Match session, scene, epoch and token; acknowledge successful presentation | Failed-present, pause and graphics-recreation probes |
| Modern world presentation | Source tactical M3G models, materials, shadows, source-keyed textures | GLES pixel/state tests and actual mission rendering |
| Checkpoints | Original atomic writer and serialized session format | New-host Continue restore in gameplay probe |

## Platform substitutions

UIKit/Metal/AVFoundation cannot execute on Android. Android host code replaces
these APIs while retaining the source data and orchestration:

- Metal shaders and fixed-function state are translated to OpenGL ES 3. The
  renderer converts Metal's 0..1 clip-depth convention to GLES −1..1, implements
  original blend/texture/alpha/depth behavior, and retains immutable model
  source-geometry checks. Driver floating-point/rasterization differences remain.
- EGL presents Android buffers. Completion fences plus successful buffer queueing
  gate receipts; this is not a measurement of physical panel scan-out time.
- Android Canvas monospace/system text replaces iOS Menlo/UIKit for source roles
  that used platform fonts. Content is preserved, including registered symbols;
  exact glyph pixels, font weight, line breaks and page boundaries can differ.
- The production action sheet becomes a scrollable GL list with a fixed Cancel
  action. It exposes all original choices rather than applying the original
  background artwork's nine-button menu limit to a native action sheet.
- AAudio plays the original decoded mono 22050 Hz clips, resampling if the opened
  output stream has another sample rate. Android audio focus suspends playback
  when another application owns audio; this differs from iOS Ambient mixing.
- MediaPlayer presents the unchanged intro with aspect fit and lifecycle position
  retention. Android decoder output and physical audio still require device checks.
- Android Back and safe insets replace UIKit navigation/system margins. Low-power,
  serious thermal and memory-pressure signals feed the unchanged source quality
  policy; UI remains at the display's native resolution.
- Android 12/API 31 is the runtime and native minimum from 0.1.2. API 33+
  uses the newer Back dispatcher and receiver export flags; API 34+ uses
  accessibility window bounds. Older versions use the corresponding guarded
  Android host paths. Compile and target SDK remain 36.

## Intentional Android host fixes

These fixes are in Android adapters; the authoritative core remains untouched.

1. Carry fractional nanoseconds between display callbacks before feeding the
   source 30 ms simulation clock. Truncating each 120 Hz callback would lose time.
2. Reject stale scene/epoch/content receipts and preserve the pending final battle
   frame until its actual matching battle HUD has been checked.
3. Rebuild every GL resource after context loss while retaining CPU session,
   animation and presentation state; ordinary SurfaceView recreation resumes
   a still-foreground activity correctly.
4. Prepare Tech Specs frames against the uploaded immutable model asset identity,
   allowing the actual 3D robot/vehicle preview to render under strict validation.
5. Invalidate cached command ranges when a completed transform/focus is acknowledged,
   so the real enabled controls return without requiring a second unit selection.
6. Prime Modern resources before changing the selected graphics mode. A preparation
   failure leaves the previous mode and a recoverable settings screen.
7. Keep generic results/messages separate from source menu artwork, and provide
   unrestricted production scrolling with matching clipped touch targets.
8. Recover from initial audio-device opening failures with a bounded retry while
   retaining original preload requests, so effects can play after recovery.
9. Match the iOS four-tap bilinear supersampling filter. Consult both texture
   and depth-renderbuffer limits; use the native target on allocation failure.
   Preserve a reported GL context loss for the Android context recreation path.
10. Correct the Android 12 installation mismatch in 0.1.2. The reported phone
    ran API 31, while 0.1.1 declared minimum API 36. Both Java platform calls
    and the native build now use the Android 12 compatibility boundary; newer
    Android callbacks are guarded rather than loaded during older-OS startup.
11. Draw clearer Android control chrome using the source red, steel and white
    palette: button plates/borders, enabled and disabled states, captions and
    HUD labels. Original art, menu layout, control geometry and game rules are
    preserved; these drawing changes are in the Android UI adapter.
12. Use the canonical character-name text entry for tactical and battle labels
    in both forms. The second text entry is a quotation, not a vehicle name;
    selecting it by form replaced the transformed unit's name and prevented
    its HUD portrait lookup. The final public-touch regression verifies the
    displayed name and rendered portrait across transformation.

The historical 0.1.1 host integration probe completed Mission 1, captured its
objective, presented results, opened Mission 2 and restored its checkpoint
through the real menus. It also rejected one framebuffer allocation and verified
a successful native-resolution frame followed by restored supersampling. The report is
`validation/resume-0.1.1/game-probe-report.json`.

## Validation boundaries

Upstream unit tests, source-derived rendering fixtures and an actual AndroidGame
session are different evidence levels. The gameplay probe navigates the real
menus and controls using public touch calls; it never grants unlocks, fabricates
session state or calls core actions behind the UI. Its desktop GLES context is
still not a Samsung driver or an Android application process.

The 0.1.2 release package contains ARM64 and x86_64 libraries so its exact bytes
can be used for both phone installation and emulator checks. The default source
build remains ARM64. An x86_64 Android emulator result establishes that runtime
path; it does not establish ARM64 GPU behavior on a physical Samsung phone.
Host GLES screenshots, Android emulator screenshots and physical-phone screenshots
must identify their origin. Prior 0.1.1 checks do not automatically validate 0.1.2.

Full Story, Showdown and Arena playthroughs on physical devices, device
audio/video, TalkBack use, interruptions, background process death, sustained
thermal behavior and later Android versions require their own recorded results.
See `VALIDATION.md` for checks actually completed at this checkpoint. No claim
of 100% verified parity is made from host tests or a successful build.
