#pragma once

#include "g1/Audio.hpp"
#include "g1/MissionProgression.hpp"

#include <array>

namespace g1 {

// Original 24-entry tagged resource table at0xce8ec, normalized to the low
// 24-bit Archive identity. All entries use the Ogg resource key0x32fd99e1.
std::uint32_t audio_slot_resource(std::uint8_t slot);
PcmAudio decode_audio_slot(const Archive&,std::uint8_t slot);

struct AudioControlState {
    std::array<bool,24> loaded{};
    std::int32_t last_music_slot{-1}; // Original app+0x9c.
    std::int32_t last_played_slot{-1};
};
struct AudioConfiguration {
    bool suppress_music_a0{};
    bool enabled_a3{true};
    bool enabled_a4{};
};
struct AudioHostState {
    bool enabled{true};
    bool playing{}; // Actual output state supplied by the native host.
};
enum class AudioActionKind { load,unload,stop_all,play_once };
struct AudioAction {
    AudioActionKind kind{};
    std::uint8_t slot{};
};
struct AudioDispatch {
    bool return_value{};
    std::vector<AudioAction> actions;
};

// Original2ad12/2b2d2/2b3ac control flow. This returns requests, never proof of
// audible playback. Operation0 has original eight-slot preload groups0..2;
// play5 accepts slots0..23. Operation6 reports the supplied actual host state.
// Music slots0..3 update the resume slot. That flag alone is not evidence of
// a loop instruction. Each dispatch emits one original platform play request;
// the platform host separately owns completion and interruption handling.
AudioDispatch dispatch_audio_request(AudioControlState&,const AudioConfiguration&,
                                     AudioHostState,MissionAudioRequest);

} // namespace g1
