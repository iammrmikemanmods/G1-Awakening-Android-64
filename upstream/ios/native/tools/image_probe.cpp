#include "g1/Image.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: image_probe source.png output.rgba");
        const auto image = g1::decode_png(g1::read_file(argv[1], g1::Archive::max_resource_size));
        std::ofstream out(argv[2], std::ios::binary);
        out.write(reinterpret_cast<const char*>(image.pixels.data()),
                  static_cast<std::streamsize>(image.pixels.size()));
        if (!out) throw std::runtime_error("Cannot write RGBA output");
        std::cout << image.width << ' ' << image.height << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n'; return 1;
    }
}
