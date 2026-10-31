#pragma once

#include "g1/Archive.hpp"
#include "g1/Image.hpp"

#include <array>
#include <string_view>

namespace g1 {

struct BitmapGlyph {
    char32_t character{};
    std::uint16_t source_x{}, source_y{};
    std::uint8_t width{}, height{};
    std::int8_t offset_x{}, offset_y{}, advance{};
};

struct BitmapBlank {
    char32_t character{};
    std::uint8_t classification{};
    std::int8_t advance{};
};

struct BitmapFontMetrics {
    std::int32_t height{}, leading{}, tracking{};
    // Original virtual metric +8 includes ten extra pixels above height 15.
    std::int32_t line_height{}, draw_y_adjustment{};
};

struct BitmapTextMeasure {
    std::int32_t width{};
    std::size_t characters{};
};

struct BitmapGlyphDraw {
    std::size_t character_index{};
    std::uint16_t source_x{}, source_y{};
    std::int32_t width{}, height{}, x{}, y{};
};

// Bounded version-2 CGraphicFont data, independently recovered from the
// original 0x14ada loader and 0x14668..0x14ad8 font methods. Glyph identities
// are Unicode scalars; no case conversion, substitute glyph or new kerning
// is invented. Font descriptor glyph order (including duplicates) is kept.
class BitmapFontDescriptor {
public:
    BitmapFontDescriptor(const Bytes&, std::uint32_t image_width,
                         std::uint32_t image_height);
    const std::vector<BitmapGlyph>& glyphs() const noexcept { return glyphs_; }
    const std::vector<BitmapBlank>& blanks() const noexcept { return blanks_; }
    BitmapFontMetrics metrics() const noexcept { return metrics_; }
    const BitmapGlyph* glyph(char32_t) const noexcept;
    const BitmapBlank* blank(char32_t) const noexcept;
    bool contains(char32_t c) const noexcept { return blank(c) || glyph(c); }
    std::int32_t advance(char32_t) const noexcept;

    // Original 0x14a54 width/fit behavior, including signed tracking and its
    // subtraction after the last accepted character. A negative maximum is
    // unbounded. Missing glyphs have zero advance but still count tracking.
    BitmapTextMeasure measure(std::u32string_view, std::int32_t max_width = -1) const;

    // Original 0x148aa draw plan with no external graphics clipping rectangle.
    // Right/bottom extent limits crop individual source rectangles; a negative
    // extent is unbounded. Missing glyphs emit no draw and do not move this
    // draw pen (unlike the original measurement behavior above).
    std::vector<BitmapGlyphDraw> layout(std::u32string_view, std::int32_t x = 0,
                                       std::int32_t y = 0,
                                       std::int32_t max_width = -1,
                                       std::int32_t max_height = -1) const;

private:
    BitmapFontMetrics metrics_;
    std::vector<BitmapGlyph> glyphs_;
    std::vector<BitmapBlank> blanks_;
};

// Strict UTF-8 conversion for native host callers. Invalid encodings, NUL,
// surrogate values and scalars beyond U+10ffff are rejected.
std::u32string bitmap_font_utf8(std::string_view);

struct BitmapFontBinding {
    std::uint32_t descriptor_resource_id{}, image_property_resource_id{};
};
// Original font-manager table 0xcf4b4; entries are slot identities, not sizes.
const std::array<BitmapFontBinding, 12>& bitmap_font_bindings() noexcept;

struct BitmapFontAssets {
    RgbaImage image;
    BitmapFontDescriptor descriptor;
    std::uint32_t image_resource_id{};
    std::size_t font_slot{};
};

// Current asset boundary is the original keyed, indexed BMP font slots
// 0/1/2/3/11. PNG-property conversion is deliberately rejected pending its
// own original-renderer evidence. No proprietary pixels are embedded in code.
BitmapFontAssets load_bitmap_font_assets(const Archive&, std::size_t font_slot);

} // namespace g1
