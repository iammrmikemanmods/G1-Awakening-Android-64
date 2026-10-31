#include "g1/BitmapFont.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace g1 {
namespace {

std::int8_t signed_byte(std::uint8_t value) noexcept {
    return value < 128 ? static_cast<std::int8_t>(value) :
        static_cast<std::int8_t>(static_cast<int>(value) - 256);
}

std::int32_t signed_word(std::uint32_t value) noexcept {
    return value <= 0x7fffffffU ? static_cast<std::int32_t>(value) :
        -1 - static_cast<std::int32_t>(0xffffffffU - value);
}

std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return signed_word(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

std::int32_t subtract(std::int32_t a, std::int32_t b) noexcept {
    return signed_word(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

std::int32_t original_draw_position(std::int32_t value) noexcept {
    const auto low = static_cast<std::uint32_t>(value) & 0xffffU;
    return low < 0x8000U ? static_cast<std::int32_t>(low) : static_cast<std::int32_t>(low) - 65536;
}

class Reader {
public:
    explicit Reader(const Bytes& bytes) : bytes_(bytes) {}
    std::uint8_t byte() {
        if (position_ == bytes_.size()) throw FormatError("truncated bitmap font descriptor");
        return bytes_[position_++];
    }
    std::uint16_t le16() {
        const auto a = byte();
        return static_cast<std::uint16_t>(std::uint16_t(a) | (std::uint16_t(byte()) << 8U));
    }
    std::uint32_t le32() {
        const auto a = le16();
        return std::uint32_t(a) | (std::uint32_t(le16()) << 16U);
    }
    std::u32string characters(std::size_t count) {
        if (count == 0) return {};
        const auto a = byte();
        const auto length = (std::size_t(a) << 8U) | byte();
        if (length > bytes_.size() - position_) throw FormatError("truncated bitmap font character map");
        const auto* start = reinterpret_cast<const char*>(bytes_.data() + position_);
        auto result = bitmap_font_utf8(std::string_view(start, length));
        position_ += length;
        if (result.size() != count) throw FormatError("bitmap font character count mismatch");
        for (const auto c : result)
            if (c > 0xffffU) throw FormatError("bitmap font character is outside original 16-bit identity");
        return result;
    }
    bool finished() const noexcept { return position_ == bytes_.size(); }
private:
    const Bytes& bytes_;
    std::size_t position_{};
};

Bytes unique_resource(const Archive& archive, std::uint32_t id, std::uint32_t key) {
    const auto found = archive.find(id, key);
    if (found.size() != 1) throw FormatError("bitmap font resource is missing or ambiguous");
    return archive.read(found.front());
}

void bounded_text(std::u32string_view text) {
    if (text.size() > 1024U * 1024U) throw FormatError("bitmap font text exceeds native bound");
    for (const auto c : text)
        if (c == 0 || c > 0x10ffffU || (c >= 0xd800U && c <= 0xdfffU))
            throw FormatError("bitmap font text contains an invalid Unicode scalar");
}

RgbaImage decode_font_bitmap(const Bytes& bytes, std::uint32_t color_key) {
    // The actual font family includes one 4-bit BMP and two 8-bit BMPs with
    // two zero trailer bytes included in biSizeImage. Keep the existing strict
    // support-image decoder unchanged; this is a separately bounded profile.
    if(bytes.size()<54 || bytes.size()>Archive::max_resource_size || bytes[0]!='B' || bytes[1]!='M')
        throw FormatError("invalid font bitmap size or signature");
    const auto word=[&](std::size_t at) {
        if(at>bytes.size() || bytes.size()-at<4)throw FormatError("truncated font bitmap field");
        return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at+1])<<8U) |
            (std::uint32_t(bytes[at+2])<<16U) | (std::uint32_t(bytes[at+3])<<24U);
    };
    const auto half=[&](std::size_t at) {
        return std::uint16_t(std::uint16_t(bytes.at(at)) | (std::uint16_t(bytes.at(at+1))<<8U));
    };
    const auto depth=half(28);
    if(word(2)!=bytes.size() || word(6)!=0 || word(14)!=40 || half(26)!=1 ||
       (depth!=4 && depth!=8) || word(30)!=0)
        throw FormatError("unsupported font bitmap header profile");
    const auto width=word(18),raw_height=word(22);
    const bool top_down=(raw_height&0x80000000U)!=0;
    const auto height=top_down ? ~raw_height+1U : raw_height;
    if(width==0 || width>8192 || height==0 || height>8192 ||
       std::uint64_t(width)*height>16U*1024U*1024U)
        throw FormatError("font bitmap dimensions exceed bounds");
    const auto declared_colors=word(46);
    const auto palette_size=declared_colors ? declared_colors : (1U<<depth);
    if(palette_size>(1U<<depth) || word(50)>palette_size)
        throw FormatError("unsupported font bitmap palette size");
    const auto pixels_begin=std::size_t(word(10));
    const auto palette_end=54U+std::size_t(palette_size)*4U;
    if(palette_end>bytes.size() || pixels_begin<palette_end || pixels_begin>bytes.size())
        throw FormatError("font bitmap pixel offset exceeds or overlaps palette");
    const auto stride=((std::size_t(width)*depth+31U)/32U)*4U;
    const auto pixel_bytes=stride*height;
    if(pixel_bytes>bytes.size()-pixels_begin)throw FormatError("truncated font bitmap rows");
    const auto trailer=bytes.size()-pixels_begin-pixel_bytes;
    if(trailer!=0 && (trailer!=2 || bytes[bytes.size()-2]!=0 || bytes.back()!=0))
        throw FormatError("unsupported font bitmap trailer");
    const auto declared_pixels=word(34);
    if(declared_pixels!=0 && declared_pixels!=pixel_bytes+trailer)
        throw FormatError("font bitmap row byte count mismatch");
    std::array<std::array<std::uint8_t,4>,256> palette{};
    for(std::size_t i=0;i<palette_size;++i) {
        const auto value=word(54U+i*4U);
        palette[i]=(value&0x00ffffffU)==(color_key&0x00ffffffU) ?
            std::array<std::uint8_t,4>{{255,0,255,0}} :
            std::array<std::uint8_t,4>{{std::uint8_t(value>>16U),std::uint8_t(value>>8U),std::uint8_t(value),255}};
    }
    RgbaImage result{width,height,Bytes(std::size_t(width)*height*4U)};
    for(std::size_t y=0;y<height;++y) {
        const auto source=pixels_begin+(top_down ? y : std::size_t(height)-1U-y)*stride;
        for(std::size_t x=0;x<width;++x) {
            const auto index=depth==8 ? bytes[source+x] :
                static_cast<std::uint8_t>((bytes[source+x/2U]>>((x%2U)==0 ? 4U : 0U))&0xfU);
            if(index>=palette_size)throw FormatError("font bitmap references missing palette entry");
            std::copy(palette[index].begin(),palette[index].end(),result.pixels.begin()+
                static_cast<std::ptrdiff_t>((y*width+x)*4U));
        }
    }
    return result;
}

} // namespace

std::u32string bitmap_font_utf8(std::string_view bytes) {
    if (bytes.size() > Archive::max_resource_size)
        throw FormatError("bitmap font UTF-8 input exceeds bound");
    std::u32string result;
    for (std::size_t at = 0; at < bytes.size();) {
        const auto first = static_cast<std::uint8_t>(bytes[at++]);
        std::uint32_t value{}, minimum{};
        std::size_t continuation{};
        if (first > 0 && first < 0x80U) value = first;
        else if (first >= 0xc2U && first <= 0xdfU) { value = first & 0x1fU; minimum = 0x80U; continuation = 1; }
        else if (first >= 0xe0U && first <= 0xefU) { value = first & 0x0fU; minimum = 0x800U; continuation = 2; }
        else if (first >= 0xf0U && first <= 0xf4U) { value = first & 0x07U; minimum = 0x10000U; continuation = 3; }
        else throw FormatError("invalid bitmap font UTF-8 leading byte");
        if (continuation > bytes.size() - at) throw FormatError("truncated bitmap font UTF-8 scalar");
        for (std::size_t n = 0; n < continuation; ++n) {
            const auto next = static_cast<std::uint8_t>(bytes[at++]);
            if ((next & 0xc0U) != 0x80U) throw FormatError("invalid bitmap font UTF-8 continuation");
            value = (value << 6U) | (next & 0x3fU);
        }
        if (value < minimum || value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU))
            throw FormatError("invalid bitmap font UTF-8 scalar");
        result.push_back(static_cast<char32_t>(value));
    }
    return result;
}

BitmapFontDescriptor::BitmapFontDescriptor(const Bytes& bytes, std::uint32_t image_width,
                                           std::uint32_t image_height) {
    if (bytes.size() > Archive::max_resource_size || image_width == 0 || image_height == 0 ||
        image_width > 65535U || image_height > 65535U)
        throw FormatError("bitmap font descriptor/image dimensions outside supported bounds");
    Reader reader(bytes);
    if (reader.le32() != 2) throw FormatError("unsupported bitmap font descriptor version");
    (void)reader.byte(); // Original header byte +4 is retained by the loader but not these methods.
    metrics_.height = signed_byte(reader.byte());
    metrics_.tracking = signed_byte(reader.byte());
    metrics_.leading = signed_byte(reader.byte());
    if (metrics_.height <= 0) throw FormatError("bitmap font has nonpositive height");
    metrics_.draw_y_adjustment = metrics_.height > 15 ? 5 : 0;
    metrics_.line_height = metrics_.height + metrics_.leading + (metrics_.height > 15 ? 10 : 0);
    const auto glyph_count = reader.le16(), blank_count = reader.le16();
    if (glyph_count > 4096U || blank_count > 4096U)
        throw FormatError("bitmap font record count exceeds native bound");
    const auto characters = reader.characters(glyph_count);
    glyphs_.reserve(glyph_count);
    for (const auto character : characters) {
        BitmapGlyph item;
        item.character = character;
        item.source_x = reader.le16(); item.source_y = reader.le16();
        item.width = reader.byte(); item.height = reader.byte();
        item.offset_x = signed_byte(reader.byte()); item.offset_y = signed_byte(reader.byte());
        item.advance = signed_byte(reader.byte());
        if (reader.byte() != 0) throw FormatError("unsupported bitmap font glyph reserved field");
        if (item.width == 0 || item.height == 0 || item.width > 127 || item.height > 127 ||
            std::uint32_t(item.source_x) + item.width > image_width ||
            std::uint32_t(item.source_y) + item.height > image_height)
            throw FormatError("bitmap font glyph source rectangle exceeds image");
        if (!glyphs_.empty() && character < glyphs_.back().character)
            throw FormatError("bitmap font character map is not sorted for original search");
        glyphs_.push_back(item);
    }
    const auto blank_characters = reader.characters(blank_count);
    blanks_.reserve(blank_count);
    for (const auto character : blank_characters)
        blanks_.push_back({character, reader.byte(), signed_byte(reader.byte())});
    if (!reader.finished()) throw FormatError("trailing bytes in bitmap font descriptor");
}

const BitmapGlyph* BitmapFontDescriptor::glyph(char32_t character) const noexcept {
    // Match the original midpoint search rather than changing which duplicate
    // identity is selected (the actual slot-5 descriptor contains duplicates).
    std::int32_t low = 0, high = static_cast<std::int32_t>(glyphs_.size()) - 1;
    while (high >= low) {
        const auto middle = (low + high) / 2;
        const auto& item = glyphs_[static_cast<std::size_t>(middle)];
        if (item.character == character) return &item;
        if (item.character > character) high = middle - 1;
        else low = middle + 1;
    }
    return nullptr;
}

const BitmapBlank* BitmapFontDescriptor::blank(char32_t character) const noexcept {
    for (const auto& item : blanks_) if (item.character == character) return &item;
    return nullptr;
}

std::int32_t BitmapFontDescriptor::advance(char32_t character) const noexcept {
    if (const auto* item = blank(character)) return item->advance;
    if (const auto* item = glyph(character)) return item->advance;
    return 0;
}

BitmapTextMeasure BitmapFontDescriptor::measure(std::u32string_view text, std::int32_t max_width) const {
    bounded_text(text);
    BitmapTextMeasure result;
    for (const auto character : text) {
        const auto width = advance(character);
        const auto step = add(width, metrics_.tracking);
        if (max_width >= 0 && subtract(max_width, result.width) < std::max(step, width)) break;
        result.width = add(result.width, step);
        ++result.characters;
    }
    if (result.characters) result.width = subtract(result.width, metrics_.tracking);
    return result;
}

std::vector<BitmapGlyphDraw> BitmapFontDescriptor::layout(std::u32string_view text,
    std::int32_t x, std::int32_t y, std::int32_t max_width, std::int32_t max_height) const {
    bounded_text(text);
    std::vector<BitmapGlyphDraw> result;
    std::int32_t pen{};
    y = add(y, metrics_.draw_y_adjustment);
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (const auto* spacing = blank(text[index])) {
            pen = add(pen, add(spacing->advance, metrics_.tracking));
            continue;
        }
        const auto* item = glyph(text[index]);
        if (!item) continue;
        std::int32_t width = item->width, height = item->height;
        if (max_width >= 0)
            width = std::max(0, std::min(width, subtract(subtract(max_width, pen), item->offset_x)));
        if (max_height >= 0)
            height = std::max(0, std::min(height, subtract(max_height, item->offset_y)));
        result.push_back({index, item->source_x, item->source_y, width, height,
                          original_draw_position(add(add(x, pen), item->offset_x)),
                          original_draw_position(add(y, item->offset_y))});
        pen = add(pen, add(item->advance, metrics_.tracking));
    }
    return result;
}

const std::array<BitmapFontBinding, 12>& bitmap_font_bindings() noexcept {
    static constexpr std::array<BitmapFontBinding, 12> bindings{{
        {1134,608}, {609,597}, {984,1276}, {525,1005}, {688,491}, {1273,1017},
        {449,1060}, {469,542}, {378,1290}, {567,1108}, {804,965}, {775,707}
    }};
    return bindings;
}

BitmapFontAssets load_bitmap_font_assets(const Archive& archive, std::size_t font_slot) {
    if (font_slot >= bitmap_font_bindings().size()) throw FormatError("bitmap font slot is out of range");
    const auto binding = bitmap_font_bindings()[font_slot];
    const auto properties = unique_resource(archive, binding.image_property_resource_id, 0xe179f4beU);
    if (properties.size() != 24 && properties.size() != 32)
        throw FormatError("unsupported bitmap font image property record");
    Reader reader(properties);
    std::array<std::optional<std::uint32_t>, 4> fields;
    while (!reader.finished()) {
        const auto property = reader.le32(), value = reader.le32();
        const auto slot = property == 6 ? 0U : property == 9 ? 1U : property == 14 ? 2U : property == 16 ? 3U : 4U;
        if (slot == fields.size() || fields[slot]) throw FormatError("unsupported or repeated bitmap font image property");
        fields[slot] = value;
    }
    if (!fields[0] || !fields[1] || !fields[2] || *fields[0] != 0xffffffffU || (*fields[2] >> 24U) != 2U ||
        (fields[3] && *fields[3] != 1U))
        throw FormatError("unsupported bitmap font image source profile");
    const auto image_id = *fields[2] & 0x00ffffffU;
    const auto bmp = archive.find(image_id, 0xb717945fU);
    const auto png = archive.find(image_id, 0xb7178678U);
    if(bmp.size()+png.size()!=1)throw FormatError("bitmap font image identity is ambiguous");
    RgbaImage image;
    if(!bmp.empty())image=decode_font_bitmap(archive.read(bmp.front()),*fields[1]);
    else {
        image=decode_png(archive.read(png.front()));
        // Original property9 is an RGB transparency key, independent of the
        // BMP/PNG container. Preserve existing source alpha for all other pixels.
        for(std::size_t at=0;at<image.pixels.size();at+=4) {
            const auto rgb=(std::uint32_t(image.pixels[at])<<16U)|
                (std::uint32_t(image.pixels[at+1])<<8U)|image.pixels[at+2];
            if(rgb==(*fields[1]&0xffffffU))image.pixels[at+3]=0;
        }
    }
    BitmapFontDescriptor descriptor(unique_resource(archive, binding.descriptor_resource_id, 0xf4e02223U), image.width, image.height);
    return {std::move(image), std::move(descriptor), image_id, font_slot};
}

} // namespace g1
