#pragma once

#include "g1/Archive.hpp"
#include <array>

namespace g1 {

struct M3GObject {
    std::size_t index; // File object numbering starts at 1; 0 is a null reference.
    std::uint8_t type;
    std::size_t section;
    std::size_t payload_offset;
    std::size_t payload_size;
};

// Validates and decompresses the M3G 1.0 container. Object bodies remain
// serialized; rendering, skeleton evaluation and gameplay are not implemented.
class M3GFile {
public:
    explicit M3GFile(const Bytes& bytes);
    const std::vector<M3GObject>& objects() const noexcept { return objects_; }
    const std::vector<Bytes>& sections() const noexcept { return sections_; }
    const std::string& authoring_field() const noexcept { return authoring_field_; }
    bool has_external_references() const noexcept { return external_references_; }
private:
    std::vector<Bytes> sections_;
    std::vector<M3GObject> objects_;
    std::string authoring_field_;
    bool external_references_{};
};

} // namespace g1
