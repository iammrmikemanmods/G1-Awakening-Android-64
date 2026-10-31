#include "g1/Atlas.hpp"

#include <string>
#include <utility>

namespace g1 {
namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw FormatError(message);
}

// Explicit modulo conversion reproduces STRH without an implementation-defined
// out-of-range signed cast or a left shift of a negative signed integer.
std::int16_t short_bits(std::int32_t value) noexcept {
    const auto low = static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) & 0xffffU);
    return static_cast<std::int16_t>(low < 0x8000U ? std::int32_t(low) : std::int32_t(low) - 65536);
}

class Reader {
public:
    explicit Reader(const Bytes& bytes) : bytes_(bytes) {}
    std::size_t position() const noexcept { return position_; }
    std::size_t remaining() const noexcept { return bytes_.size() - position_; }
    void need(std::size_t count) const {
        require(count <= remaining(), "truncated atlas field at byte " + std::to_string(position_));
    }
    void records(std::size_t count, std::size_t minimum_bytes) const {
        require(count <= remaining() / minimum_bytes,
                "atlas count exceeds resource at byte " + std::to_string(position_));
    }
    std::uint8_t byte() { need(1); return bytes_[position_++]; }
    std::uint16_t word() {
        need(2);
        const auto value = static_cast<std::uint16_t>(
            (std::uint16_t(bytes_[position_]) << 8U) | bytes_[position_ + 1U]);
        position_ += 2U;
        return value;
    }
    std::int16_t coordinate(bool wide) {
        if (wide) return short_bits(word());
        const auto value = byte();
        return static_cast<std::int16_t>(value < 0x80U ? int(value) : int(value) - 256);
    }
    bool flag() {
        const auto value = byte();
        require(value <= 1U, "unsupported atlas boolean flag");
        return value != 0;
    }

private:
    const Bytes& bytes_;
    std::size_t position_{};
};

} // namespace

AtlasDescriptor::AtlasDescriptor(const Bytes& bytes) {
    require(bytes.size() <= Archive::max_resource_size, "atlas resource exceeds byte limit");
    Reader reader(bytes);
    require(reader.byte() == 2U, "unsupported atlas descriptor version");
    wide_ = reader.flag();
    const auto rectangle_count = reader.word();
    tag_ = std::uint32_t(reader.word()) << 16U;
    tag_ |= reader.word();
    reader.records(rectangle_count, 8U);
    rectangles_.reserve(rectangle_count);
    for (std::size_t i = 0; i < rectangle_count; ++i) {
        const auto x = short_bits(reader.word());
        const auto y = short_bits(reader.word());
        const auto width = short_bits(reader.word());
        const auto height = short_bits(reader.word());
        rectangles_.push_back({x, y, width, height});
    }
    const auto variant_count = reader.word();
    reader.records(variant_count, 2U);
    variants_.reserve(variant_count);
    for (std::size_t i = 0; i < variant_count; ++i) {
        const auto rectangle = reader.byte();
        const auto orientation = reader.byte();
        require(rectangle < rectangles_.size(), "atlas variant references missing rectangle");
        require(orientation <= 7U, "unsupported atlas orientation");
        variants_.push_back({rectangle, orientation});
    }
    const auto frame_count = reader.word();
    reader.records(frame_count, 2U); // Part count and explicit-bounds flag are always present.
    frames_.reserve(frame_count);
    for (std::size_t i = 0; i < frame_count; ++i) {
        AtlasFrame frame;
        const auto count = reader.byte();
        reader.records(count, wide_ ? 5U : 3U);
        frame.parts.reserve(count);
        for (std::size_t part = 0; part < count; ++part) {
            const auto variant = reader.byte();
            require(variant < variants_.size(), "atlas part references missing variant");
            const auto x = reader.coordinate(wide_);
            const auto y = reader.coordinate(wide_);
            frame.parts.push_back({variant, x, y});
        }
        if (reader.flag()) {
            const auto x = reader.coordinate(wide_);
            const auto y = reader.coordinate(wide_);
            const auto width = wide_ ? reader.coordinate(true) : std::int16_t(reader.byte());
            const auto height = wide_ ? reader.coordinate(true) : std::int16_t(reader.byte());
            frame.explicit_bounds = std::array<std::int16_t, 4>{x, y, width, height};
        }
        frames_.push_back(std::move(frame));
    }
    const auto point_count = reader.byte();
    reader.records(point_count, wide_ ? 4U : 2U);
    points_.reserve(point_count);
    for (std::size_t i = 0; i < point_count; ++i) {
        const auto x = reader.coordinate(wide_);
        const auto y = reader.coordinate(wide_);
        points_.push_back({x, y});
    }
    const auto animation_count = reader.byte();
    reader.records(animation_count, 1U);
    animations_.reserve(animation_count);
    for (std::size_t i = 0; i < animation_count; ++i) {
        const auto count = reader.byte();
        reader.records(count, 3U);
        std::vector<AtlasStep> animation;
        animation.reserve(count);
        for (std::size_t step = 0; step < count; ++step) {
            const auto frame = reader.byte();
            const auto duration = reader.word();
            require(frame < frames_.size(), "atlas animation references missing frame");
            animation.push_back({frame, duration});
        }
        animations_.push_back(std::move(animation));
    }
    require(reader.position() == bytes.size(), "atlas descriptor has trailing bytes");
}

std::vector<AtlasQuad> AtlasDescriptor::quads_for_frame(std::size_t frame_index) const {
    require(frame_index < frames_.size(), "atlas frame index outside descriptor");
    const auto& parts = frames_[frame_index].parts;
    std::vector<AtlasQuad> result;
    result.reserve(parts.size());
    for (std::size_t reverse = parts.size(); reverse != 0; --reverse) {
        const auto source = reverse - 1U;
        const auto& part = parts[source];
        const auto& variant = variants_[part.variant_index];
        const auto& rectangle = rectangles_[variant.rectangle_index];
        const std::int32_t x = part.offset_x, y = -std::int32_t(part.offset_y);
        const std::int32_t width = rectangle.width, height = rectangle.height;
        AtlasQuad quad{};
        quad.source_part_index = source;
        const auto vertex = [](std::int32_t px, std::int32_t py) {
            return std::array<std::int16_t, 3>{short_bits(px), short_bits(py), 0};
        };
        if (variant.orientation >= 4U) {
            quad.positions = {vertex(x, y - width), vertex(x + height, y - width),
                              vertex(x, y), vertex(x + height, y)};
        } else {
            quad.positions = {vertex(x, y), vertex(x, y - height),
                              vertex(x + width, y), vertex(x + width, y - height)};
        }
        // Original v2 loader adds a pixel to each origin, except when adding
        // one to Y would make Y+height exceed its fixed 512-pixel atlas.
        const auto adjusted_x = short_bits(std::int32_t(rectangle.x) + 1);
        const std::int32_t candidate_y = std::int32_t(rectangle.y) + 1;
        const auto adjusted_y = short_bits(candidate_y + height > 512 ? rectangle.y : candidate_y);
        std::int32_t u = std::int32_t(adjusted_x) * 128 - 32767;
        std::int32_t v = std::int32_t(adjusted_y) * 128 - 32767;
        std::int32_t du = width * 128 - 4, dv = height * 128 - 4;
        if (variant.orientation == 2U) { u += du; du = -du; }
        else if (variant.orientation == 1U) { v += dv; dv = -dv; }
        const auto uv = [](std::int32_t pu, std::int32_t pv) {
            return std::array<std::int16_t, 2>{short_bits(pu), short_bits(pv)};
        };
        quad.quantized_uv = {uv(u, v), uv(u, v + dv), uv(u + du, v), uv(u + du, v + dv)};
        result.push_back(quad);
    }
    return result;
}

} // namespace g1
