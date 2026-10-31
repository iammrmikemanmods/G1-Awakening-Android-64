#include "g1/Audio.hpp"
#include <cstring>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: audio_probe input.ogg output.f32le");
        const auto audio = g1::decode_vorbis(g1::read_file(argv[1],g1::Archive::max_resource_size));
        static_assert(sizeof(float) == 4, "PCM probe requires IEEE32 floats");
        std::ofstream out(argv[2],std::ios::binary);
        for (const auto value : audio.samples) {
            std::uint32_t word = 0;
            std::memcpy(&word,&value,4);
            for (unsigned shift=0;shift<32;shift+=8) out.put(static_cast<char>((word >> shift) & 255));
        }
        if (!out) throw std::runtime_error("cannot write PCM output");
        std::cout << audio.sample_rate << ' ' << unsigned(audio.channels) << ' ' << audio.frames() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n'; return 1;
    }
}
