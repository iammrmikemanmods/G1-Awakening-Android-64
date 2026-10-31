#include "g1/Mission.hpp"

#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::size_t checks = 0;

void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

g1::Bytes stream(std::uint8_t count, std::initializer_list<std::uint16_t> words) {
    g1::Bytes bytes{1, count};
    for (const auto word : words) {
        bytes.push_back(static_cast<std::uint8_t>(word >> 8U));
        bytes.push_back(static_cast<std::uint8_t>(word & 0xffU));
    }
    return bytes;
}

void rejects(const g1::Bytes& bytes, const char* message) {
    try {
        const g1::MissionStream parsed(bytes);
        (void)parsed;
    } catch (const g1::FormatError&) {
        ++checks;
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        // Constructed format-contract fixtures contain no original game payloads.
        // Original-data parity is tested separately by verify_missions.py using
        // the independent audit reference and the user's local original IPA.
        rejects({}, "empty stream accepted");
        rejects({1}, "partial header accepted");
        rejects({2, 0}, "unknown version accepted");
        rejects({1, 0, 0}, "partial word accepted");
        rejects(stream(0, {0}), "trailing command accepted");
        rejects(stream(2, {0}), "missing declared instruction accepted");
        rejects(stream(1, {11}), "unobserved opcode 11 accepted");
        rejects(stream(1, {0xffff}), "unknown opcode accepted");
        rejects(stream(1, {12, 4, 0, 0, 0, 0}), "unknown opcode 12 subtype accepted");
        rejects(stream(1, {7, 1, 10, 11, 3}), "missing opcode 7 groups accepted");
        rejects(stream(1, {15, 0xffff}), "oversized one-word record list accepted");
        rejects(stream(1, {17, 0xffff}), "oversized three-word record list accepted");
        rejects(stream(1, {21, 0xffff}), "oversized four-word record list accepted");
        rejects(stream(1, {7, 0xffff}), "oversized outer nested list accepted");
        rejects(stream(1, {7, 1, 10, 11, 1, 0xffff}), "oversized nested pairs accepted");
        rejects(stream(1, {7, 1, 10, 11, 2, 0xffff}), "oversized nested group accepted");
        rejects(g1::Bytes(g1::Archive::max_resource_size + 1U, 0), "resource byte cap not enforced");

        auto original = stream(1, {6, 1, 0x8000, 0xfffe, 0xffff, 42});
        const g1::MissionStream preserved(original);
        original.assign(original.size(), 0); // Public result must own its words.
        const auto& one = preserved.commands().at(0);
        check(one.argument_words_u16 == std::vector<std::uint16_t>({1, 0x8000, 0xfffe, 0xffff, 42}),
              "unsigned words or result ownership changed");
        check(one.byte_offset == 2U && one.byte_size == 12U && one.instruction_index == 0U,
              "single-command byte span incorrect");

        const g1::MissionStream mixed(stream(3, {0, 15, 2, 0xffff, 0x8000, 12, 3, 10, 11, 12, 13}));
        check(mixed.commands().size() == 3U && mixed.declared_instruction_count() == 3U,
              "mixed command count incorrect");
        check(mixed.commands()[1].byte_offset == 4U && mixed.commands()[1].byte_size == 8U &&
              mixed.commands()[2].byte_offset == 12U && mixed.commands()[2].byte_size == 12U,
              "variable-layout command boundaries incorrect");

        const auto nested = stream(1, {7, 2, 10, 11, 1, 2, 12, 13, 14, 15,
                                      20, 21, 2, 1, 22, 23, 1, 25, 26});
        const g1::MissionStream parsed_nested(nested);
        check(parsed_nested.commands()[0].argument_words_u16.size() == 18U &&
              parsed_nested.commands()[0].byte_size == nested.size() - 2U,
              "nested record boundaries incorrect");
        const auto three_groups = stream(2, {7, 1, 4, 5, 3,
                                              0, 1, 6, 7, 2, 8, 9, 10, 11, 0});
        const g1::MissionStream general(three_groups);
        check(general.commands()[0].argument_words_u16 ==
                  std::vector<std::uint16_t>({1, 4, 5, 3, 0, 1, 6, 7, 2, 8, 9, 10, 11}),
              "opcode 7 independent group counts not preserved");
        check(general.commands()[1].opcode == 0U &&
              general.commands()[1].byte_offset == 30U,
              "opcode 7 third group consumed the following opcode");
        for (std::size_t size = 0; size < nested.size(); ++size) {
            const g1::Bytes truncated(nested.begin(), nested.begin() + static_cast<std::ptrdiff_t>(size));
            rejects(truncated, "nested command prefix accepted as complete");
        }
        std::cout << "Passed " << checks << " mission framing contract checks; no gameplay execution.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
