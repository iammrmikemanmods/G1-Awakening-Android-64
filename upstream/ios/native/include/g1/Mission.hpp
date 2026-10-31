#pragma once

#include "g1/Archive.hpp"

namespace g1 {

struct MissionCommand {
    std::size_t instruction_index;
    std::size_t byte_offset; // Offset of the opcode in the original resource.
    std::size_t byte_size;   // Opcode and all its argument words.
    std::uint16_t opcode;
    // Words remain unsigned and uninterpreted, including counts and sentinels.
    std::vector<std::uint16_t> argument_words_u16;
};

// Native C++ decoder for the framing observed in G1 Awakening iOS 1.1.3
// resources 310..339. No guest CPU, compatibility runtime or original machine
// code is executed. Command behavior is NOT implemented here: decoded words
// must not be treated as verified gameplay semantics.
//
// The version/count header is two bytes, followed by big-endian u16 words.
// Unknown opcodes (including unobserved opcode 11), unknown layout subtypes,
// incomplete arguments and trailing bytes are rejected with FormatError.
// The resource-size limit is the same as Archive::max_resource_size.
class MissionStream {
public:
    explicit MissionStream(const Bytes& bytes);

    std::uint8_t version() const noexcept { return version_; }
    std::uint8_t declared_instruction_count() const noexcept { return count_; }
    const std::vector<MissionCommand>& commands() const noexcept { return commands_; }

private:
    std::uint8_t version_{};
    std::uint8_t count_{};
    std::vector<MissionCommand> commands_;
};

} // namespace g1
