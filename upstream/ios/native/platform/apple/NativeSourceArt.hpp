#pragma once

#import <UIKit/UIKit.h>
#include "g1/Archive.hpp"
#include "g1/Image.hpp"
#include "g1/UIBitmap.hpp"
#include "NativeAdaptiveUI.hpp"

// Original FGIB PNG/BMP resources are decoded from the untouched owner archive.
// The shared presentation layer may apply the optional source-hash-keyed clean
// derivative pack; missing derivatives retain the original source fallback.
namespace g1::source_art {
inline UIImage* indexed_bmp(const Archive& archive,std::uint32_t resource,
                            std::uint32_t expected_width,std::uint32_t expected_height) {
    const auto ids=archive.find(resource,0xb717945fU);
    if(ids.size()!=1)throw FormatError("Source portrait BMP missing or ambiguous");
    const auto bitmap=decode_ui_bmp(archive.read(ids.front()),0x00ff00ffU);
    if(bitmap.width!=expected_width || bitmap.height!=expected_height)
        throw FormatError("Original portrait BMP size mismatch");
    const auto png=encode_png(bitmap);
    UIImage* image=[UIImage imageWithData:[NSData dataWithBytes:png.data() length:png.size()]];
    if(!image.CGImage)throw FormatError("Original indexed BMP did not decode to a UIKit image");
    return adaptive_ui::image(bitmap);
}

inline UIImage* png(const Archive& archive,std::uint32_t resource,
                    std::uint32_t expected_width,std::uint32_t expected_height) {
    const auto ids=archive.find(resource,0xb7178678U);
    if(ids.size()!=1)throw FormatError("Original UI PNG missing or ambiguous");
    const auto bytes=archive.read(ids.front());
    const auto image=decode_png(bytes);
    if(image.width!=expected_width || image.height!=expected_height)
        throw FormatError("Original UI PNG size mismatch");
    UIImage* result=[UIImage imageWithData:[NSData dataWithBytes:bytes.data() length:bytes.size()]];
    if(!result.CGImage)throw FormatError("Original UI PNG did not decode to a UIKit image");
    return adaptive_ui::image(image);
}

struct Portrait {
    std::uint32_t id{};
    std::uint32_t width{},height{};
};

// Validated by comparing the source 1.1.3 indexed pictures to native
// gameplay evidence. These are original image IDs, not fabricated character art.
inline Portrait dialogue_portrait(NSString* speaker) {
    NSString* name=speaker.uppercaseString ?: @"";
    if([name containsString:@"BUMBLEBEE"])return {1295,77,66};
    if([name containsString:@"RATCHET"])return {396,70,66};
    if([name containsString:@"OPTIMUS"])return {878,73,70};
    if([name containsString:@"MEGATRON"])return {1156,76,68};
    return {}; // Never show the wrong Transformer when no match was verified.
}
inline Portrait tactical_portrait(NSString* name) {
    NSString* upper=name.uppercaseString ?: @"";
    if([upper containsString:@"BUMBLEBEE"])return {731,54,50};
    if([upper containsString:@"RATCHET"])return {396,70,66};
    if([upper containsString:@"OPTIMUS"])return {878,73,70};
    if([upper containsString:@"MEGATRON"])return {670,54,51};
    return {};
}
}
