#include "g1/Mission.hpp"

#include <charconv>
#include <iostream>
#include <string>

namespace {

std::uint32_t parse_u32(const std::string& text) {
    const bool hex = text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    const auto* begin = text.data() + (hex ? 2U : 0U);
    const auto* end = text.data() + text.size();
    std::uint32_t value = 0;
    const auto result = std::from_chars(begin, end, value, hex ? 16 : 10);
    if (result.ec != std::errc{} || result.ptr != end)
        throw g1::FormatError("invalid unsigned 32-bit resource identifier");
    return value;
}

void print_mission(const g1::MissionStream& mission) {
    std::cout << "{\"version\":" << unsigned(mission.version())
              << ",\"declared_instruction_count\":" << unsigned(mission.declared_instruction_count())
              << ",\"commands\":[";
    bool first = true;
    for (const auto& command : mission.commands()) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"instruction_index\":" << command.instruction_index
                  << ",\"byte_offset\":" << command.byte_offset
                  << ",\"byte_size\":" << command.byte_size
                  << ",\"opcode\":" << command.opcode << ",\"argument_words_u16\":[";
        for (std::size_t i = 0; i < command.argument_words_u16.size(); ++i) {
            if (i != 0U) std::cout << ',';
            std::cout << command.argument_words_u16[i];
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        g1::Bytes bytes;
        if (argc == 2) {
            bytes = g1::read_file(argv[1], g1::Archive::max_resource_size);
        } else if (argc == 5 && std::string(argv[1]) == "--archive") {
            const g1::Archive archive(g1::read_file(argv[2]));
            const auto matches = archive.find(parse_u32(argv[3]), parse_u32(argv[4]));
            if (matches.size() != 1U)
                throw g1::FormatError("mission lookup must match exactly one resource ID/key pair");
            bytes = archive.read(matches.front());
        } else {
            std::cerr << "Usage: mission_probe mission.bin\n"
                      << "   or: mission_probe --archive iphone.big RESOURCE_ID RESOURCE_KEY\n";
            return 2;
        }
        print_mission(g1::MissionStream(bytes));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
