#pragma once

#include "g1/Atlas.hpp"

namespace g1 {

// Strict metadata decoder for the original single-image, narrow-coordinate
// version-1 2D overlay profile used by resource 256. The original 0x5eb30
// loader, crop constructors and animation tables were compared separately.
//
// Its input image dimensions come from resolving constructor resource 1000,
// not from a tag embedded in this descriptor. It has no v2 opaque tag, no
// M3G 512-pixel UV adjustment and no +1-pixel crop-origin adjustment.
//
// This bounded decoder intentionally rejects the unrecovered profiles:
// wide coordinates, images larger than 256 pixels, extra image references,
// transformed variants, explicit frame bounds and extra points. These are
// not present in resource 256. It does not decode images, project a world
// anchor, schedule a player or issue a draw.
class OverlayDescriptor {
public:
    OverlayDescriptor(const Bytes& bytes, std::uint32_t image_width,
                      std::uint32_t image_height);

    std::uint8_t version() const noexcept { return 1; }
    const std::vector<AtlasRectangle>& rectangles() const noexcept { return rectangles_; }
    const std::vector<AtlasVariant>& variants() const noexcept { return variants_; }
    const std::vector<AtlasFrame>& frames() const noexcept { return frames_; }
    const std::vector<std::vector<AtlasStep>>& animations() const noexcept { return animations_; }

private:
    std::vector<AtlasRectangle> rectangles_;
    std::vector<AtlasVariant> variants_;
    std::vector<AtlasFrame> frames_;
    std::vector<std::vector<AtlasStep>> animations_;
};

} // namespace g1
