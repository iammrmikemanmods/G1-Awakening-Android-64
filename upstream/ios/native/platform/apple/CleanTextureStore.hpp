#pragma once
#import <Foundation/Foundation.h>
#import <CommonCrypto/CommonDigest.h>
#include "g1/Image.hpp"
#include "g1/TileUpscale.hpp"
#include <optional>
#include <string>

namespace g1 {
// Source-pixel keyed, offline curve-reconstructed textures. No runtime SVG
// parsing, neural generation, original-data edits or cache-path overrides.
// A missing/bad optional pack falls back to the proven source texture path.
inline std::optional<RgbaImage> clean_texture(const RgbaImage& original,const char* category,unsigned factor) {
    if(!original.width||!original.height||original.width>1024||original.height>1024||
       original.pixels.size()!=std::size_t(original.width)*original.height*4||
       (factor!=2&&factor!=4))return std::nullopt;
    if(std::string(category)!="ui"&&std::string(category)!="material"&&std::string(category)!="terrain"&&std::string(category)!="sprite")
        throw FormatError("Unknown clean texture category");
    unsigned char sum[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(original.pixels.data(),static_cast<CC_LONG>(original.pixels.size()),sum);
    constexpr char hex[]="0123456789abcdef";std::string digest;digest.reserve(64);
    for(auto c:sum){digest+=hex[c>>4];digest+=hex[c&15];}
    const std::string name=std::string(category)+"_"+std::to_string(original.width)+"x"+
        std::to_string(original.height)+"_"+digest+".png";
    NSString* relative=[@"CleanTextures017" stringByAppendingPathComponent:[NSString stringWithUTF8String:name.c_str()]];
    NSString* path=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:relative];
    NSError* error=nil;const auto attr=[NSFileManager.defaultManager attributesOfItemAtPath:path error:&error];
    if(!attr||error||[attr fileSize]>16U*1024U*1024U||[attr fileSize]<33)return std::nullopt;
    NSData* data=[NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:&error];
    if(!data||error)return std::nullopt;
    try {
        const auto* begin=static_cast<const std::uint8_t*>(data.bytes);
        auto result=decode_png(Bytes(begin,begin+data.length));
        if(result.width!=original.width*factor||result.height!=original.height*factor)return std::nullopt;
        return result;
    }catch(const std::exception&){return std::nullopt;}
}
inline RgbaImage clean_sprite_or_source(const RgbaImage& source) {
    auto cleaned=clean_texture(source,"sprite",4);
    return cleaned?std::move(*cleaned):scale4x_rgba(source);
}
inline RgbaImage clean_terrain_or_source(const RgbaImage& source) {
    auto cleaned=clean_texture(source,"terrain",4);
    return cleaned?std::move(*cleaned):scale4x_tiles(source,64,64);
}
} // namespace g1
