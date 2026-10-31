#pragma once
#include "g1/IndexedBitmap.hpp"
namespace g1 {
// The original Frenzy portrait #646 is 24-bit BI_RGB, unlike the other
// indexed portraits. Keep the strict indexed decoder unchanged and dispatch
// the verified true-colour format explicitly. Output uses the same colour key.
inline RgbaImage decode_ui_bmp(const Bytes& b,std::uint32_t key=0x00ff00ffU) {
    const auto u32=[&](std::size_t o){if(o>b.size()||b.size()-o<4)throw FormatError("Truncated UI BMP");
        return std::uint32_t(b[o])|(std::uint32_t(b[o+1])<<8)|(std::uint32_t(b[o+2])<<16)|(std::uint32_t(b[o+3])<<24);};
    const auto u16=[&](std::size_t o){if(o>b.size()||b.size()-o<2)throw FormatError("Truncated UI BMP");return unsigned(b[o])|(unsigned(b[o+1])<<8);};
    if(b.size()<54||b.size()>Archive::max_resource_size||b[0]!='B'||b[1]!='M')throw FormatError("Invalid UI BMP");
    if(u16(28)==8) {
        // Source Windows exporters padded entire files to four bytes too.
        // Validate and remove at most three ZERO trailer bytes in a copy;
        // never weaken the strict palette decoder or alter iphone.big.
        const auto w=u32(18),raw=u32(22),h=raw&0x80000000U?~raw+1:raw,off=u32(10);
        if(!w||!h||w>8192||h>8192||off<54||off>b.size()||u32(2)!=b.size())throw FormatError("Invalid indexed UI BMP bounds");
        const std::size_t span=((std::size_t(w)+3)&~std::size_t(3))*h;
        if(span>b.size()-off||b.size()-off-span>3||(u32(34)&&u32(34)!=span&&u32(34)!=b.size()-off))
            throw FormatError("Invalid indexed UI BMP row bounds");
        for(std::size_t i=off+span;i<b.size();++i)if(b[i])throw FormatError("Nonzero indexed UI BMP trailer");
        Bytes copy(b.begin(),b.begin()+off+span);
        for(unsigned j=0;j<4;++j){copy[2+j]=std::uint8_t(copy.size()>>(8*j));copy[34+j]=std::uint8_t(span>>(8*j));}
        return decode_indexed_bmp(copy,key);
    }
    if(u32(2)!=b.size()||u32(6)!=0||u32(14)!=40||u16(26)!=1||u16(28)!=24||u32(30)!=0||u32(46)||u32(50))
        throw FormatError("Unsupported true-colour UI BMP header");
    const auto w=u32(18),raw=u32(22),h=raw&0x80000000U?~raw+1:raw;
    if(!w||!h||w>8192||h>8192||std::uint64_t(w)*h>16*1024*1024)throw FormatError("UI BMP dimensions exceed bounds");
    const auto offset=u32(10);const std::size_t stride=(std::size_t(w)*3+3)&~std::size_t(3);
    if(offset<54||offset>b.size()||stride*h>b.size()-offset||b.size()-offset-stride*h>3||(u32(34)&&u32(34)!=b.size()-offset&&u32(34)!=stride*h))
        throw FormatError("UI BMP row bounds mismatch");
    for(std::size_t i=offset+stride*h;i<b.size();++i)if(b[i])throw FormatError("Nonzero UI BMP trailer");
    RgbaImage out{w,h,Bytes(std::size_t(w)*h*4)};
    for(std::size_t y=0;y<h;++y)for(std::size_t x=0;x<w;++x){
        const auto at=offset+(raw&0x80000000U?y:h-1-y)*stride+x*3,dst=(y*w+x)*4;
        const auto color=(unsigned(b[at+2])<<16)|(unsigned(b[at+1])<<8)|b[at];
        out.pixels[dst]=b[at+2];out.pixels[dst+1]=b[at+1];out.pixels[dst+2]=b[at];
        out.pixels[dst+3]=(color==(key&0xffffffU))?0:255;
    }
    return out;
}
} // namespace g1
