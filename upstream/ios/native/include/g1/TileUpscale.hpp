#pragma once
#include "g1/TextureEnhancement.hpp"
#include <algorithm>
namespace g1 {
// Isolate every terrain cell BEFORE edge reconstruction. Scaling the whole
// atlas lets neighbouring road/water/building cells contaminate each other.
inline RgbaImage scale4x_tiles(const RgbaImage& image,std::uint32_t tile_width,std::uint32_t tile_height) {
    if(!tile_width||!tile_height||!image.width||!image.height||image.width>1024||image.height>1024||
       image.width%tile_width||image.height%tile_height||
       image.pixels.size()!=std::size_t(image.width)*image.height*4)
        throw FormatError("Invalid bounded tile atlas geometry");
    RgbaImage out;out.width=image.width*4;out.height=image.height*4;
    out.pixels.resize(std::size_t(out.width)*out.height*4);
    for(std::uint32_t y=0;y<image.height;y+=tile_height)for(std::uint32_t x=0;x<image.width;x+=tile_width) {
        RgbaImage tile;tile.width=tile_width;tile.height=tile_height;tile.pixels.resize(std::size_t(tile_width)*tile_height*4);
        for(std::uint32_t row=0;row<tile_height;++row)
            std::copy_n(image.pixels.begin()+(std::size_t(y+row)*image.width+x)*4,std::size_t(tile_width)*4,
                        tile.pixels.begin()+std::size_t(row)*tile_width*4);
        const auto expanded=scale4x_rgba(tile);
        for(std::uint32_t row=0;row<expanded.height;++row)
            std::copy_n(expanded.pixels.begin()+std::size_t(row)*expanded.width*4,std::size_t(expanded.width)*4,
                        out.pixels.begin()+(std::size_t(y*4+row)*out.width+x*4)*4);
    }
    return out;
}
}
