#include "g1/Mission.hpp"

#include <string>
#include <utility>

namespace g1 {
namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw FormatError(message);
}

class WordReader {
public:
    explicit WordReader(const Bytes& bytes) : bytes_(bytes) {}

    std::size_t position() const noexcept { return position_; }
    std::size_t remaining_words() const noexcept { return (bytes_.size() - position_) / 2U; }

    std::uint16_t word() {
        require(remaining_words() != 0,
                "truncated mission word at byte " + std::to_string(position_));
        const auto result = static_cast<std::uint16_t>(
            (std::uint16_t(bytes_[position_]) << 8U) | std::uint16_t(bytes_[position_ + 1U]));
        position_ += 2U;
        return result;
    }

    void append(std::size_t count, std::vector<std::uint16_t>& output) {
        require(count <= remaining_words(),
                "mission arguments exceed resource at byte " + std::to_string(position_));
        for (std::size_t i = 0; i < count; ++i) output.push_back(word());
    }

    void append_records(std::uint16_t count, std::size_t width,
                        std::vector<std::uint16_t>& output) {
        // Check before multiplying or allocating. All callers use nonzero widths.
        require(std::size_t(count) <= remaining_words() / width,
                "mission record list exceeds resource at byte " + std::to_string(position_));
        append(std::size_t(count) * width, output);
    }

private:
    const Bytes& bytes_;
    std::size_t position_{2};
};

int fixed_argument_count(std::uint16_t opcode) noexcept {
    switch (opcode) {
        case 0: case 1: case 2: case 13: case 14: return 0;
        case 5: case 8: case 10: case 16: return 1;
        case 3: case 9: case 18: case 19: case 20: return 2;
        case 4: return 3;
        case 6: return 5;
        default: return -1;
    }
}

} // namespace

MissionStream::MissionStream(const Bytes& bytes) {
    require(bytes.size() >= 2U && bytes.size() <= Archive::max_resource_size,
            "invalid mission resource size");
    require(bytes[0] == 1U, "unsupported mission version");
    require((bytes.size() - 2U) % 2U == 0U, "partial mission word");
    version_ = bytes[0];
    count_ = bytes[1];
    WordReader reader(bytes);
    require(std::size_t(count_) <= reader.remaining_words(),
            "mission instruction count exceeds available words");
    commands_.reserve(count_);
    for (std::size_t index = 0; index < count_; ++index) {
        const auto offset = reader.position();
        const auto opcode = reader.word();
        MissionCommand command{index, offset, 0, opcode, {}};
        auto& args = command.argument_words_u16;
        const int fixed = fixed_argument_count(opcode);
        if (fixed >= 0) {
            reader.append(static_cast<std::size_t>(fixed), args);
        } else if (opcode == 15U || opcode == 17U || opcode == 21U) {
            const auto count = reader.word();
            args.push_back(count);
            const std::size_t width = opcode == 15U ? 1U : (opcode == 17U ? 3U : 4U);
            reader.append_records(count, width, args);
        } else if (opcode == 12U) {
            const auto subtype = reader.word();
            args.push_back(subtype);
            constexpr std::size_t total_words[] = {4U, 6U, 3U, 5U};
            require(subtype < 4U,
                    "unsupported mission opcode 12 subtype at byte " + std::to_string(offset));
            reader.append(total_words[subtype] - 1U, args);
        } else if (opcode == 7U) {
            const auto count = reader.word();
            args.push_back(count);
            require(std::size_t(count) <= reader.remaining_words() / 3U,
                    "mission opcode 7 list exceeds resource at byte " + std::to_string(offset));
            for (std::size_t record = 0; record < count; ++record) {
                reader.append(3U, args);
                // Original 0x48698..0x487ac: (x, y, group_count), then
                // group_count independently counted lists of coordinate pairs.
                // The third word is a count, not a 0/1/2 layout subtype.
                const auto groups = args.back();
                require(std::size_t(groups) <= reader.remaining_words(),
                        "mission opcode 7 groups exceed resource at byte " +
                            std::to_string(reader.position()));
                for (std::size_t group = 0; group < groups; ++group) {
                    const auto pairs = reader.word();
                    args.push_back(pairs);
                    reader.append_records(pairs, 2U, args);
                }
            }
        } else {
            throw FormatError("unsupported mission opcode " + std::to_string(opcode) +
                              " at byte " + std::to_string(offset));
        }
        command.byte_size = reader.position() - offset;
        commands_.push_back(std::move(command));
    }
    require(reader.position() == bytes.size(),
            "mission instruction count leaves trailing bytes at byte " +
                std::to_string(reader.position()));
}

} // namespace g1
