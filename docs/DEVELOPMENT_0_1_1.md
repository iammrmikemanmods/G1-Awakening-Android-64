# Android development update 0.1.1

Date: 31 October 2026. Target: Samsung Galaxy A36, Android 16/API 36 or later.

This is an Android host update over the initial `5cc5a63` source checkpoint.
The authoritative iOS Build 017 commit remains
`be203a911bc26313615ea450758413e5c9cf6462`; all 769 vendored files and all 98
017R1 resources remain byte-identical. The original iOS cinematic is retained.

## What changed

### Audio startup and recovery

An initial AAudio builder/open/start error previously escaped the game constructor
and stopped startup. The Android adapter now records that failure and retries
after its existing 500 ms backoff. Original preload requests continue while the
output device is unavailable, so recovered audio has its effects loaded.

Eight regression scenarios use the real original audio planner, archive and
Vorbis decoder, substituting only the AAudio device calls. They cover healthy
PCM output, all three initial failure points, bounded repeated retries,
background suppression, a separate-thread disconnect callback, retained playback
position and pause-failure recovery.

### iOS rendering parity and GPU allocation pressure

The old single linear framebuffer blit did not match the iOS supersampling
filter. GLES now averages four bilinear samples at quarter-pixel offsets in
the destination footprint, with correct GLES texture orientation. UI remains
at native resolution after the world resolve.

Classic and constrained Modern render directly to the native target. Modern
planning considers both the maximum texture size and the maximum depth
renderbuffer size. If the supersampling target cannot be allocated, a frame
can still render and present at native resolution. A later successful frame
can restore supersampling.

Context loss is kept separate from allocation pressure: a reported GL context
loss reaches the existing Android recovery path, which rejects that frame and
rebuilds graphics resources while retaining the CPU session.

The renderer's 127 checks include an independent CPU pixel oracle at 1.5 times
render resolution, orientation, edges, alpha and inherited GL state. The test
also executes the previous blit and demonstrates that it fails the new pixel
oracle. The gameplay probe injects one incomplete framebuffer result and
observes native 2340 × 1080 presentation followed by restored 3510 × 1620
supersampling. No failure-injection switch is shipped in the Android library.

### Packaging and identity

Gradle APK/AAB packaging now invokes the source/resource verifier. A missing
package, changed original file or unmanifested resource is rejected. Source-only
native/Java compilation and lint still work without redistributing game data.
The actual missing-assets Gradle path was tested, then the exact original
resources were restored and the positive verification passed.

The app is version `0.1.1-dev`, version code `2`, with the same application ID
and signing certificate as `0.1.0-dev`. The in-game credits now accurately
identify the Android port and its iOS opening cinematic.

## Verified campaign progress

The final public-touch host probe passes 1,832 rendered frames, 83 touches and
11 original dialogue commands. It completes Mission 1 through normal movement,
combat and capture, presents the real results, waits for the player's Continue,
and opens Mission 2 through the original campaign controller. Mission 2 becomes
unlocked while Mission 3 remains locked. Continue restores the Mission 2 checkpoint.

The previous transform, movement confirmation, Undo, Tech Specs, Classic/Modern,
battle-return, failed-presentation, pause/resume and graphics recreation checks
remain in the run. The original 72 portable core tests also pass unchanged.

These are actual Android host C++ interactions on desktop GLES. They do not
exercise the Android Java lifecycle, AAudio hardware, MediaPlayer or Samsung's
driver. Android runtime results are recorded separately in [VALIDATION.md](../VALIDATION.md).

## APK identity

| Field | Value |
| --- | --- |
| Filename | `G1-Awakening-Android64-v0.1.1-dev.apk` |
| Size | 39,533,625 bytes |
| ABI | `arm64-v8a` only |
| Minimum / target API | 36 / 36 |
| SHA-256 | `2ff8d0052d75b6d5f897893e7ffb9a92eaab56739ae94c83e52d4febe4a97b37` |
| Signing certificate SHA-256 | `303fcdd4074d308738d01d667dc2c6f1ccca8eee32fc077f367a2ccea91dbf29` |

The original development signing certificate is preserved. The private key is
not part of this repository. Android signature verification, 16 KB APK alignment,
ELF64/ARM64 segment alignment, ZIP CRCs and every packaged resource hash pass.

## Evidence and remaining work

Current reports are in [`validation/resume-0.1.1`](../validation/resume-0.1.1).
The [initial validation record](VALIDATION_0_1_0.md) remains available for comparison.

Physical A36 testing, full Story/Showdown/Arena playthroughs, hardware sound and
video, TalkBack, interruption latency and sustained thermal performance remain
device acceptance work. Heal, upgrade, production and the support-tutorial
route are not newly covered by the Mission 1-to-2 host probe. No whole-game
device certification is claimed.
