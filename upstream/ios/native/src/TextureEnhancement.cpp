#include "g1/TextureEnhancement.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace g1 {
namespace {
std::uint32_t pixel(const RgbaImage& image, std::uint32_t x, std::uint32_t y) {
    const std::size_t offset=(std::size_t(y)*image.width+x)*4U;
    std::uint32_t color;
    std::memcpy(&color,image.pixels.data()+offset,4U);
    return color;
}
void put(RgbaImage& image,std::uint32_t x,std::uint32_t y,std::uint32_t color) {
    const std::size_t offset=(std::size_t(y)*image.width+x)*4U;
    std::memcpy(image.pixels.data()+offset,&color,4U);
}
RgbaImage scale2x(const RgbaImage& image) {
    RgbaImage out{image.width*2U,image.height*2U,
                  Bytes(std::size_t(image.width)*image.height*16U)};
    for(std::uint32_t y=0;y<image.height;++y)for(std::uint32_t x=0;x<image.width;++x) {
        const auto e=pixel(image,x,y);
        const auto b=y ? pixel(image,x,y-1U):e;
        const auto d=x ? pixel(image,x-1U,y):e;
        const auto f=x+1U<image.width ? pixel(image,x+1U,y):e;
        const auto h=y+1U<image.height ? pixel(image,x,y+1U):e;
        const bool edge=b!=h && d!=f;
        put(out,x*2U,y*2U,edge&&d==b?d:e);
        put(out,x*2U+1U,y*2U,edge&&b==f?f:e);
        put(out,x*2U,y*2U+1U,edge&&d==h?d:e);
        put(out,x*2U+1U,y*2U+1U,edge&&h==f?f:e);
    }
    return out;
}
} // namespace

RgbaImage scale4x_rgba(const RgbaImage& image) {
    if(image.width==0 || image.height==0 || image.width>1024 ||
       image.height>1024 || std::uint64_t(image.width)*image.height>1'048'576ULL ||
       image.pixels.size()!=std::size_t(image.width)*image.height*4U)
        throw FormatError("unsupported Scale4x original source texture");
    return scale2x(scale2x(image));
}

} // namespace g1
