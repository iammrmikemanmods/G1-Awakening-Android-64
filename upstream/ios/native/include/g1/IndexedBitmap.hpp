#pragma once

#include "g1/Image.hpp"

#include <optional>

namespace g1 {

// Bounded Windows BITMAPINFOHEADER (40-byte), 8-bit BI_RGB decoder. Positive
// and negative row heights are normalized to the RgbaImage top-down contract.
// Other BMP headers, compression and pixel formats are rejected.
//
// The original indexed-image palette operation compares only the low 24 bits
// of a color key. A match becomes transparent magenta; every other entry is
// opaque, irrespective of the reserved/high byte stored in the BMP palette.
RgbaImage decode_indexed_bmp(const Bytes& bytes,
                             std::optional<std::uint32_t> color_key = std::nullopt);

} // namespace g1
