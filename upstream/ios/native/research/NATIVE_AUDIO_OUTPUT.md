# Native Apple audio output adapter

> Current integration: `NATIVE_CAMPAIGN_UI.md` records the 0.4.0/build 4 event wiring, independent Music/Sound output preferences and current host source hashes. The original dispatch, no-loop and lifetime evidence below remains applicable; its readiness statements belong to the earlier adapter checkpoint.

`platform/apple/AppleAudio.hpp/.mm` implements a separate native iOS audio host for the recovered `AudioEvents` dispatcher. It is ready for Apple compilation and integration. It has not been compiled against an Apple SDK or exercised on an iPhone in this checkpoint, and no device audibility is claimed.

## API and ownership

`G1AppleAudio` is an Objective-C++ object whose public calls belong on the main thread:

```objc
G1AppleAudio* audio = [[G1AppleAudio alloc] initWithArchive:archive];
[audio request:g1::MissionAudioRequest{0,0}];
[audio request:g1::MissionAudioRequest{0,1}];
[audio request:g1::MissionAudioRequest{0,2}];
```

Those three optional startup calls are the original eight-slot preload groups. Playback requests must come from an actual recovered game event; initialization does not choose or invent a soundtrack. `request:` returns the original dispatch return value, which is **not a generic success flag**. Check `operational` and `diagnostics` for output failures. `setForegroundActive:` connects to the application's foreground/background lifecycle. `diagnostics` returns a JSON-compatible dictionary for the existing test report.

The host owns a native copy of the data archive, decoded `AVAudioPCMBuffer` objects and a pool of independent player nodes. It does not retain any original executable or old audio library. Every active voice separately retains its buffer so a later cache unload cannot free PCM still scheduled for playback.

Root owns the portable `AudioEvents` module and its original-function validation. The host consumes its typed `load`, `unload`, `stop_all` and `play_once` actions. Slot IDs, resource keys, music resume state and configuration gates remain in that dispatcher. Cache allocation/decode errors are recorded and disable this output instance; failed loads are not reported as successful playback.

## Exact no-loop evidence and correction

The original no-loop conclusion comes from the platform event flag, **not** the PCM constructor's channel argument. `0x2b2d2` passes R2=0 to `0x7dc94`; `0x7d66c` stores that value at event offset `+0x1c`. Cocoa code `0x1e3be..0x1e3ca` reads that byte, masks bit 0, and calls the imported `alSourcei` through `0xba4e0`. The parameter literal at `0x1e484` is `0x1007`, `AL_LOOPING`. Consequently these dispatched events request false for looping. The music flags in the table at `0xce94c` select resume handling; they do not imply looping.

An earlier research interpretation of R2=1 at `0x7dce0` as a repeat count was corrected: that argument is the channel count, paired with 22,050 Hz and 16-bit PCM setup. The host does not rely on that erroneous interpretation. The detailed control/platform evidence is preserved by root's `AudioEvents` research record.

The host uses the iOS 11+ `scheduleBuffer:completionCallbackType:completionHandler:` selector, once per play action, with `AVAudioPlayerNodeCompletionDataPlayedBack`. It never requests a loop option or automatically restarts a completed music buffer. A recovered operation 4 is accepted through `AudioEvents` and can explicitly resume the retained music slot.

## PCM and output policy

The 24 original Ogg resources have already been decoded and independently checked by the portable `Audio` audit: all are mono at 22,050 Hz. `decode_audio_slot()` supplies native float PCM. The host validates this corpus format, copies it into a standard noninterleaved float buffer, and connects that format to the engine mixer. The system output graph performs any device-rate conversion. No file-format conversion, sample trimming or synthesized replacement audio is introduced.

The audio session uses `AVAudioSessionCategoryAmbient` and the default mode. Playback needs no microphone input or recording permission. The native mix uses independent voices at default gain. It has an explicit bound of 64 voices; exceeding it records a failure rather than silently stealing a voice. Exact old mixer concurrency, gain and device output bit identity are not established by this adapter.

## Completion, interruptions and lifetime

Node callbacks only enqueue work onto the main queue. A voice ticket changes when its buffers are stopped or reused; an engine generation changes during reconstruction. Delayed callbacks must match both before touching the current voice. An intentional stop therefore cannot be mistaken for the earlier scheduled buffer's accepted completion. The node is stopped only after returning to the main queue, not from the audio callback itself.

Backgrounding pauses retained player requests and the engine, then deactivates the session. Returning to the foreground starts the engine and resumes retained requests if no interruption is active. Interruption notifications pause output; an ended interruption resumes retained requests only when the system's `shouldResume` option allows it. Otherwise those requests are stopped. This lifecycle handling is native policy, not a claim of original platform timing parity.

An audio engine configuration change rebuilds the native engine and discards invalidated voices. A media-services reset also recreates cached audio buffers from original data. Neither path invents a fresh play/loop request. Dropped voices and reset counts are reported. All observer callbacks run on the main operation queue; `setForegroundActive`, `diagnostics` and object destruction must also remain on the main thread.

The report distinguishes requests received, buffers decoded, one-shot buffers scheduled, accepted `dataPlayedBack` callbacks, explicit stops, active/paused/playing node counts, engine state, interruptions, configuration changes and errors. These are framework state/callback observations. They are not a microphone measurement and do not prove that the user heard audio, that the device was unmuted, or that original mix fidelity is complete.

## Primary Apple references

- [Schedule a buffer with a completion callback type](https://developer.apple.com/documentation/avfaudio/avaudioplayernode/schedulebuffer%28_%3Acompletioncallbacktype%3Acompletionhandler%3A%29?language=objc): the selector used here is available from iOS 11, rather than the newer similarly named APIs in current documentation.
- [AVAudioPlayerNode](https://developer.apple.com/documentation/avfaudio/avaudioplayernode): scheduling/stop semantics and the warning against stopping a player inside its completion callback.
- [Ambient audio category](https://developer.apple.com/documentation/avfaudio/avaudiosession/category-swift.struct/ambient).
- [Handling audio interruptions](https://developer.apple.com/documentation/AVFAudio/handling-audio-interruptions): interruption type and resumption option handling.
- [Engine configuration changes](https://developer.apple.com/documentation/avfaudio/avaudioengineconfigurationchangenotification).
- [Media-services reset notification](https://developer.apple.com/documentation/avfaudio/avaudiosession/mediaserviceswereresetnotification).

## Validation boundary and integration

A bounded independent source review found no concrete lifetime/cache/callback blocker under the stated main-thread contract. Portable decoder and dispatcher checks are documented separately; they are not an Apple host test. No fake framework or simulated speaker test is used to claim success.

The application target needs `AppleAudio.mm` plus `AVFAudio.framework`. The header imports the root-owned portable `AudioEvents.hpp`. The next required gate is an actual Apple Objective-C++ compile/link, followed by native event integration and device checks for real sound, mute/route behavior, background/resume, interruption/reset recovery, overlapping effects, and music completion/resume. `NATIVE_AUDIO_OUTPUT.json` pins this source checkpoint and its unverified boundaries.
