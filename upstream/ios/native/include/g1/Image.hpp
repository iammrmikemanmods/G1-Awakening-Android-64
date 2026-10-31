#pragma once

#include "g1/Archive.hpp"

namespace g1 {

// Top-to-bottom rows of straight (not premultiplied) RGBA8 pixels. No display
// color conversion is applied; these are texture samples, not UIKit images.
struct RgbaImage {
    std::uint32_t width{};
    std::uint32_t height{};
    Bytes pixels;
};

RgbaImage decode_png(const Bytes& bytes);
Bytes encode_png(const RgbaImage& image);

} // namespace g1
