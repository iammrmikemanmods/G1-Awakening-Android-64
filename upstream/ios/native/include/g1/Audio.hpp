#pragma once

#include "g1/Archive.hpp"

namespace g1 {
struct PcmAudio {
    std::uint32_t sample_rate{};
    std::uint8_t channels{};
    std::vector<float> samples; // Interleaved, unresampled decoder output.
    std::size_t frames() const noexcept { return channels ? samples.size() / channels : 0; }
};

// Original archive audio uses Ogg Vorbis. Decoding is native compiled code;
// sound event selection, looping/mixing and iOS playback are separate layers.
PcmAudio decode_vorbis(const Bytes&);
} // namespace g1
