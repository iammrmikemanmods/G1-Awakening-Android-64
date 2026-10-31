#pragma once

// Synthetic, copyright-independent animation data shared only by host tests
// and the private-data comparison tool. This is not an app/core dependency.
#include "g1/M3GAnimation.hpp"
#include <cstring>
#include <zlib.h>

namespace g1_test {
inline void u32(g1::Bytes& b,std::uint32_t v) { for(unsigned i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(v>>(8U*i))); }
inline void scalar(g1::Bytes& b,float v) { std::uint32_t bits;std::memcpy(&bits,&v,4);u32(b,bits); }
inline void append(g1::Bytes& a,const g1::Bytes& b) { a.insert(a.end(),b.begin(),b.end()); }
inline g1::Bytes base(const std::vector<std::uint32_t>& tracks={}) {
    g1::Bytes b;u32(b,0);u32(b,static_cast<std::uint32_t>(tracks.size()));for(auto t:tracks)u32(b,t);u32(b,0);return b;
}
inline g1::Bytes node(const std::vector<std::uint32_t>& tracks={}) {
    auto b=base(tracks);append(b,{0,0,1,1,255});u32(b,0xffffffffU);b.push_back(0);return b;
}
inline g1::Bytes chunk(std::uint8_t type,const g1::Bytes& body) {
    g1::Bytes b{type};u32(b,static_cast<std::uint32_t>(body.size()));append(b,body);return b;
}
inline g1::Bytes section(const g1::Bytes& raw) {
    g1::Bytes b{0};u32(b,static_cast<std::uint32_t>(raw.size()+13));u32(b,static_cast<std::uint32_t>(raw.size()));append(b,raw);
    u32(b,static_cast<std::uint32_t>(adler32(1,b.data(),static_cast<uInt>(b.size()))));return b;
}
inline g1::Bytes file(const g1::Bytes& raw) {
    const auto body=section(raw);const auto length=static_cast<std::uint32_t>(12+30+body.size());
    g1::Bytes header{1,0,0};u32(header,length);u32(header,length);header.push_back(0);
    g1::Bytes bytes{0xab,'J','S','R','1','8','4',0xbb,13,10,26,10};append(bytes,section(chunk(0,header)));append(bytes,body);return bytes;
}
inline g1::Bytes weighted_animation(std::uint32_t property,float first_weight,float second_weight) {
    const bool orientation=property==268;
    const std::vector<std::vector<std::vector<float>>> values=orientation ?
        std::vector<std::vector<std::vector<float>>>{{{0,0,0,1},{0,0,0.70710677F,0.70710677F}},{{0.70710677F,0,0,0.70710677F},{0,1,0,0}}} :
        std::vector<std::vector<std::vector<float>>>{{{0,0,0},{10,20,30}},{{8,-4,1},{18,6,-9}}};
    g1::Bytes raw;
    for(const auto& keys:values) {
        auto sequence=base();append(sequence,{static_cast<std::uint8_t>(orientation ? 177 : 176),192,0});
        u32(sequence,100);u32(sequence,0);u32(sequence,1);u32(sequence,orientation ? 4 : 3);u32(sequence,2);
        for(std::size_t n=0;n<keys.size();++n) { u32(sequence,static_cast<std::uint32_t>(n*100));for(float v:keys[n])scalar(sequence,v); }
        append(raw,chunk(19,sequence)); // indices2,3
    }
    for(const auto weight:{first_weight,second_weight}) {
        auto controller=base();scalar(controller,1);scalar(controller,weight);u32(controller,0);u32(controller,0);scalar(controller,0);u32(controller,0);
        append(raw,chunk(1,controller)); // indices4,5
    }
    for(std::uint32_t n=0;n<2;++n) {
        auto track=base();u32(track,2+n);u32(track,4+n);u32(track,property);append(raw,chunk(2,track)); //6,7
    }
    auto group=node({6,7});u32(group,0);append(raw,chunk(9,group)); //8
    auto world=node();u32(world,1);u32(world,8);u32(world,0);u32(world,0);append(raw,chunk(22,world));
    return file(raw);
}
inline g1::Bytes deep_hierarchy(std::uint32_t count) {
    g1::Bytes raw;
    for(std::uint32_t i=0;i<count;++i) {
        auto group=node();u32(group,i ? 1 : 0);if(i)u32(group,i+1);append(raw,chunk(9,group));
    }
    auto world=node();u32(world,1);u32(world,count+1);u32(world,0);u32(world,0);append(raw,chunk(22,world));return file(raw);
}
} // namespace g1_test
