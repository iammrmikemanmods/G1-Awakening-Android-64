#include "g1/IndexedBitmap.hpp"

#include <array>

namespace g1 {
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw FormatError(message);
}

std::uint32_t read32(const Bytes& bytes, std::size_t offset) {
    require(offset <= bytes.size() && bytes.size() - offset >= 4,
            "truncated indexed BMP field");
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8U) |
           (std::uint32_t(bytes[offset + 2]) << 16U) | (std::uint32_t(bytes[offset + 3]) << 24U);
}

std::uint16_t read16(const Bytes& bytes, std::size_t offset) {
    require(offset <= bytes.size() && bytes.size() - offset >= 2,
            "truncated indexed BMP field");
    return std::uint16_t(std::uint16_t(bytes[offset]) |
                         (std::uint16_t(bytes[offset + 1]) << 8U));
}

} // namespace

RgbaImage decode_indexed_bmp(const Bytes& bytes, std::optional<std::uint32_t> color_key) {
    require(bytes.size() >= 54 && bytes.size() <= Archive::max_resource_size,
            "indexed BMP exceeds size bounds");
    require(bytes[0] == 'B' && bytes[1] == 'M', "invalid indexed BMP signature");
    require(read32(bytes, 2) == bytes.size(), "indexed BMP file size mismatch");
    require(read32(bytes, 6) == 0, "unsupported indexed BMP reserved fields");
    require(read32(bytes, 14) == 40, "unsupported indexed BMP information header");
    require(read16(bytes, 26) == 1 && read16(bytes, 28) == 8 && read32(bytes, 30) == 0,
            "unsupported indexed BMP planes, depth or compression");

    const auto width = read32(bytes, 18);
    const auto raw_height = read32(bytes, 22);
    const bool top_down = (raw_height & 0x80000000U) != 0;
    const auto height = top_down ? (~raw_height + 1U) : raw_height;
    // Bound both allocation and row arithmetic before calculating byte spans.
    require(width > 0 && width <= 8192 && height > 0 && height <= 8192 &&
                std::uint64_t(width) * height <= 16U * 1024U * 1024U,
            "indexed BMP dimensions exceed bounds");
    const auto declared_colors = read32(bytes, 46);
    const auto palette_count = declared_colors == 0 ? 256U : declared_colors;
    require(palette_count <= 256 && read32(bytes, 50) <= palette_count,
            "unsupported indexed BMP palette size");
    const std::size_t palette_end = 54U + std::size_t(palette_count) * 4U;
    const std::size_t pixels_begin = read32(bytes, 10);
    require(palette_end <= bytes.size() && pixels_begin >= palette_end && pixels_begin <= bytes.size(),
            "indexed BMP pixel offset overlaps or exceeds its palette");
    const std::size_t stride = (std::size_t(width) + 3U) & ~std::size_t(3U);
    const std::size_t pixel_bytes = stride * height;
    const auto declared_pixel_bytes = read32(bytes, 34);
    require((declared_pixel_bytes == 0 || declared_pixel_bytes == pixel_bytes) &&
                pixel_bytes == bytes.size() - pixels_begin,
            "indexed BMP row data size mismatch");

    std::array<std::array<std::uint8_t, 4>, 256> palette{};
    for (std::size_t index = 0; index < palette_count; ++index) {
        const auto color = read32(bytes, 54U + index * 4U);
        if (color_key && (color & 0x00ffffffU) == (*color_key & 0x00ffffffU)) {
            // Original 0x1caa8..0x1cb0e uses this fixed transparent word even
            // when the supplied key is a different RGB color.
            palette[index] = {{255, 0, 255, 0}};
        } else {
            palette[index] = {{std::uint8_t(color >> 16U), std::uint8_t(color >> 8U),
                               std::uint8_t(color), 255}};
        }
    }

    RgbaImage image{width, height, Bytes(std::size_t(width) * height * 4U)};
    for (std::size_t y = 0; y < height; ++y) {
        const auto stored_row = top_down ? y : std::size_t(height) - 1U - y;
        const auto source = pixels_begin + stored_row * stride;
        const auto destination = y * width * 4U;
        for (std::size_t x = 0; x < width; ++x) {
            const auto index = bytes[source + x];
            require(index < palette_count, "indexed BMP pixel references a missing palette entry");
            for (std::size_t channel = 0; channel < 4; ++channel)
                image.pixels[destination + x * 4U + channel] = palette[index][channel];
        }
    }
    return image;
}

} // namespace g1
