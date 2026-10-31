#include "g1/Audio.hpp"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace g1 {
PcmAudio decode_vorbis(const Bytes& bytes) {
    if (bytes.empty() || bytes.size() > Archive::max_resource_size ||
        bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw FormatError("Ogg Vorbis input exceeds resource bounds");
    int error = 0;
    std::unique_ptr<stb_vorbis,decltype(&stb_vorbis_close)> stream(
        stb_vorbis_open_memory(bytes.data(),static_cast<int>(bytes.size()),&error,nullptr),
        &stb_vorbis_close);
    if (!stream || error != VORBIS__no_error)
        throw FormatError("Ogg Vorbis header/setup decoding failed: " + std::to_string(error));
    const auto info = stb_vorbis_get_info(stream.get());
    if (info.channels < 1 || info.channels > 2 || info.sample_rate < 8000 || info.sample_rate > 96000)
        throw FormatError("unsupported native audio channel/rate configuration");
    const auto frames = stb_vorbis_stream_length_in_samples(stream.get());
    constexpr std::size_t max_samples = 16U * 1024U * 1024U;
    if (frames == 0 || static_cast<std::size_t>(frames) > max_samples / info.channels ||
        stb_vorbis_get_error(stream.get()) != VORBIS__no_error)
        throw FormatError("Ogg Vorbis sample count is missing or exceeds decode bounds");
    PcmAudio audio{info.sample_rate,static_cast<std::uint8_t>(info.channels),
                   std::vector<float>(static_cast<std::size_t>(frames) * info.channels)};
    std::size_t written = 0;
    while (written < audio.samples.size()) {
        const auto requested = std::min<std::size_t>(audio.samples.size() - written,8192U * info.channels);
        const int decoded = stb_vorbis_get_samples_float_interleaved(stream.get(),info.channels,
            audio.samples.data() + written,static_cast<int>(requested));
        if (decoded <= 0 || static_cast<std::size_t>(decoded) * info.channels > requested)
            throw FormatError("Ogg Vorbis stream ended before its declared samples");
        written += static_cast<std::size_t>(decoded) * info.channels;
    }
    float extra[2]{};
    if (stb_vorbis_get_samples_float_interleaved(stream.get(),info.channels,extra,info.channels) != 0 ||
        stb_vorbis_get_error(stream.get()) != VORBIS__no_error)
        throw FormatError("Ogg Vorbis stream has an inconsistent final sample count");
    if (!std::all_of(audio.samples.begin(),audio.samples.end(),[](float value){return std::isfinite(value);}))
        throw FormatError("Ogg Vorbis decoder produced nonfinite samples");
    return audio;
}
} // namespace g1
