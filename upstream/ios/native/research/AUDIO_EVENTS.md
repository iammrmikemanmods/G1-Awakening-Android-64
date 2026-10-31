# Native audio event routing

`AudioEvents` reconstructs original `0x2ad12` and its play/preload/resume helpers.
The 24 slot identities at original table0xce8ec each resolve to a unique source
Ogg with key0x32fd99e1. The native Vorbis decoder separately passed all24 source
files against an independent decoder. Preload groups contain eight slots;
music slots0..3 are loaded on demand and retain the original resume slot.
The native control model emits load, unload, stop and play requests and retains
the original operation return value. A false return is not a generic host error.

512 synthetic original dispatches matched 3,706 scalar comparisons across
loaded-state masks, audio flags, host availability, playing state and invalid
play slots. No original instruction was patched. The resource loader/releaser
and platform enabled/playing/stop/play methods are explicit recorded boundaries.
The comparisons validate control flow, not an operating-system audio device.
See `audio_event_validation/original_audio_events.json` and
`analysis_tools/audio/events_oracle.py`.

## Loop setting

The original play path passes R2=0 into0x7dc94;0x7d66c stores it in event+0x1c.
The Cocoa event loader at0x1e3be..0x1e3ca reads that byte, masks bit0 and passes it
to imported `_alSourcei` with parameter0x1007, `AL_LOOPING`. Thus these events
explicitly disable looping. The four resume flags at0xce94c do not request loops.
A preliminary interpretation of0x7dce0's value1 as a repeat count was corrected:
that parameter is mono channel count, next to22050Hz and16-bit format fields.

The modern host uses AVAudioEngine/AVAudioPlayerNode and decoded PCM. Actual
completion/interruption/error reports remain distinct from control requests.
The host is described in `NATIVE_AUDIO_OUTPUT.md`; iPhone audibility is untested.
