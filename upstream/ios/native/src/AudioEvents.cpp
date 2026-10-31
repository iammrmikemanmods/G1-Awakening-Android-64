#include "g1/AudioEvents.hpp"

namespace g1 {
namespace {
constexpr std::array<std::uint32_t,24> resources{{938,1153,974,1074,954,1001,622,547,
    1186,590,885,932,504,711,1081,863,992,1099,392,959,1215,947,797,429}};
}

std::uint32_t audio_slot_resource(std::uint8_t slot) {
    if (slot>=resources.size()) throw FormatError("audio slot is outside original table");
    return resources[slot];
}

PcmAudio decode_audio_slot(const Archive& archive,std::uint8_t slot) {
    const auto found=archive.find(audio_slot_resource(slot),0x32fd99e1U);
    if (found.size()!=1) throw FormatError("original audio resource is missing or ambiguous");
    return decode_vorbis(archive.read(found[0]));
}

AudioDispatch dispatch_audio_request(AudioControlState& state,const AudioConfiguration& config,
                                     AudioHostState host,MissionAudioRequest request) {
    if (state.last_music_slot < -1 || state.last_music_slot>23)
        throw FormatError("invalid native audio resume slot");
    AudioDispatch result;
    const auto stop=[&] {
        if (host.playing) {
            result.actions.push_back({AudioActionKind::stop_all,0});
            host.playing=false;
        }
    };
    const auto play=[&](std::int32_t slot) {
        if (slot<0 || slot>23) return;
        if (slot<=3) {
            if (!state.loaded[std::size_t(slot)]) {
                for (std::size_t i=0;i<4;++i)
                    if (state.loaded[i]) {
                        result.actions.push_back({AudioActionKind::unload,static_cast<std::uint8_t>(i)});
                        state.loaded[i]=false;
                    }
                result.actions.push_back({AudioActionKind::load,static_cast<std::uint8_t>(slot)});
                state.loaded[std::size_t(slot)]=true;
            }
            stop();
        }
        if (!(config.enabled_a3 || config.enabled_a4) ||
            (slot<=3 && config.suppress_music_a0) || !host.enabled || !state.loaded[std::size_t(slot)]) return;
        state.last_played_slot=slot;
        if (slot<=3) state.last_music_slot=slot;
        result.actions.push_back({AudioActionKind::play_once,static_cast<std::uint8_t>(slot)});
    };
    switch (request.operation) {
    case 0:
        if (request.argument<0 || request.argument>2)
            throw FormatError("audio preload group exceeds original table");
        for (std::int32_t slot=request.argument*8;slot<(request.argument+1)*8;++slot) {
            if (slot>=1 && slot<=3) state.loaded[std::size_t(slot)]=false;
            else {
                result.actions.push_back({AudioActionKind::load,static_cast<std::uint8_t>(slot)});
                state.loaded[std::size_t(slot)]=true;
            }
        }
        break;
    case 1: case 3: stop(); break;
    case 2: break; // Original queries playing, but false argument never stops.
    case 4:
        if (host.enabled) {
            stop();
            if (state.last_music_slot>=0) play(state.last_music_slot);
        }
        result.return_value=true;
        break;
    case 5: play(request.argument); break;
    case 6: result.return_value=host.playing; break;
    default: break;
    }
    return result;
}
} // namespace g1
