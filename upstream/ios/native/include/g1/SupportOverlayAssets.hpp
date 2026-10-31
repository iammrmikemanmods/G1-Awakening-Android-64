#pragma once

#include "g1/Image.hpp"
#include "g1/OverlayDescriptor.hpp"

namespace g1 {

struct SupportOverlayAssets {
    RgbaImage image;
    OverlayDescriptor descriptor;
    std::uint32_t image_resource_id{};
};

// Original effect-bank constructor: property resource 1000, descriptor 256.
// Accepts only its recovered three-property profile and resolved BMP image.
// This decodes source samples and metadata; GPU conversion, scheduling and
// projection remain separate native presentation responsibilities.
SupportOverlayAssets load_support_overlay_assets(const Archive& archive);

} // namespace g1
