#include "g1/Atlas.hpp"

#include <charconv>
#include <iostream>
#include <string>

namespace {

std::uint32_t parse_u32(const std::string& text) {
    const bool hex = text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    const auto* begin = text.data() + (hex ? 2U : 0U);
    const auto* end = text.data() + text.size();
    std::uint32_t value = 0;
    const auto result = std::from_chars(begin, end, value, hex ? 16 : 10);
    if (result.ec != std::errc{} || result.ptr != end)
        throw g1::FormatError("invalid unsigned 32-bit resource identifier");
    return value;
}

template<class Range, class Function> void list(const Range& range, Function emit) {
    std::cout << '[';
    bool first = true;
    for (const auto& value : range) {
        if (!first) std::cout << ',';
        first = false;
        emit(value);
    }
    std::cout << ']';
}

template<class Range> void numbers(const Range& range) {
    list(range, [](auto value) { std::cout << +value; });
}

void print_atlas(const g1::AtlasDescriptor& atlas) {
    std::cout << "{\"version\":" << unsigned(atlas.version())
              << ",\"wide_coordinates\":" << (atlas.wide_coordinates() ? "true" : "false")
              << ",\"opaque_tag\":" << atlas.opaque_tag() << ",\"rectangles\":";
    list(atlas.rectangles(), [](const auto& rect) {
        numbers(std::array<std::int16_t, 4>{rect.x, rect.y, rect.width, rect.height});
    });
    std::cout << ",\"variants\":";
    list(atlas.variants(), [](const auto& variant) {
        numbers(std::array<std::uint8_t, 2>{variant.rectangle_index, variant.orientation});
    });
    std::cout << ",\"frames\":";
    list(atlas.frames(), [](const auto& frame) {
        std::cout << "{\"parts\":";
        list(frame.parts, [](const auto& part) {
            numbers(std::array<std::int16_t, 3>{part.variant_index, part.offset_x, part.offset_y});
        });
        std::cout << ",\"explicit_bounds\":";
        if (frame.explicit_bounds) numbers(*frame.explicit_bounds);
        else std::cout << "null";
        std::cout << '}';
    });
    std::cout << ",\"extra_points\":";
    list(atlas.extra_points(), [](const auto& point) { numbers(point); });
    std::cout << ",\"animations\":";
    list(atlas.animations(), [](const auto& sequence) {
        list(sequence, [](const auto& step) {
            numbers(std::array<std::uint16_t, 2>{step.frame_index, step.duration_units});
        });
    });
    std::cout << ",\"frame_quads\":[";
    for (std::size_t frame = 0; frame < atlas.frames().size(); ++frame) {
        if (frame != 0U) std::cout << ',';
        list(atlas.quads_for_frame(frame), [](const auto& quad) {
            std::cout << "{\"source_part_index\":" << quad.source_part_index << ",\"positions\":";
            list(quad.positions, [](const auto& vertex) { numbers(vertex); });
            std::cout << ",\"quantized_uv\":";
            list(quad.quantized_uv, [](const auto& uv) { numbers(uv); });
            std::cout << '}';
        });
    }
    std::cout << "]}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        g1::Bytes bytes;
        if (argc == 2) {
            bytes = g1::read_file(argv[1], g1::Archive::max_resource_size);
        } else if (argc == 5 && std::string(argv[1]) == "--archive") {
            const g1::Archive archive(g1::read_file(argv[2]));
            const auto matches = archive.find(parse_u32(argv[3]), parse_u32(argv[4]));
            if (matches.size() != 1U)
                throw g1::FormatError("atlas lookup must match exactly one resource ID/key pair");
            bytes = archive.read(matches.front());
        } else {
            std::cerr << "Usage: atlas_probe descriptor.bin\n"
                      << "   or: atlas_probe --archive iphone.big RESOURCE_ID RESOURCE_KEY\n";
            return 2;
        }
        print_atlas(g1::AtlasDescriptor(bytes));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
