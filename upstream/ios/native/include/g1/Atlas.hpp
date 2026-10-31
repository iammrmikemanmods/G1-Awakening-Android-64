#pragma once

#include "g1/Archive.hpp"

#include <array>
#include <optional>

namespace g1 {

struct AtlasRectangle {
    std::int16_t x, y, width, height;
};

struct AtlasVariant {
    std::uint8_t rectangle_index;
    // Kept as the original value. Values 4..7 must not be interpreted as a
    // conventional flip-bit mask; the recovered routine treats them alike.
    std::uint8_t orientation;
};

struct AtlasPart {
    std::uint8_t variant_index;
    std::int16_t offset_x, offset_y;
};

struct AtlasFrame {
    std::vector<AtlasPart> parts;
    std::optional<std::array<std::int16_t, 4>> explicit_bounds;
};

struct AtlasStep {
    std::uint8_t frame_index;
    std::uint16_t duration_units; // Unit conversion / playback timing not inferred here.
};

struct AtlasQuad {
    std::size_t source_part_index;
    // Original local XY convention (positive Y up), original vertex order.
    // Original stores truncate to signed 16 bits; z is always zero.
    std::array<std::array<std::int16_t, 3>, 4> positions;
    // Quantized values written by 0x728f0, NOT normalized Metal texture UVs.
    // The later M3G texture transform still needs recovery.
    std::array<std::array<std::int16_t, 2>, 4> quantized_uv;
};

// Framing of the 38 descriptor resources referenced by the original table at
// 0xcf5b0. The original v2 parser at 0x7315c establishes the nonempty layout.
// All observed files include the opaque four-byte tag, including seven with
// no rectangles. The same original loader skips that tag for zero rectangles;
// their original consumption path is unresolved. This decoder preserves their
// observed complete framing and does not claim that loader path was validated.
//
// Versions other than 2, nonboolean flags, invalid references, orientations
// above 7, incomplete fields and trailing bytes are rejected. The descriptor
// owns its decoded values and never executes original machine code.
class AtlasDescriptor {
public:
    explicit AtlasDescriptor(const Bytes& bytes);

    std::uint8_t version() const noexcept { return 2; }
    bool wide_coordinates() const noexcept { return wide_; }
    std::uint32_t opaque_tag() const noexcept { return tag_; }
    const std::vector<AtlasRectangle>& rectangles() const noexcept { return rectangles_; }
    const std::vector<AtlasVariant>& variants() const noexcept { return variants_; }
    const std::vector<AtlasFrame>& frames() const noexcept { return frames_; }
    const std::vector<std::array<std::int16_t, 2>>& extra_points() const noexcept { return points_; }
    const std::vector<std::vector<AtlasStep>>& animations() const noexcept { return animations_; }

    // Reconstructs only the active part quads, in original reverse part order,
    // using the recovered 512x512 atlas adjustment and 0x728f0 arithmetic.
    // No camera, world transform, normalized UV, frame timing, bounding-box
    // synthesis or GPU draw is included. Empty frames yield no quads.
    std::vector<AtlasQuad> quads_for_frame(std::size_t frame_index) const;

private:
    bool wide_{};
    std::uint32_t tag_{};
    std::vector<AtlasRectangle> rectangles_;
    std::vector<AtlasVariant> variants_;
    std::vector<AtlasFrame> frames_;
    std::vector<std::array<std::int16_t, 2>> points_;
    std::vector<std::vector<AtlasStep>> animations_;
};

} // namespace g1
