#pragma once
#include "g1/Image.hpp"
#include <array>
#include <algorithm>
#include <cmath>
namespace g1 {
enum class MaterialEnhancement {original, character, environment, effect};
// Modest 2x reconstruction of the source, not generated high-frequency noise.
// Interpolate premultiplied colour to avoid dark/bright transparency fringes;
// alpha is never sharpened. Edge detail is capped at 3/255 for environments,
// 1/255 for faces/characters, and zero for additive/transparent effect art.
inline RgbaImage enhance_material(const RgbaImage& image,MaterialEnhancement kind) {
    if(!image.width||!image.height||image.width>1024||image.height>1024||
       image.pixels.size()!=std::size_t(image.width)*image.height*4)
        throw FormatError("Invalid bounded original material image");
    if(kind==MaterialEnhancement::original)return image;
    RgbaImage out{image.width*2,image.height*2,Bytes(std::size_t(image.width)*image.height*16)};
    const auto value=[&](int x,int y,int channel)->float {
        x=std::clamp(x,0,int(image.width)-1);y=std::clamp(y,0,int(image.height)-1);
        return image.pixels[(std::size_t(y)*image.width+x)*4+channel];
    };
    const float cap=kind==MaterialEnhancement::environment?3.0F:kind==MaterialEnhancement::character?1.0F:0.0F;
    for(std::uint32_t y=0;y<out.height;++y)for(std::uint32_t x=0;x<out.width;++x) {
        const float sx=(x+0.5F)*0.5F-0.5F,sy=(y+0.5F)*0.5F-0.5F;
        const int ix=int(std::floor(sx)),iy=int(std::floor(sy));const float fx=sx-ix,fy=sy-iy;
        float alpha=0;std::array<float,3> rgb{};
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
            const float weight=(dx?fx:1-fx)*(dy?fy:1-fy),a=value(ix+dx,iy+dy,3);
            alpha+=weight*a;
            for(int c=0;c<3;++c)rgb[c]+=weight*a*value(ix+dx,iy+dy,c);
        }
        const auto at=(std::size_t(y)*out.width+x)*4;
        out.pixels[at+3]=std::uint8_t(std::clamp(std::lround(alpha),0L,255L));
        for(int c=0;c<3;++c) {
            float v=alpha>0?rgb[c]/alpha:0;
            // Small source-only local contrast, disabled on alpha edges and
            // flat gradients (sky) so detail does not turn into grain/halos.
            if(cap>0 && alpha>=254.5F) {
                const int cx=int(x/2),cy=int(y/2);const float center=value(cx,cy,c);
                const float neighbours=(value(cx-1,cy,c)+value(cx+1,cy,c)+value(cx,cy-1,c)+value(cx,cy+1,c))*0.25F;
                const float d=center-neighbours;
                if(std::abs(d)>4 && std::abs(d)<64)v+=std::clamp(d*0.10F,-cap,cap);
            }
            out.pixels[at+c]=std::uint8_t(std::clamp(std::lround(v),0L,255L));
        }
    }
    return out;
}
} // namespace g1
