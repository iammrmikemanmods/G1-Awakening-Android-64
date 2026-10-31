#include "g1/Archive.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>
#include <zlib.h>

namespace g1 {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw FormatError(message);
}

std::uint32_t u32(const Bytes& b, std::size_t pos) {
    require(pos <= b.size() && b.size() - pos >= 4, "truncated 32-bit field");
    return std::uint32_t(b[pos]) | (std::uint32_t(b[pos+1]) << 8U) |
           (std::uint32_t(b[pos+2]) << 16U) | (std::uint32_t(b[pos+3]) << 24U);
}

std::uint16_t u16(const Bytes& b, std::size_t pos) {
    require(pos <= b.size() && b.size() - pos >= 2, "truncated 16-bit field");
    return std::uint16_t(std::uint16_t(b[pos]) | (std::uint16_t(b[pos+1]) << 8U));
}

template<std::size_t N>
bool starts(const Bytes& b, const unsigned char (&prefix)[N]) noexcept {
    return b.size() >= N && std::equal(prefix, prefix+N, b.begin());
}
} // namespace

Bytes read_file(const std::string& path, std::size_t limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw FormatError("cannot open input file");
    const auto end = file.tellg();
    require(end >= 0 && static_cast<std::uint64_t>(end) <= limit, "input exceeds size limit");
    Bytes data(static_cast<std::size_t>(end));
    file.seekg(0);
    if (!data.empty()) file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    require(bool(file), "input file read failed");
    return data;
}

Bytes inflate_exact(const std::uint8_t* data, std::size_t size, std::size_t expected) {
    require(expected <= Archive::max_resource_size, "expanded resource exceeds bound");
    require(size <= std::numeric_limits<uInt>::max(), "compressed resource exceeds zlib bound");
    // One spare output byte detects a stream that expands beyond its declaration.
    Bytes output(expected+1U);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    require(inflateInit(&stream) == Z_OK, "zlib initialization failed");
    const int status = inflate(&stream, Z_FINISH);
    const bool valid = status == Z_STREAM_END && stream.total_out == expected &&
                       stream.total_in == size && stream.avail_in == 0;
    inflateEnd(&stream);
    require(valid, "zlib length/checksum/truncation/trailing-data failure");
    output.resize(expected);
    return output;
}

std::uint32_t adler32_bytes(const Bytes& bytes) {
    require(bytes.size() <= std::numeric_limits<uInt>::max(), "checksum input too large");
    return static_cast<std::uint32_t>(::adler32(1, bytes.data(), static_cast<uInt>(bytes.size())));
}

const char* resource_kind(const Bytes& bytes) noexcept {
    static const unsigned char m3g[] = {0xab,'J','S','R','1','8','4',0xbb,13,10,26,10};
    static const unsigned char png[] = {137,'P','N','G',13,10,26,10};
    static const unsigned char bmp[] = {'B','M'};
    static const unsigned char ogg[] = {'O','g','g','S'};
    if (starts(bytes, m3g)) return "m3g";
    if (starts(bytes, png)) return "png";
    if (starts(bytes, bmp)) return "bmp";
    if (starts(bytes, ogg)) return "ogg";
    return "bin";
}

Archive::Archive(Bytes bytes) : bytes_(std::move(bytes)) {
    require(bytes_.size() >= 32 && bytes_.size() <= max_archive_size, "invalid FGIB size");
    checksum_ = adler32_bytes(bytes_);
    require(u32(bytes_, 0) == 0x42494746U && u32(bytes_, 4) == 0x00800001U,
            "unsupported FGIB magic/version");
    const std::uint64_t group_offset = u32(bytes_, 8);
    const std::uint32_t group_count = u32(bytes_, 12);
    const std::uint64_t member_offset = u32(bytes_, 16);
    const std::uint32_t member_count = u32(bytes_, 20);
    const std::uint64_t body_offset = u32(bytes_, 24);
    const std::uint64_t body_size = u32(bytes_, 28);
    require(group_offset == 32 && group_count > 0 && group_count <= 65535 &&
            member_count > 0 && member_count <= 65535, "invalid FGIB table counts");
    require(group_offset + std::uint64_t(group_count)*8U == member_offset &&
            member_offset + (std::uint64_t(member_count)+1U)*8U == body_offset &&
            body_offset + body_size == bytes_.size(), "FGIB table/body size mismatch");

    struct Owner { std::uint32_t group{}; std::uint16_t ordinal{}; bool assigned{}; };
    std::vector<Owner> owners(member_count);
    std::uint32_t prior_id = 0;
    std::uint64_t prior_range_end = 0;
    for (std::uint32_t n = 0; n < group_count; ++n) {
        const auto pos = static_cast<std::size_t>(group_offset + std::uint64_t(n)*8U);
        const auto id = u32(bytes_, pos);
        const auto count = u16(bytes_, pos+4);
        const auto first = u16(bytes_, pos+6);
        require((n == 0 || id > prior_id) && count > 0 &&
                std::uint32_t(first)+count <= member_count, "invalid FGIB group range/order");
        require(std::uint64_t(id)+count <= 0x100000000ULL &&
                (n == 0 || id >= prior_range_end), "overlapping/overflowing FGIB resource IDs");
        for (std::uint32_t j = first; j < std::uint32_t(first)+count; ++j) {
            require(!owners[j].assigned, "overlapping FGIB group ranges");
            owners[j] = {id, static_cast<std::uint16_t>(j-first), true};
        }
        prior_id = id;
        prior_range_end = std::uint64_t(id)+count;
    }
    require(std::all_of(owners.begin(), owners.end(), [](const Owner& o){return o.assigned;}),
            "unowned FGIB resource");
    require(u32(bytes_, static_cast<std::size_t>(member_offset)+4) == body_offset,
            "FGIB first resource offset mismatch");
    const auto sentinel = static_cast<std::size_t>(member_offset+std::uint64_t(member_count)*8U);
    require(u32(bytes_, sentinel) == 0 && u32(bytes_, sentinel+4) == bytes_.size(),
            "invalid FGIB sentinel");

    resources_.reserve(member_count);
    for (std::uint32_t n = 0; n < member_count; ++n) {
        const auto pos = static_cast<std::size_t>(member_offset+std::uint64_t(n)*8U);
        const auto key = u32(bytes_, pos);
        const auto begin = u32(bytes_, pos+4);
        const auto end = u32(bytes_, pos+12);
        require(begin >= body_offset && begin < end && end <= bytes_.size() && end-begin >= 4,
                "invalid FGIB resource span");
        const auto flags = u32(bytes_, begin);
        std::uint32_t expanded = end-begin-4;
        require(flags == 4 || flags == 0x00800004U, "unsupported FGIB resource flags");
        if (flags == 0x00800004U) {
            require(end-begin >= 12, "truncated FGIB compressed header");
            expanded = u32(bytes_, begin+4);
            require(u32(bytes_, begin+8) == end-begin-12, "FGIB compressed size mismatch");
        }
        require(expanded <= max_resource_size, "FGIB expanded size exceeds bound");
        resources_.push_back({n, owners[n].group, owners[n].ordinal,
                              owners[n].group+owners[n].ordinal, key, begin,
                              end-begin, expanded, flags != 4});
    }
    group_count_ = group_count;
}

Bytes Archive::read(std::size_t index) const {
    require(index < resources_.size(), "resource index out of range");
    const auto& r = resources_[index];
    if (r.compressed) return inflate_exact(bytes_.data()+r.offset+12U, r.stored_size-12U, r.expanded_size);
    return Bytes(bytes_.begin()+r.offset+4U, bytes_.begin()+r.offset+r.stored_size);
}

std::vector<std::size_t> Archive::find(std::uint32_t resource_id, std::uint32_t key) const {
    std::vector<std::size_t> matches;
    for (const auto& r : resources_) {
        if (r.resource_id == resource_id && r.key == key) matches.push_back(r.index);
    }
    return matches; // Do not silently assume resource keys are globally unique.
}

} // namespace g1
