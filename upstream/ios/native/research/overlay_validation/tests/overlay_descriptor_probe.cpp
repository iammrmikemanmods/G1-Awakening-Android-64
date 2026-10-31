#include "g1/OverlayDescriptor.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 2;
    const g1::Bytes bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    try {
        const g1::OverlayDescriptor descriptor(bytes, std::stoul(argv[2]), std::stoul(argv[3]));
        std::cout << "{\"rectangles\":[";
        bool comma = false;
        for (const auto& rectangle : descriptor.rectangles()) {
            if (comma) std::cout << ',';
            comma = true;
            std::cout << '[' << rectangle.x << ',' << rectangle.y << ',' << rectangle.width
                      << ',' << rectangle.height << ']';
        }
        std::cout << "],\"variants\":[";
        comma = false;
        for (const auto& variant : descriptor.variants()) {
            if (comma) std::cout << ',';
            comma = true;
            const auto& rectangle = descriptor.rectangles()[variant.rectangle_index];
            std::cout << '[' << rectangle.x << ',' << rectangle.y << ',' << rectangle.width
                      << ',' << rectangle.height << ',' << int(variant.orientation) << ']';
        }
        std::cout << "],\"frames\":[";
        comma = false;
        for (const auto& frame : descriptor.frames()) {
            if (comma) std::cout << ',';
            comma = true;
            std::cout << '[';
            bool inner = false;
            for (const auto& part : frame.parts) {
                if (inner) std::cout << ',';
                inner = true;
                std::cout << '[' << int(part.variant_index) << ',' << part.offset_x << ','
                          << part.offset_y << ']';
            }
            std::cout << ']';
        }
        std::cout << "],\"animations\":[";
        comma = false;
        for (const auto& animation : descriptor.animations()) {
            if (comma) std::cout << ',';
            comma = true;
            std::cout << '[';
            bool inner = false;
            for (const auto& step : animation) {
                if (inner) std::cout << ',';
                inner = true;
                std::cout << '[' << int(step.frame_index) << ',' << step.duration_units << ']';
            }
            std::cout << ']';
        }
        std::cout << "]}\n";
        return 0;
    } catch (const g1::FormatError& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
