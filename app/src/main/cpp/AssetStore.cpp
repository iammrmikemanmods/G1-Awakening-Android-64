#include "AssetStore.hpp"
#include "g1/TextureEnhancement.hpp"
#include "g1/TileUpscale.hpp"
#include <array>
#include <filesystem>
#include <cstring>
#include <stdexcept>

namespace g1::android {
namespace {
constexpr std::array<std::uint32_t, 64> K{
    {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
     0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
     0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
     0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
     0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
     0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
     0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
     0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
     0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
     0xc67178f2}};
std::uint32_t rotr(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}
void block(std::array<std::uint32_t, 8> &h, const std::uint8_t *p) {
    std::array<std::uint32_t, 64> w{};
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) |
               (std::uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
    for (unsigned i = 16; i < 64; ++i) {
        auto x = w[i - 15], y = w[i - 2];
        w[i] = w[i - 16] + (rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3)) + w[i - 7] +
               (rotr(y, 17) ^ rotr(y, 19) ^ (y >> 10));
    }
    auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], v = h[7];
    for (unsigned i = 0; i < 64; ++i) {
        auto t =
            v + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ ((~e) & g)) + K[i] + w[i];
        auto u = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        v = g;
        g = f;
        f = e;
        e = d + t;
        d = c;
        c = b;
        b = a;
        a = t + u;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += v;
}
} // namespace
std::string sha256(const std::uint8_t *data, std::size_t size) {
    std::array<std::uint32_t, 8> h{{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f,
                                    0x9b05688c, 0x1f83d9ab, 0x5be0cd19}};
    std::size_t at = 0;
    for (; size - at >= 64; at += 64)
        block(h, data + at);
    std::array<std::uint8_t, 128> tail{};
    const auto rem = size - at;
    if (rem)
        std::memcpy(tail.data(), data + at, rem);
    tail[rem] = 0x80;
    const std::size_t n = rem < 56 ? 64 : 128;
    const std::uint64_t bits = std::uint64_t(size) * 8;
    for (unsigned i = 0; i < 8; ++i)
        tail[n - 1 - i] = static_cast<std::uint8_t>(bits >> (i * 8));
    block(h, tail.data());
    if (n == 128)
        block(h, tail.data() + 64);
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (auto v : h)
        for (int s = 28; s >= 0; s -= 4)
            out += hex[(v >> s) & 15];
    return out;
}
void verifyAsset(const Bytes &data, const char *expected) {
    if (sha256(data) != expected)
        throw FormatError("Bundled original game data failed SHA-256 verification");
}
std::optional<RgbaImage> cleanTexture(const RgbaImage &image, const char *category, unsigned factor,
                                      const std::string &root) {
    if (!image.width || !image.height || image.width > 1024 || image.height > 1024 ||
        image.pixels.size() != std::size_t(image.width) * image.height * 4 ||
        (factor != 2 && factor != 4))
        return {};
    const std::string kind(category);
    if (kind != "ui" && kind != "material" && kind != "terrain" && kind != "sprite")
        throw FormatError("Unknown clean texture category");
    const auto name = kind + "_" + std::to_string(image.width) + "x" +
                      std::to_string(image.height) + "_" + sha256(image.pixels) + ".png";
    const auto path = std::filesystem::path(root) / "CleanTextures017" / name;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 33 || size > 16U * 1024U * 1024U)
        return {};
    try {
        auto result = decode_png(read_file(path.string(), 16U * 1024U * 1024U));
        if (result.width != image.width * factor || result.height != image.height * factor)
            return {};
        return result;
    } catch (const std::exception &) {
        return {};
    }
}
RgbaImage cleanSprite(const RgbaImage &image, const std::string &root) {
    auto v = cleanTexture(image, "sprite", 4, root);
    return v ? std::move(*v) : scale4x_rgba(image);
}
RgbaImage cleanTerrain(const RgbaImage &image, const std::string &root) {
    auto v = cleanTexture(image, "terrain", 4, root);
    return v ? std::move(*v) : scale4x_tiles(image, 64, 64);
}
} // namespace g1::android
