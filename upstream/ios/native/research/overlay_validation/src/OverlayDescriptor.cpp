#include "g1/OverlayDescriptor.hpp"

#include <string>
#include <utility>

namespace g1 {
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw FormatError(message);
}

class Reader {
public:
    explicit Reader(const Bytes& bytes) : bytes_(bytes) {}
    std::size_t remaining() const noexcept { return bytes_.size() - position_; }
    void records(std::size_t count, std::size_t width) const {
        require(count <= remaining() / width, "truncated 2D overlay records");
    }
    std::uint8_t byte() {
        require(remaining() != 0, "truncated 2D overlay field");
        return bytes_[position_++];
    }
    std::uint16_t word() {
        const auto high = byte();
        return std::uint16_t((std::uint16_t(high) << 8U) | byte());
    }
    std::int16_t coordinate() {
        const auto value = byte();
        return std::int16_t(value < 128U ? int(value) : int(value) - 256);
    }

private:
    const Bytes& bytes_;
    std::size_t position_{};
};

} // namespace

OverlayDescriptor::OverlayDescriptor(const Bytes& bytes, std::uint32_t image_width,
                                     std::uint32_t image_height) {
    require(bytes.size() <= Archive::max_resource_size, "2D overlay exceeds resource limit");
    require(image_width > 0 && image_width <= 256 && image_height > 0 && image_height <= 256,
            "unsupported 2D overlay source image size");
    Reader reader(bytes);
    require(reader.byte() == 1, "unsupported 2D overlay version");
    require(reader.byte() == 0, "unsupported 2D overlay coordinate width");
    const auto rectangle_count = reader.word();
    require(rectangle_count > 0 && rectangle_count <= 32767,
            "unsupported 2D overlay rectangle count");
    require(reader.byte() == 0, "unsupported 2D overlay extra images");
    reader.records(rectangle_count, 4);
    rectangles_.reserve(rectangle_count);
    for (std::size_t i = 0; i < rectangle_count; ++i) {
        const auto x = reader.byte(), y = reader.byte();
        const auto width = reader.byte(), height = reader.byte();
        require(width > 0 && height > 0 && std::uint32_t(x) + width <= image_width &&
                    std::uint32_t(y) + height <= image_height,
                "2D overlay crop is outside its source image");
        rectangles_.push_back({x, y, width, height});
    }
    const auto variant_count = reader.word();
    require(variant_count > 0 && variant_count <= 32767,
            "unsupported 2D overlay variant count");
    reader.records(variant_count, 2);
    variants_.reserve(variant_count);
    for (std::size_t i = 0; i < variant_count; ++i) {
        const auto rectangle = reader.byte(), orientation = reader.byte();
        require(rectangle < rectangles_.size(), "2D overlay variant references a missing crop");
        require(orientation == 0, "unsupported 2D overlay variant transform");
        variants_.push_back({rectangle, orientation});
    }
    const auto frame_count = reader.word();
    require(frame_count > 0 && frame_count <= 256, "unsupported 2D overlay frame count");
    reader.records(frame_count, 2);
    frames_.reserve(frame_count);
    for (std::size_t i = 0; i < frame_count; ++i) {
        AtlasFrame frame;
        const auto count = reader.byte();
        reader.records(count, 3);
        frame.parts.reserve(count);
        for (std::size_t part = 0; part < count; ++part) {
            const auto variant = reader.byte();
            const auto x = reader.coordinate(), y = reader.coordinate();
            require(variant < variants_.size(), "2D overlay part references a missing variant");
            frame.parts.push_back({variant, x, y});
        }
        require(reader.byte() == 0, "unsupported 2D overlay explicit frame bounds");
        frames_.push_back(std::move(frame));
    }
    require(reader.byte() == 0, "unsupported 2D overlay extra points");
    const auto animation_count = reader.byte();
    require(animation_count > 0, "unsupported implicit 2D overlay animation");
    reader.records(animation_count, 1);
    animations_.reserve(animation_count);
    for (std::size_t animation_index = 0; animation_index < animation_count; ++animation_index) {
        const auto count = reader.byte();
        require(count > 0, "empty 2D overlay animation");
        reader.records(count, 3);
        std::vector<AtlasStep> animation;
        animation.reserve(count);
        for (std::size_t step = 0; step < count; ++step) {
            const auto frame = reader.byte();
            const auto duration = reader.word();
            require(frame < frames_.size(), "2D overlay animation references a missing frame");
            require(duration > 0 && duration <= 32767, "unsupported 2D overlay duration");
            animation.push_back({frame, duration});
        }
        animations_.push_back(std::move(animation));
    }
    require(reader.remaining() == 0, "2D overlay descriptor has trailing bytes");
}

} // namespace g1
