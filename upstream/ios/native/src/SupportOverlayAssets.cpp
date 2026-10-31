#include "g1/SupportOverlayAssets.hpp"
#include "g1/IndexedBitmap.hpp"

#include <array>
#include <optional>
#include <utility>

namespace g1 {
namespace {

Bytes read_unique(const Archive& archive, std::uint32_t resource_id, std::uint32_t key) {
    const auto matches = archive.find(resource_id, key);
    if (matches.size() != 1)
        throw FormatError("support overlay resource identity is missing or ambiguous");
    return archive.read(matches.front());
}

std::uint32_t property_word(const Bytes& bytes, std::size_t offset) noexcept {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8U) |
           (std::uint32_t(bytes[offset + 2]) << 16U) | (std::uint32_t(bytes[offset + 3]) << 24U);
}

} // namespace

SupportOverlayAssets load_support_overlay_assets(const Archive& archive) {
    const auto properties = read_unique(archive, 1000, 0xe179f4beU);
    if (properties.size() != 24)
        throw FormatError("unsupported support overlay property record size");
    std::array<std::optional<std::uint32_t>, 3> values;
    for (std::size_t offset = 0; offset < properties.size(); offset += 8U) {
        const auto property = property_word(properties, offset);
        const auto value = property_word(properties, offset + 4U);
        const std::size_t slot = property == 6 ? 0U : property == 9 ? 1U : property == 14 ? 2U : 3U;
        if (slot == values.size() || values[slot])
            throw FormatError("unsupported or repeated support overlay image property");
        values[slot] = value;
    }
    if (!values[0] || !values[1] || !values[2] ||
        *values[0] != 0xffffffffU || *values[2] != 0x020003e5U)
        throw FormatError("unsupported support overlay image source profile");

    const auto image_resource_id = *values[2] & 0x00ffffffU;
    auto image = decode_indexed_bmp(read_unique(archive, image_resource_id, 0xb717945fU), *values[1]);
    OverlayDescriptor descriptor(read_unique(archive, 256, 0xf4e02223U), image.width, image.height);
    if (descriptor.animations().size() <= 2)
        throw FormatError("support overlay percentage animation is missing");
    return {std::move(image), std::move(descriptor), image_resource_id};
}

} // namespace g1
