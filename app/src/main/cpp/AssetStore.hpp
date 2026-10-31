#pragma once
#include "g1/Image.hpp"
#include <optional>
#include <string>
namespace g1::android {
std::string sha256(const std::uint8_t *data, std::size_t size);
inline std::string sha256(const Bytes &data) {
    return sha256(data.data(), data.size());
}
void verifyAsset(const Bytes &, const char *expected);
std::optional<RgbaImage> cleanTexture(const RgbaImage &, const char *category, unsigned factor,
                                      const std::string &root);
RgbaImage cleanSprite(const RgbaImage &, const std::string &root);
RgbaImage cleanTerrain(const RgbaImage &, const std::string &root);
} // namespace g1::android
