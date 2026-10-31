#include "g1/Image.hpp"

#include "lodepng.h"

#include <cstdlib>
#include <memory>
#include <string>

namespace g1 {
namespace {
constexpr std::size_t max_pixels = 16U * 1024U * 1024U;

struct State {
    LodePNGState value;
    State() { lodepng_state_init(&value); }
    ~State() { lodepng_state_cleanup(&value); }
};

void dimensions(unsigned width, unsigned height) {
    if (width == 0 || height == 0 || width > 8192 || height > 8192 ||
        std::size_t(width) > max_pixels / height)
        throw FormatError("PNG dimensions exceed native texture bounds");
}

void checked(unsigned error, const char* operation) {
    if (error != 0) throw FormatError(std::string(operation) + ": " + lodepng_error_text(error));
}
} // namespace

RgbaImage decode_png(const Bytes& bytes) {
    if (bytes.empty() || bytes.size() > Archive::max_resource_size)
        throw FormatError("PNG input size exceeds resource bounds");
    State state;
    unsigned width = 0, height = 0;
    checked(lodepng_inspect(&width, &height, &state.value, bytes.data(), bytes.size()), "PNG header");
    dimensions(width, height);
    state.value.info_raw.colortype = LCT_RGBA;
    state.value.info_raw.bitdepth = 8;
    // Bound the decompressed source even for sixteen-bit/interlaced PNGs.
    state.value.decoder.zlibsettings.max_output_size = std::size_t(width) * height * 8U +
                                                       std::size_t(height) * 8U + 4096U;
    unsigned char* allocated = nullptr;
    const unsigned result = lodepng_decode(&allocated, &width, &height, &state.value,
                                           bytes.data(), bytes.size());
    std::unique_ptr<unsigned char, decltype(&std::free)> owned(allocated, &std::free);
    checked(result, "PNG decode");
    dimensions(width, height);
    const auto size = std::size_t(width) * height * 4U;
    if (!owned) throw FormatError("PNG decoder returned no pixels");
    return {width, height, Bytes(owned.get(), owned.get() + size)};
}

Bytes encode_png(const RgbaImage& image) {
    dimensions(image.width, image.height);
    if (image.pixels.size() != std::size_t(image.width) * image.height * 4U)
        throw FormatError("RGBA byte count does not match dimensions");
    unsigned char* allocated = nullptr;
    std::size_t size = 0;
    const unsigned result = lodepng_encode32(&allocated, &size, image.pixels.data(),
                                            image.width, image.height);
    std::unique_ptr<unsigned char, decltype(&std::free)> owned(allocated, &std::free);
    checked(result, "PNG encode");
    return Bytes(owned.get(), owned.get() + size);
}
} // namespace g1
