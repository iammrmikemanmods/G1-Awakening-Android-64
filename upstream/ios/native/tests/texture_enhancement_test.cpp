#include "g1/TextureEnhancement.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
std::uint32_t rgba(const g1::RgbaImage& image,std::size_t x,std::size_t y) {
    const auto i=(y*image.width+x)*4U;
    return (std::uint32_t(image.pixels[i])<<24U)|(std::uint32_t(image.pixels[i+1])<<16U)|
           (std::uint32_t(image.pixels[i+2])<<8U)|image.pixels[i+3];
}
void invalid(const g1::RgbaImage& image) {
    bool rejected=false;try{(void)g1::scale4x_rgba(image);}
    catch(const g1::FormatError&){rejected=true;}
    check(rejected,"malformed input was accepted");
}
} // namespace

int main() {
    try {
        g1::RgbaImage one{1,1,{17,29,53,97}};
        auto expanded=g1::scale4x_rgba(one);
        check(expanded.width==4&&expanded.height==4&&expanded.pixels.size()==64,
              "1x1 output geometry mismatch");
        for(std::size_t y=0;y<4;++y)for(std::size_t x=0;x<4;++x)
            check(rgba(expanded,x,y)==0x111d3561U,"uniform source/alpha changed");
        g1::RgbaImage input{3,3,{
            255,0,0,255,   0,0,0,0,    0,0,255,255,
            255,0,0,255,   255,0,0,255, 0,0,255,255,
            0,255,0,128,   0,255,0,128,0,0,255,255}};
        auto modern=g1::scale4x_rgba(input);
        check(modern.width==12&&modern.height==12&&modern.pixels.size()==576,
              "3x3 dimensions or allocation mismatch");
        std::set<std::uint32_t> source,output;
        for(std::size_t y=0;y<input.height;++y)for(std::size_t x=0;x<input.width;++x)
            source.insert(rgba(input,x,y));
        for(std::size_t y=0;y<modern.height;++y)for(std::size_t x=0;x<modern.width;++x)
            output.insert(rgba(modern,x,y));
        check(std::all_of(output.begin(),output.end(),
             [&](auto color){return source.count(color)>0;}),"invented RGBA sample");
        check(source==output,"source palette colour unexpectedly vanished");
        check(g1::scale4x_rgba(input).pixels==modern.pixels,"nondeterministic Scale4x");
        invalid({0,1,{}});
        invalid({1,0,{}});
        invalid({1025,1,g1::Bytes(1025*4U)});
        invalid({1,1,{1,2,3}});
        invalid({1,1,{1,2,3,4,5}});
        invalid({1024,1024,{}});
        std::cout<<"Scale4x: bounds, strict RGBA, alpha, palette, dimensions and determinism passed\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<"\n";return 1;
    }
}
