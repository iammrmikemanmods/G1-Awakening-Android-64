#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace g1 {

using Bytes = std::vector<std::uint8_t>;

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Resource {
    std::uint32_t index;
    std::uint32_t group_id;
    std::uint16_t group_ordinal;
    std::uint32_t resource_id; // Observed range-base + ordinal identity.
    std::uint32_t key;
    std::uint32_t offset;
    std::uint32_t stored_size;
    std::uint32_t expanded_size;
    bool compressed;
};

// Implements the FGIB variant observed in G1 Awakening iOS 1.1.3.
// It does not execute embedded code or interpret gameplay resources.
class Archive {
public:
    static constexpr std::size_t max_archive_size = 256U * 1024U * 1024U;
    static constexpr std::size_t max_resource_size = 32U * 1024U * 1024U;

    explicit Archive(Bytes bytes);
    const std::vector<Resource>& resources() const noexcept { return resources_; }
    std::size_t group_count() const noexcept { return group_count_; }
    // Cached identity for native checkpoint/data pairing. Adler32 detects
    // accidental changes; it is not a cryptographic authenticity claim.
    std::uint32_t content_checksum() const noexcept { return checksum_; }
    std::size_t byte_size() const noexcept { return bytes_.size(); }
    Bytes read(std::size_t index) const;
    std::vector<std::size_t> find(std::uint32_t resource_id, std::uint32_t key) const;

private:
    Bytes bytes_;
    std::vector<Resource> resources_;
    std::size_t group_count_{};
    std::uint32_t checksum_{};
};

Bytes read_file(const std::string& path, std::size_t limit = Archive::max_archive_size);
Bytes inflate_exact(const std::uint8_t* data, std::size_t size, std::size_t expected);
std::uint32_t adler32_bytes(const Bytes& bytes);
const char* resource_kind(const Bytes& bytes) noexcept;

} // namespace g1
