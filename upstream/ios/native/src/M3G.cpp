#include "g1/M3G.hpp"

#include <algorithm>
#include <cstring>
#include <zlib.h>

namespace g1 {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw FormatError(message);
}
std::uint32_t u32(const Bytes& bytes, std::size_t p) {
    require(p <= bytes.size() && bytes.size()-p >= 4, "truncated M3G integer");
    return std::uint32_t(bytes[p]) | (std::uint32_t(bytes[p+1])<<8U) |
           (std::uint32_t(bytes[p+2])<<16U) | (std::uint32_t(bytes[p+3])<<24U);
}
} // namespace

M3GFile::M3GFile(const Bytes& bytes) {
    require(bytes.size() <= Archive::max_resource_size && std::strcmp(resource_kind(bytes), "m3g") == 0,
            "invalid M3G magic/size");
    std::size_t cursor = 12;
    std::size_t cumulative_size = 0;
    bool header_seen = false;
    while (cursor < bytes.size()) {
        require(bytes.size()-cursor >= 13, "truncated M3G section");
        const auto scheme = bytes[cursor];
        const auto total = u32(bytes, cursor+1);
        const auto raw_size = u32(bytes, cursor+5);
        require(scheme <= 1 && total >= 13 && total <= bytes.size()-cursor,
                "invalid M3G section span/compression");
        require(raw_size <= Archive::max_resource_size, "M3G section expansion bound exceeded");
        cumulative_size += raw_size;
        require(cumulative_size <= Archive::max_archive_size, "M3G total expansion bound exceeded");
        const auto checksum = static_cast<std::uint32_t>(::adler32(1, bytes.data()+cursor, total-4));
        require(checksum == u32(bytes, cursor+total-4), "M3G section checksum mismatch");
        Bytes decoded;
        if (scheme == 1) decoded = inflate_exact(bytes.data()+cursor+9, total-13, raw_size);
        else {
            require(total-13 == raw_size, "M3G uncompressed size mismatch");
            decoded.assign(bytes.begin()+static_cast<std::ptrdiff_t>(cursor+9),
                           bytes.begin()+static_cast<std::ptrdiff_t>(cursor+total-4));
        }
        std::size_t position = 0;
        const auto previous_objects = objects_.size();
        while (position < decoded.size()) {
            require(decoded.size()-position >= 5, "truncated M3G object header");
            const auto type = decoded[position];
            const auto length = u32(decoded, position+1);
            require((type <= 22 || type == 255) && length <= decoded.size()-position-5,
                    "invalid M3G object type/span");
            const auto payload = position+5;
            require(!sections_.empty() || (objects_.empty() && type == 0),
                    "M3G first section must contain only its header");
            if (type == 0) {
                require(!header_seen && objects_.empty() && sections_.empty() && scheme == 0,
                        "misplaced/duplicate M3G header");
                require(length >= 12 && decoded[payload] == 1 && decoded[payload+1] == 0 &&
                        decoded[payload+2] <= 1, "unsupported M3G header version");
                require(u32(decoded, payload+3) == bytes.size(), "M3G declared file size mismatch");
                const auto first = decoded.begin()+static_cast<std::ptrdiff_t>(payload+11);
                const auto last = decoded.begin()+static_cast<std::ptrdiff_t>(payload+length);
                const auto nul = std::find(first, last, 0);
                require(nul != last && nul+1 == last, "invalid M3G authoring string termination");
                authoring_field_.assign(first, nul);
                external_references_ = decoded[payload+2] != 0;
                header_seen = true;
            } else require(header_seen, "M3G object precedes header");
            objects_.push_back({objects_.size()+1, type, sections_.size(), payload, length});
            position += std::size_t(length)+5U;
        }
        require(objects_.size() > previous_objects, "M3G section contains no objects");
        sections_.push_back(std::move(decoded));
        cursor += total;
    }
    require(header_seen, "M3G missing header");
    require(sections_.size() >= 2, "M3G missing scene data section");
}

} // namespace g1
