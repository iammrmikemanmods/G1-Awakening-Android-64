#include "g1/IndexedBitmap.hpp"
#include "g1/SupportOverlayAssets.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

unsigned checks{};
unsigned rejected{};

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    ++checks;
}

template<class Operation>
void reject(Operation operation) {
    try { operation(); }
    catch (const g1::FormatError&) { ++rejected; return; }
    throw std::runtime_error("unsupported or malformed support asset was accepted");
}

void put16(g1::Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = std::uint8_t(value);
    bytes.at(offset + 1) = std::uint8_t(value >> 8U);
}

void put32(g1::Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = std::uint8_t(value >> (i * 8U));
}

g1::Bytes bitmap(std::uint32_t width, std::int32_t height,
                 const std::vector<std::uint32_t>& palette, const g1::Bytes& stored_pixels) {
    const auto rows = std::uint32_t(height < 0 ? -height : height);
    const auto stride = (width + 3U) & ~3U;
    const auto offset = std::uint32_t(54U + palette.size() * 4U);
    g1::Bytes bytes(offset + stride * rows);
    bytes[0] = 'B'; bytes[1] = 'M';
    put32(bytes, 2, std::uint32_t(bytes.size()));
    put32(bytes, 10, offset); put32(bytes, 14, 40);
    put32(bytes, 18, width); put32(bytes, 22, std::uint32_t(height));
    put16(bytes, 26, 1); put16(bytes, 28, 8);
    put32(bytes, 46, std::uint32_t(palette.size()));
    for (std::size_t i = 0; i < palette.size(); ++i) put32(bytes, 54U + i * 4U, palette[i]);
    for (std::size_t y = 0; y < rows; ++y)
        for (std::size_t x = 0; x < width; ++x)
            bytes[offset + y * stride + x] = stored_pixels.at(y * width + x);
    return bytes;
}

g1::Bytes descriptor() {
    // Independent tiny v1 fixture: exact edge crop, two signed-offset parts,
    // an empty frame, and three explicit animations with big-endian durations.
    return {1,0,0,1,0, 1,0,2,2, 0,1,0,0, 0,2,
            2,0,255,254,0,127,128,0, 0,0, 0,3,
            2,0,0,30,1,127,255, 1,0,0,60, 1,1,0,90};
}

g1::Bytes properties() {
    return {6,0,0,0,255,255,255,255, 9,0,0,0,255,0,255,0,
            14,0,0,0,229,3,0,2};
}

g1::Archive asset_archive(const g1::Bytes& metadata, const g1::Bytes& bmp,
                          const g1::Bytes& image_properties, bool image_key_present = true) {
    const std::array<std::uint32_t, 3> ids{{256,997,1000}};
    const std::array<std::uint32_t, 3> keys{{0xf4e02223U,
        image_key_present ? 0xb717945fU : 0xb717945eU,0xe179f4beU}};
    const std::array<g1::Bytes, 3> payloads{{metadata,bmp,image_properties}};
    // FGIB has three one-member groups, four member-table records (sentinel
    // included), and uncompressed resources with the original four-byte flag.
    g1::Bytes bytes(88);
    put32(bytes, 0, 0x42494746U); put32(bytes, 4, 0x00800001U);
    put32(bytes, 8, 32); put32(bytes, 12, 3);
    put32(bytes, 16, 56); put32(bytes, 20, 3); put32(bytes, 24, 88);
    for (std::size_t i = 0; i < 3; ++i) {
        put32(bytes, 32U + i * 8U, ids[i]);
        put16(bytes, 36U + i * 8U, 1); put16(bytes, 38U + i * 8U, std::uint16_t(i));
        put32(bytes, 56U + i * 8U, keys[i]);
        put32(bytes, 60U + i * 8U, std::uint32_t(bytes.size()));
        bytes.insert(bytes.end(), {4,0,0,0});
        bytes.insert(bytes.end(), payloads[i].begin(), payloads[i].end());
    }
    put32(bytes, 84, std::uint32_t(bytes.size()));
    put32(bytes, 28, std::uint32_t(bytes.size() - 88U));
    return g1::Archive(std::move(bytes));
}

void bitmap_contract() {
    const auto bmp = bitmap(3, 2, {0x12ff00ffU,0x00ff0000U,0x800000ffU}, {0,1,2,2,0,1});
    const auto image = g1::decode_indexed_bmp(bmp, 0xaaff00ffU);
    const g1::Bytes expected{0,0,255,255, 255,0,255,0, 255,0,0,255,
                             255,0,255,0, 255,0,0,255, 0,0,255,255};
    check(image.width == 3 && image.height == 2 && image.pixels == expected,
          "BMP bottom-up rows, stride, channels or key alpha changed");
    const auto top_down = bitmap(3, -2, {0x12ff00ffU,0x00ff0000U,0x800000ffU}, {2,0,1,0,1,2});
    check(g1::decode_indexed_bmp(top_down, 0x00ff00ffU).pixels == expected,
          "BMP top-down row normalization changed");
    auto opaque_expected = expected;
    for (std::size_t i = 3; i < opaque_expected.size(); i += 4) opaque_expected[i] = 255;
    check(g1::decode_indexed_bmp(bmp).pixels == opaque_expected,
          "disabled BMP key did not force every palette alpha opaque");

    const auto colors = bitmap(9, 1,
        {0x00ff00ffU,0x12ff00ffU,0xffff00ffU,0,0x0000ff00U,0x000000ffU,
         0x00ff0000U,0x80ffffffU,0x00ff00feU}, {0,1,2,3,4,5,6,7,8});
    const g1::Bytes keyed{255,0,255,0, 255,0,255,0, 255,0,255,0, 0,0,0,255,
                         0,255,0,255, 0,0,255,255, 255,0,0,255, 255,255,255,255, 255,0,254,255};
    check(g1::decode_indexed_bmp(colors, 0xaaff00ffU).pixels == keyed,
          "original low24-bit palette comparison changed");
    auto red_keyed = opaque_expected;
    for (std::size_t i : {std::size_t(8),std::size_t(16)}) {
        red_keyed[i] = 255; red_keyed[i + 1] = 0; red_keyed[i + 2] = 255; red_keyed[i + 3] = 0;
    }
    check(g1::decode_indexed_bmp(bmp, 0xccff0000U).pixels == red_keyed,
          "non-magenta key did not produce the original transparent magenta word");

    for (std::size_t size = 0; size < bmp.size(); ++size)
        reject([&] { (void)g1::decode_indexed_bmp(g1::Bytes(bmp.begin(), bmp.begin() + size)); });
    for (const auto& change : std::vector<std::pair<std::size_t,std::uint32_t>>{
             {2,0},{6,1},{10,53},{10,65},{10,1000},{14,124},{18,0},{18,0xffffffffU},
             {18,8193},{22,0},{22,0x80000000U},{22,8193},{30,1},{34,1},
             {46,257},{46,0},{50,4}}) {
        auto invalid = bmp; put32(invalid, change.first, change.second);
        reject([&] { (void)g1::decode_indexed_bmp(invalid); });
    }
    for (const auto& change : std::vector<std::pair<std::size_t,std::uint16_t>>{{26,2},{28,24}}) {
        auto invalid = bmp; put16(invalid, change.first, change.second);
        reject([&] { (void)g1::decode_indexed_bmp(invalid); });
    }
    auto invalid = bmp; invalid[0] = 'X';
    reject([&] { (void)g1::decode_indexed_bmp(invalid); });
    invalid = bmp; invalid[66] = 3;
    reject([&] { (void)g1::decode_indexed_bmp(invalid); });
    invalid = bmp; invalid.push_back(0); put32(invalid, 2, std::uint32_t(invalid.size()));
    reject([&] { (void)g1::decode_indexed_bmp(invalid); });
    invalid = bmp; put32(invalid, 34, 8);
    check(g1::decode_indexed_bmp(invalid, 0x00ff00ffU).pixels == expected,
          "valid explicit BMP image size was rejected");
    const auto default_palette = bitmap(1,1,std::vector<std::uint32_t>(256,0x12020304U),{255});
    auto implicit_palette = default_palette; put32(implicit_palette,46,0);
    check(g1::decode_indexed_bmp(implicit_palette).pixels == g1::Bytes({2,3,4,255}),
          "zero BMP color count did not select the 256-entry palette");
}

void descriptor_contract() {
    const auto bytes = descriptor();
    const g1::OverlayDescriptor value(bytes,3,2);
    const auto& crop = value.rectangles().at(0);
    check(crop.x == 1 && crop.y == 0 && crop.width == 2 && crop.height == 2,
          "version-1 crop incorrectly received the version-2 origin adjustment");
    const auto& parts = value.frames().at(0).parts;
    check(parts.size() == 2 && parts[0].offset_x == -1 && parts[0].offset_y == -2 &&
              parts[1].offset_x == 127 && parts[1].offset_y == -128,
          "version-1 signed frame offsets changed");
    check(value.frames().at(1).parts.empty() && !value.frames().at(0).explicit_bounds &&
              value.animations().at(0).at(0).duration_units == 30 &&
              value.animations().at(0).at(1).duration_units == 32767,
          "version-1 empty frame or duration byte order changed");
    for (std::size_t size = 0; size < bytes.size(); ++size)
        reject([&] { (void)g1::OverlayDescriptor(g1::Bytes(bytes.begin(),bytes.begin() + size),3,2); });
    for (const auto& change : std::vector<std::pair<std::size_t,std::uint8_t>>{
             {0,2},{1,1},{3,0},{4,1},{5,2},{7,0},{11,1},{12,1},{16,1},
             {22,1},{25,1},{26,0},{27,0},{28,2},{29,128},{30,0}}) {
        auto invalid = bytes; invalid[change.first] = change.second;
        reject([&] { (void)g1::OverlayDescriptor(invalid,3,2); });
    }
    auto extra = bytes; extra.push_back(0);
    reject([&] { (void)g1::OverlayDescriptor(extra,3,2); });
    reject([&] { (void)g1::OverlayDescriptor(bytes,0,2); });
    reject([&] { (void)g1::OverlayDescriptor(bytes,257,2); });
}

void resource_contract() {
    const auto bmp = bitmap(3,2,{0x12ff00ffU,0x00ff0000U,0x800000ffU},{0,1,2,2,0,1});
    const auto metadata = descriptor();
    const auto props = properties();
    const auto loaded = g1::load_support_overlay_assets(asset_archive(metadata,bmp,props));
    check(loaded.image_resource_id == 997 && loaded.image.width == 3 && loaded.image.height == 2 &&
              loaded.descriptor.animations().size() == 3 && loaded.image.pixels[7] == 0,
          "support constructor property-to-image mapping changed");
    auto reordered = props;
    std::swap_ranges(reordered.begin(),reordered.begin() + 8,reordered.begin() + 16);
    check(g1::load_support_overlay_assets(asset_archive(metadata,bmp,reordered)).image.pixels == loaded.image.pixels,
          "distinct original support properties must not depend on record order");
    for (const auto& change : std::vector<std::pair<std::size_t,std::uint32_t>>{
             {0,7},{0,9},{4,0},{20,0x030003e5U},{20,0x020003e4U}}) {
        auto invalid = props; put32(invalid,change.first,change.second);
        reject([&] { (void)g1::load_support_overlay_assets(asset_archive(metadata,bmp,invalid)); });
    }
    auto short_props = props; short_props.pop_back();
    reject([&] { (void)g1::load_support_overlay_assets(asset_archive(metadata,bmp,short_props)); });
    reject([&] { (void)g1::load_support_overlay_assets(asset_archive(metadata,bmp,props,false)); });
    auto missing_animation = metadata;
    missing_animation[26] = 1; missing_animation.resize(34);
    reject([&] { (void)g1::load_support_overlay_assets(asset_archive(missing_animation,bmp,props)); });
}

void actual_archive_contract(const char* path) {
    const g1::Archive archive(g1::read_file(path));
    const auto value = g1::load_support_overlay_assets(archive);
    check(value.image_resource_id == 997 && value.image.width == 69 && value.image.height == 63,
          "actual support image identity or dimensions changed");
    unsigned transparent = 0, opaque = 0;
    for (std::size_t i = 3; i < value.image.pixels.size(); i += 4) {
        if (value.image.pixels[i] == 0) ++transparent;
        else if (value.image.pixels[i] == 255) ++opaque;
        else throw std::runtime_error("actual support image has an unexpected partial alpha");
    }
    check(transparent == 1498 && opaque == 2849 && g1::adler32_bytes(value.image.pixels) == 0x4408ccdcU,
          "actual support source pixels differ from the original palette evidence");
    const auto& metadata = value.descriptor;
    check(metadata.rectangles().size() == 13 && metadata.variants().size() == 13 &&
              metadata.frames().size() == 27 && metadata.animations().size() == 4,
          "actual support descriptor counts changed");
    const auto& crop = metadata.rectangles().at(metadata.variants().at(11).rectangle_index);
    check(crop.x == 0 && crop.y == 0 && crop.width == 32 && crop.height == 14,
          "actual support percentage crop changed");
    const std::array<std::int16_t,5> offsets{{-59,-63,-65,-67,-69}};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const auto& parts = metadata.frames().at(16U + i).parts;
        check(parts.size() == 1 && parts[0].variant_index == 11 && parts[0].offset_x == 0 &&
                  parts[0].offset_y == offsets[i], "actual support percentage rise changed");
    }
    check(metadata.frames().at(21).parts.empty(), "actual support blink frame is not empty");
    std::uint32_t duration = 0;
    for (const auto& step : metadata.animations().at(2)) duration += step.duration_units;
    check(metadata.animations().at(2).size() == 20 && duration == 1680,
          "actual support animation duration changed");
    const auto original = archive.read(archive.find(256,0xf4e02223U).at(0));
    for (std::size_t size = 0; size < original.size(); ++size)
        reject([&] { (void)g1::OverlayDescriptor(g1::Bytes(original.begin(),original.begin() + size),69,63); });
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 2) { std::cerr << "Usage: support_assets_test [iphone.big]\n"; return 2; }
        bitmap_contract(); descriptor_contract(); resource_contract();
        if (argc == 2) actual_archive_contract(argv[1]);
        std::cout << "Support asset contracts passed: " << checks << " checks, " << rejected
                  << " malformed/unsupported inputs rejected; actual archive "
                  << (argc == 2 ? "checked" : "not supplied") << ".\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
