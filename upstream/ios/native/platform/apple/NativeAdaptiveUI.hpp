#pragma once
#import <UIKit/UIKit.h>
#include "g1/AdaptivePresentation.hpp"
#include "g1/TextureEnhancement.hpp"
#include "g1/Image.hpp"
#include "CleanTextureStore.hpp"

// Presentation-only derived pixels. Original bytes stay in iphone.big;
// both Classic and Modern use this same enhanced UI path.
// Empty overlay space must hit the underlying MTKView, not swallow world
// pan/pinch/select. Buttons themselves keep UIKit's normal hit-testing.
@interface G1PassthroughControls : UIView
@end
@implementation G1PassthroughControls
- (UIView*)hitTest:(CGPoint)point withEvent:(UIEvent*)event {
    UIView* hit=[super hitTest:point withEvent:event];
    return hit==self ? nil : hit;
}
@end

namespace g1::adaptive_ui {
inline CGRect aspect_fit(UIImage* image,CGRect rect) {
    if(!image || image.size.width<=0 || image.size.height<=0)return CGRectZero;
    const CGFloat scale=std::min(rect.size.width/image.size.width,rect.size.height/image.size.height);
    return CGRectMake(rect.origin.x+(rect.size.width-image.size.width*scale)/2,
        rect.origin.y+(rect.size.height-image.size.height*scale)/2,
        image.size.width*scale,image.size.height*scale);
}
inline UIImage* image(const RgbaImage& original,bool use_clean_pack=true) {
    const auto clean=use_clean_pack?clean_texture(original,"ui",4):std::nullopt;
    const auto pixels=clean?*clean:scale4x_rgba(original);
    const auto png=encode_png(pixels);
    UIImage* decoded=[UIImage imageWithData:[NSData dataWithBytes:png.data() length:png.size()]];
    if(!decoded.CGImage)throw FormatError("Enhanced source UI decode failed");
    return [UIImage imageWithCGImage:decoded.CGImage scale:4 orientation:UIImageOrientationUp];
}
inline CGRect cg(adaptive::Rect r){return CGRectMake(r.x,r.y,r.width,r.height);}
inline adaptive::Layout layout(UIView* view) {
    UIEdgeInsets s=view.safeAreaInsets;
    return adaptive::layout(view.bounds.size.width,view.bounds.size.height,{s.top,s.left,s.bottom,s.right});
}
inline UIImage* crop(UIImage* source,CGRect rect) {
    if(!source.CGImage)return nil;
    static NSCache<NSString*,UIImage*>* cache=[NSCache new];
    cache.countLimit=96;
    NSString* key=[NSString stringWithFormat:@"%p:%g,%g,%g,%g",source.CGImage,
        rect.origin.x,rect.origin.y,rect.size.width,rect.size.height];
    UIImage* result=[cache objectForKey:key];if(result)return result;
    const CGFloat s=source.scale;
    CGImageRef cut=CGImageCreateWithImageInRect(source.CGImage,CGRectMake(
        rect.origin.x*s,rect.origin.y*s,rect.size.width*s,rect.size.height*s));
    if(!cut)return nil;
    result=[UIImage imageWithCGImage:cut scale:s orientation:UIImageOrientationUp];
    CGImageRelease(cut);[cache setObject:result forKey:key];return result;
}
inline void tiled(UIImage* image,CGRect dest) {
    if(image&&dest.size.width>0&&dest.size.height>0)[image drawAsPatternInRect:dest];
}
inline void menu(UIImage* source,CGRect bounds,UIEdgeInsets safe) {
    const auto l=adaptive::layout(bounds.size.width,bounds.size.height,
        {safe.top,safe.left,safe.bottom,safe.right});
    if(l.scale<=0||!source)return;
    CGContextRef ctx=UIGraphicsGetCurrentContext();CGContextSaveGState(ctx);
    CGContextSetInterpolationQuality(ctx,kCGInterpolationHigh);
    CGContextScaleCTM(ctx,l.scale,l.scale);
    // Only source star texture fills the surplus. Never duplicate the logo or
    // stretch Transformer portraits to make an old 3:2 bitmap fill a 19.5:9 screen.
    tiled(crop(source,CGRectMake(16,8,64,64)),CGRectMake(0,0,bounds.size.width/l.scale,bounds.size.height/l.scale));
    CGContextTranslateCTM(ctx,l.safe.x/l.scale,l.source.y/l.scale);
    const CGFloat w=l.virtual_width;
    [crop(source,CGRectMake(0,0,480,84)) drawInRect:CGRectMake((w-480)/2,0,480,84)];
    UIImage* left=crop(source,CGRectMake(0,84,132,29));
    UIImage* right=crop(source,CGRectMake(348,84,132,29));
    [[left resizableImageWithCapInsets:UIEdgeInsetsMake(0,20,0,20)] drawInRect:CGRectMake(0,84,(w-216)/2,29)];
    [crop(source,CGRectMake(132,84,216,29)) drawInRect:CGRectMake((w-216)/2,84,216,29)];
    [[right resizableImageWithCapInsets:UIEdgeInsetsMake(0,20,0,20)] drawInRect:CGRectMake((w+216)/2,84,(w-216)/2,29)];
    UIImage* red=crop(source,CGRectMake(0,113,480,103));
    [[red resizableImageWithCapInsets:UIEdgeInsetsMake(12,10,12,10) resizingMode:UIImageResizingModeTile]
        drawInRect:CGRectMake(0,113,w,103)];
    UIImage* rail=crop(source,CGRectMake(0,216,480,34));
    [[rail resizableImageWithCapInsets:UIEdgeInsetsMake(0,90,0,90)] drawInRect:CGRectMake(0,216,w,34)];
    [crop(source,CGRectMake(0,250,80,70)) drawInRect:CGRectMake(0,250,80,70)];
    [crop(source,CGRectMake(400,250,80,70)) drawInRect:CGRectMake(w-80,250,80,70)];
    CGContextRestoreGState(ctx);
}
} // namespace g1::adaptive_ui
