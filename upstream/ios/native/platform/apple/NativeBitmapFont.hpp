#pragma once

#import <UIKit/UIKit.h>
#include "g1/BitmapFont.hpp"
#include "NativeAdaptiveUI.hpp"
#include <algorithm>
#include <cmath>
#include <memory>

// Native UIKit adapter for the recovered bitmap-font crop/advance rules.
// Source glyphs remain in their original palette. UIKit uses the actual
// display scale; this does not render or enlarge a legacy framebuffer.
@interface G1BitmapFontFace : NSObject
- (instancetype)initWithArchive:(const g1::Archive&)archive slot:(std::size_t)slot;
- (BOOL)supportsText:(NSString*)text;
- (CGSize)sizeForText:(NSString*)text height:(CGFloat)height;
- (void)drawText:(NSString*)text rect:(CGRect)rect height:(CGFloat)height
      alignment:(NSTextAlignment)alignment;
- (NSInteger)fontSlot;
@end

@implementation G1BitmapFontFace {
    std::unique_ptr<g1::BitmapFontAssets> _assets;
    NSMutableDictionary<NSNumber*,UIImage*>* _glyphImages;
}
- (instancetype)initWithArchive:(const g1::Archive&)archive slot:(std::size_t)slot {
    self=[super init];
    if(self) {
        _assets=std::make_unique<g1::BitmapFontAssets>(g1::load_bitmap_font_assets(archive,slot));
        _glyphImages=[NSMutableDictionary new];
        for(const auto& glyph:_assets->descriptor.glyphs()) {
            if(_assets->descriptor.glyph(glyph.character)!=&glyph)continue;
            if(!glyph.width || !glyph.height)continue;
            // Upscale each glyph independently so adjacent letters can never
            // leak into each other. Logical advance and crop metrics stay original.
            const auto& sheet=_assets->image;
            if(glyph.source_x+glyph.width>sheet.width || glyph.source_y+glyph.height>sheet.height)
                throw g1::FormatError("Source glyph exceeds font sheet");
            g1::RgbaImage crop;crop.width=glyph.width;crop.height=glyph.height;
            crop.pixels.resize(std::size_t(crop.width)*crop.height*4);
            for(std::uint32_t row=0;row<crop.height;++row)
                std::copy_n(sheet.pixels.begin()+(std::size_t(glyph.source_y+row)*sheet.width+glyph.source_x)*4,
                    crop.width*4,crop.pixels.begin()+std::size_t(row)*crop.width*4);
            _glyphImages[@(static_cast<std::uint32_t>(glyph.character))]=g1::adaptive_ui::image(crop,false);
        }
    }
    return self;
}
- (BOOL)supportsText:(NSString*)text {
    if(!text || text.length==0)return NO;
    const char* utf8=text.UTF8String;
    if(!utf8)return NO;
    try {
        const auto characters=g1::bitmap_font_utf8(utf8);
        // This adapter deliberately owns single-line headings only. The
        // existing dialogue paginator still owns paragraph/line semantics.
        return std::all_of(characters.begin(),characters.end(),[&](char32_t c) {
            return c!=U'\n' && c!=U'\r' && _assets->descriptor.contains(c);
        });
    } catch(const g1::FormatError&) { return NO; }
}
- (CGSize)sizeForText:(NSString*)text height:(CGFloat)height {
    const auto characters=g1::bitmap_font_utf8(text.UTF8String);
    const auto metrics=_assets->descriptor.metrics();
    const auto scale=height/static_cast<CGFloat>(metrics.height);
    const auto measured=_assets->descriptor.measure(characters);
    return CGSizeMake(std::ceil(std::max(0,measured.width)*scale),
                      std::ceil(metrics.line_height*scale));
}
- (void)drawText:(NSString*)text rect:(CGRect)rect height:(CGFloat)height
      alignment:(NSTextAlignment)alignment {
    if(rect.size.width<=0 || rect.size.height<=0 || height<=0)return;
    CGContextRef context=UIGraphicsGetCurrentContext();
    if(!context)return;
    const auto characters=g1::bitmap_font_utf8(text.UTF8String);
    const auto scale=height/static_cast<CGFloat>(_assets->descriptor.metrics().height);
    const CGSize size=[self sizeForText:text height:height];
    CGFloat x=rect.origin.x;
    if(alignment==NSTextAlignmentCenter)x+=std::max(CGFloat(0),(rect.size.width-size.width)/2);
    else if(alignment==NSTextAlignmentRight)x+=std::max(CGFloat(0),rect.size.width-size.width);
    const CGFloat y=rect.origin.y+std::max(CGFloat(0),(rect.size.height-size.height)/2);
    CGContextSaveGState(context);
    CGContextClipToRect(context,rect);
    CGContextSetInterpolationQuality(context,kCGInterpolationHigh);
    CGContextSetShouldAntialias(context,true);
    for(const auto& draw:_assets->descriptor.layout(characters)) {
        if(draw.width<=0 || draw.height<=0)continue;
        UIImage* glyph=_glyphImages[@(static_cast<std::uint32_t>(characters.at(draw.character_index)))];
        [glyph drawInRect:CGRectMake(x+draw.x*scale,y+draw.y*scale,draw.width*scale,draw.height*scale)];
    }
    CGContextRestoreGState(context);
}
- (NSInteger)fontSlot { return static_cast<NSInteger>(_assets->font_slot); }
@end

@interface G1WorldTitleLabel : UILabel
- (void)loadOriginalFonts:(const g1::Archive&)archive;
- (BOOL)currentTextDrawn;
- (NSInteger)originalFontSlot;
@end

@implementation G1WorldTitleLabel {
    NSUInteger _textRevision,_drawnTextRevision;
    G1BitmapFontFace* _largeOriginalFont;
    G1BitmapFontFace* _smallOriginalFont;
    CGSize _lastDrawSize;
}
- (void)loadOriginalFonts:(const g1::Archive&)archive {
    // Native role mapping: original slot0 for covered headings; slot2 also
    // covers the percentage sign in support banners. Unsupported characters
    // retain readable existing UIKit text, without forced case or omission.
    _largeOriginalFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:0];
    _smallOriginalFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:2];
    ++_textRevision;_drawnTextRevision=0;
    [self invalidateIntrinsicContentSize];[self setNeedsDisplay];
}
- (G1BitmapFontFace*)originalFace {
    if([_largeOriginalFont supportsText:self.text])return _largeOriginalFont;
    if([_smallOriginalFont supportsText:self.text])return _smallOriginalFont;
    return nil;
}
- (void)setText:(NSString*)text {
    if([self.text isEqualToString:text])return;
    [super setText:text];++_textRevision;_drawnTextRevision=0;
    [self invalidateIntrinsicContentSize];[self setNeedsDisplay];
}
- (CGSize)intrinsicContentSize {
    G1BitmapFontFace* face=[self originalFace];
    return face ? [face sizeForText:self.text height:self.font.pointSize] : [super intrinsicContentSize];
}
- (void)layoutSubviews {
    [super layoutSubviews];
    if(!CGSizeEqualToSize(_lastDrawSize,self.bounds.size)) {
        _lastDrawSize=self.bounds.size;++_textRevision;_drawnTextRevision=0;
        [self setNeedsDisplay];
    }
}
- (void)drawTextInRect:(CGRect)rect {
    G1BitmapFontFace* face=[self originalFace];
    if(face)[face drawText:self.text rect:rect height:self.font.pointSize alignment:self.textAlignment];
    else [super drawTextInRect:rect];
    _drawnTextRevision=_textRevision;
}
- (BOOL)currentTextDrawn { return _textRevision>0 && _textRevision==_drawnTextRevision; }
- (NSInteger)originalFontSlot { G1BitmapFontFace* face=[self originalFace];return face ? face.fontSlot : -1; }
@end
