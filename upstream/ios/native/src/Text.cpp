#include "g1/Text.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <set>

namespace g1 {
namespace {

std::uint16_t read16(const Bytes& bytes, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes.at(at)) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes.at(at + 1U)) << 8U));
}

std::uint32_t read32(const Bytes& bytes, std::size_t at) {
    std::uint32_t value{};
    for (std::size_t n = 0; n < 4U; ++n)
        value |= static_cast<std::uint32_t>(bytes.at(at + n)) << (n * 8U);
    return value;
}

// Reject truncated/overlong encodings, surrogates and scalars above U+10FFFF.
// Byte preservation matters: no normalization or replacement is performed.
bool valid_utf8(const Bytes& bytes, std::size_t begin, std::size_t end) {
    for (std::size_t at = begin; at < end;) {
        const auto first = bytes[at++];
        if (first == 0U) return false;
        if (first < 0x80U) continue;
        std::size_t continuation{};
        std::uint32_t value{}, minimum{};
        if (first >= 0xc2U && first <= 0xdfU) {
            continuation = 1U; value = first & 0x1fU; minimum = 0x80U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuation = 2U; value = first & 0x0fU; minimum = 0x800U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation = 3U; value = first & 0x07U; minimum = 0x10000U;
        } else return false;
        if (continuation > end - at) return false;
        for (std::size_t n = 0; n < continuation; ++n) {
            const auto next = bytes[at++];
            if ((next & 0xc0U) != 0x80U) return false;
            value = (value << 6U) | (next & 0x3fU);
        }
        if (value < minimum || value > 0x10ffffU ||
            (value >= 0xd800U && value <= 0xdfffU)) return false;
    }
    return true;
}

// Development reference: original function 0x445c8, executed for every index
// and variant; original image SHA256 0d510516fe3676ee4a0f4dff60ba64e1c5bf79a0c9800f420b1bbdb1c12febfd.
#include "TextIndexMap.inc"

} // namespace

TextTable::TextTable(const Bytes& bytes) {
    if (bytes.size() < 8U || bytes.size() > Archive::max_resource_size)
        throw FormatError("localization resource size is outside the supported range");
    if (read16(bytes, 0) != 0x2000U)
        throw FormatError("unsupported localization header flags");
    const auto count = static_cast<std::size_t>(read16(bytes, 2));
    const auto table_end = 8U + count * 4U;
    if (table_end > bytes.size()) throw FormatError("truncated localization index");
    if (read32(bytes, table_end - 4U) != bytes.size())
        throw FormatError("localization end sentinel does not match payload size");
    std::set<std::uint16_t> ids;
    entries_.reserve(count);
    for (std::size_t n = 0; n < count; ++n) {
        const auto id = read16(bytes, 4U + n * 4U);
        const auto offset = read16(bytes, 6U + n * 4U);
        const auto begin = static_cast<std::size_t>(offset);
        const auto end = n + 1U == count ? bytes.size() :
            static_cast<std::size_t>(read16(bytes, 10U + n * 4U));
        if (!ids.insert(id).second) throw FormatError("duplicate localization string ID");
        if (begin < table_end || begin > end || end > bytes.size() || end - begin < 5U)
            throw FormatError("invalid localization string span");
        if (read32(bytes, begin) != 4U || bytes[end - 1U] != 0U)
            throw FormatError("unsupported localization string wrapper or terminator");
        if (!valid_utf8(bytes, begin + 4U, end - 1U))
            throw FormatError("localization string is not strict NUL-free UTF-8");
        const auto* text = reinterpret_cast<const char*>(bytes.data() + begin + 4U);
        entries_.push_back({id, offset, std::string(text, end - begin - 5U)});
    }
    if (count == 0U && bytes.size() != table_end)
        throw FormatError("unindexed bytes in empty localization resource");
}

const std::string& TextTable::at(std::uint16_t id) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [id](const TextEntry& entry) { return entry.id == id; });
    if (found == entries_.end()) throw FormatError("localization string ID is missing");
    return found->utf8;
}

bool TextTable::contains(std::uint16_t id) const noexcept {
    return std::any_of(entries_.begin(), entries_.end(),
        [id](const TextEntry& entry) { return entry.id == id; });
}

TextTable load_text_table(const Archive& archive) {
    const auto found = archive.find(1U, 0x69e4c505U);
    if (found.size() != 1U) throw FormatError("localization resource 1 is missing or ambiguous");
    return TextTable(archive.read(found.front()));
}

std::uint16_t script_text_id(std::int32_t index, bool alternate) {
    if (index < 0 || static_cast<std::uint32_t>(index) >= text_index_ids.size())
        throw FormatError("mission text index is outside the recovered original switch");
    if (alternate && index >= 373 && index <= 377)
        return alternate_text_ids[static_cast<std::size_t>(index - 373)];
    return text_index_ids[static_cast<std::size_t>(index)];
}

std::int32_t unit_text_index(std::uint16_t raw_index) noexcept {
    const auto stored = (static_cast<std::uint32_t>(raw_index) + 4U) & 0xffffU;
    return stored < 0x8000U ? static_cast<std::int32_t>(stored) :
        static_cast<std::int32_t>(stored) - 0x10000;
}

} // namespace g1
