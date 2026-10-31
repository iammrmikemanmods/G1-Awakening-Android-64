#pragma once

#include "g1/Archive.hpp"

#include <optional>

namespace g1 {

struct TextEntry {
    std::uint16_t id{};
    std::uint16_t source_offset{};
    std::string utf8;
};

// Strict decoder for the audited resource-1 variant (key 0x69e4c505).
// Entries retain source order; IDs are sparse and are not mission text indices.
// The native product reads strings from iphone.big, never from the old binary.
class TextTable {
public:
    explicit TextTable(const Bytes&);
    const std::vector<TextEntry>& entries() const noexcept { return entries_; }
    const std::string& at(std::uint16_t id) const;
    bool contains(std::uint16_t id) const noexcept;

private:
    std::vector<TextEntry> entries_;
};

TextTable load_text_table(const Archive&);

// Recovered switch 0x445c8: 486 indices, with five alternate-story variants.
// Returns the low 16 bits of its 0x21ffXXXX localization tag. Out-of-range
// indices throw FormatError rather than allowing the original -1 tag onward.
std::uint16_t script_text_id(std::int32_t index, bool alternate = false);

// The unit loader adds four to each first/second raw BE16 text index, stores
// the low 16 bits, and callers sign-extend it before the switch above.
// This function preserves that arithmetic; it does not guess a unit's name.
std::int32_t unit_text_index(std::uint16_t raw_index) noexcept;

} // namespace g1
