#pragma once

#import <UIKit/UIKit.h>
#include "NativeBitmapFont.hpp"
#include "g1/OriginalUI.hpp"
#include "NativeAdaptiveUI.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Original 32-bit G1 menu affordance retained as native iOS touch control.
// The source colour/advance bitmap font is used when complete coverage fits;
// otherwise the readable UIKit title remains. No rebuilt or generated art.
@interface G1OriginalMenuButton : UIButton
- (void)useOriginalFace:(G1BitmapFontFace*)face fallback:(G1BitmapFontFace*)fallback;
- (void)setSourceScale:(CGFloat)scale;
@end

@implementation G1OriginalMenuButton {
    G1BitmapFontFace* _originalFace;
    G1BitmapFontFace* _fallbackFace;
    CGFloat _sourceScale;
}
- (void)useOriginalFace:(G1BitmapFontFace*)face fallback:(G1BitmapFontFace*)fallback {
    _originalFace=face;_fallbackFace=fallback;
    [self setNeedsLayout];[self setNeedsDisplay];
}
- (void)setSourceScale:(CGFloat)scale {
    if(_sourceScale==scale)return;_sourceScale=scale;
    self.titleLabel.font=[UIFont boldSystemFontOfSize:13*scale];
    [self setNeedsLayout];[self setNeedsDisplay];
}
- (CGFloat)glyphHeight { return 15*(_sourceScale>0?_sourceScale:1); }
- (G1BitmapFontFace*)visibleOriginalFace {
    NSString* title=[self titleForState:UIControlStateNormal];
    if(_originalFace && [_originalFace supportsText:title]) {
        const CGSize size=[_originalFace sizeForText:title height:[self glyphHeight]];
        if(size.width<=self.bounds.size.width-8 && size.height<=self.bounds.size.height)
            return _originalFace;
    }
    if(_fallbackFace && [_fallbackFace supportsText:title]) {
        const CGSize size=[_fallbackFace sizeForText:title height:[self glyphHeight]];
        if(size.width<=self.bounds.size.width-8 && size.height<=self.bounds.size.height)
            return _fallbackFace;
    }
    return nil;
}
- (void)layoutSubviews {
    [super layoutSubviews];
    self.titleLabel.hidden=[self visibleOriginalFace]!=nil;
}
- (void)setEnabled:(BOOL)enabled {
    [super setEnabled:enabled];[self setNeedsDisplay];
}
- (void)setHighlighted:(BOOL)highlighted {
    [super setHighlighted:highlighted];[self setNeedsDisplay];
}
- (void)drawRect:(CGRect)rect {
    [super drawRect:rect];
    if(self.highlighted || self.isFocused) {
        [[UIColor colorWithRed:0.89 green:0.92 blue:0.95 alpha:1] setStroke];
        UIBezierPath* outline=[UIBezierPath bezierPathWithRect:CGRectInset(self.bounds,1,1)];
        outline.lineWidth=1.5;[outline stroke];
    }
    G1BitmapFontFace* face=[self visibleOriginalFace];
    if(face) {
        CGContextRef context=UIGraphicsGetCurrentContext();
        CGContextSaveGState(context);
        CGContextSetAlpha(context,self.enabled ? 1 : 0.38);
        [face drawText:[self titleForState:UIControlStateNormal] rect:CGRectInset(rect,4,1)
            height:[self glyphHeight] alignment:NSTextAlignmentCenter];
        CGContextRestoreGState(context);
    }
}
@end

// Native campaign/menu/results surface. Game menus use source artwork 1002,
// with original bitmap headings and native readable body text. Geometry
// adapts to wide screens without stretching the 480x320 source. Results and
// script-critical panels retain their independent draw-token contract.
@interface G1CampaignCanvas : UIView
- (void)showTitle:(NSString*)title body:(NSString*)body token:(std::uint64_t)token;
- (void)loadOriginalFonts:(const g1::Archive&)archive;
- (void)loadOriginalMenuArtwork:(const g1::Archive&)archive;
- (void)setOriginalMenuArtworkVisible:(BOOL)visible;
- (void)setDisplaysMenuDetails:(BOOL)visible;
- (NSInteger)originalHeadingFontSlot;
- (G1BitmapFontFace*)originalFaceForMenuText:(NSString*)text;
- (G1BitmapFontFace*)originalSmallFaceForMenuText:(NSString*)text;
- (void)resetPresentation;
- (std::uint64_t)drawnToken;
- (CGFloat)heightForWidth:(CGFloat)width;
- (void)setBackgroundOffset:(std::int32_t)offset;
@end

@implementation G1CampaignCanvas {
    NSString* _heading;
    NSString* _body;
    std::uint64_t _token, _drawnToken;
    std::int32_t _backgroundOffset;
    G1BitmapFontFace* _largeOriginalFont;
    G1BitmapFontFace* _smallOriginalFont;
    G1BitmapFontFace* _menuRedOriginalFont;
    UIImage* _originalMenuArtwork;
    BOOL _menuArtworkVisible;
    BOOL _menuDetailsVisible;
}
- (instancetype)initWithFrame:(CGRect)frame {
    self=[super initWithFrame:frame];
    if(self) { self.opaque=YES;self.contentMode=UIViewContentModeRedraw; self.isAccessibilityElement=YES; self.clipsToBounds=YES; }
    return self;
}
- (void)loadOriginalFonts:(const g1::Archive&)archive {
    // Original keyed sheets are kept unmodified. Draw only complete, fitted
    // single-line titles; leave uncovered or long text readable in UIKit.
    _largeOriginalFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:0];
    _smallOriginalFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:2];
    _menuRedOriginalFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:6];
    _drawnToken=0;
    [self setNeedsDisplay];
}
- (void)loadOriginalMenuArtwork:(const g1::Archive&)archive {
    // The actual 480x320 G1 background includes the Transformers title and
    // original faction portraits, but not a Glu splash. No new artwork is
    // generated, stretched or embedded into the source repository.
    const auto resources=archive.find(1002,0xb7178678U);
    if(resources.size()!=1)throw g1::FormatError("Original G1 menu art identity is ambiguous");
    const auto bytes=archive.read(resources.front());
    const auto image=g1::decode_png(bytes);
    if(image.width!=480 || image.height!=320)
        throw g1::FormatError("Original G1 menu artwork dimensions changed");
    _originalMenuArtwork=g1::adaptive_ui::image(image);
    if(!_originalMenuArtwork.CGImage)
        throw g1::FormatError("Native original G1 menu art could not be decoded");
    _drawnToken=0;
    [self setNeedsDisplay];
}
- (void)setOriginalMenuArtworkVisible:(BOOL)visible {
    if(_menuArtworkVisible==visible)return;
    _menuArtworkVisible=visible;
    _drawnToken=0;
    [self setNeedsDisplay];
}
- (void)setDisplaysMenuDetails:(BOOL)visible {
    if(_menuDetailsVisible==visible)return;
    _menuDetailsVisible=visible;
    _drawnToken=0;[self setNeedsDisplay];
}
- (G1BitmapFontFace*)originalHeadingFace {
    if(!_heading || self.bounds.size.width<=32)return nil;
    const CGFloat width=self.bounds.size.width-32;
    const auto fits=[&](G1BitmapFontFace* font) -> BOOL {
        if(!font || ![font supportsText:_heading])return NO;
        const CGSize size=[font sizeForText:_heading height:22];
        return size.width<=width && size.height<=34;
    };
    if(fits(_largeOriginalFont))return _largeOriginalFont;
    if(fits(_smallOriginalFont))return _smallOriginalFont;
    return nil;
}
- (NSInteger)originalHeadingFontSlot {
    G1BitmapFontFace* face=[self originalHeadingFace];
    return face ? [face fontSlot] : -1;
}
- (G1BitmapFontFace*)originalFaceForMenuText:(NSString*)text {
    if([_menuRedOriginalFont supportsText:text])return _menuRedOriginalFont;
    if([_largeOriginalFont supportsText:text])return _largeOriginalFont;
    if([_smallOriginalFont supportsText:text])return _smallOriginalFont;
    return nil;
}
- (G1BitmapFontFace*)originalSmallFaceForMenuText:(NSString*)text {
    return [_smallOriginalFont supportsText:text] ? _smallOriginalFont : nil;
}
- (NSDictionary*)bodyAttributes {
    NSMutableParagraphStyle* paragraph=[NSMutableParagraphStyle new];
    paragraph.lineSpacing=4;
    paragraph.lineBreakMode=NSLineBreakByWordWrapping;
    return @{NSFontAttributeName:[UIFont systemFontOfSize:16 weight:UIFontWeightMedium],
        NSForegroundColorAttributeName:UIColor.whiteColor,NSParagraphStyleAttributeName:paragraph};
}
- (void)showTitle:(NSString*)title body:(NSString*)body token:(std::uint64_t)token {
    if(_token==token && [_heading isEqualToString:title] && [_body isEqualToString:body])return;
    _heading=[title copy];_body=[body copy];_token=token;_drawnToken=0;_backgroundOffset=0;
    self.accessibilityLabel=[NSString stringWithFormat:@"%@. %@",title,body];
    [self setNeedsDisplay];
}
- (void)resetPresentation {
    _heading=nil;_body=nil;_token=0;_drawnToken=0;_backgroundOffset=0;
    self.accessibilityLabel=@"";[self setNeedsDisplay];
}
- (CGFloat)heightForWidth:(CGFloat)width {
    if(width<=32)return 0;
    const CGRect body=[_body ?: @"" boundingRectWithSize:CGSizeMake(width-32,CGFLOAT_MAX)
        options:NSStringDrawingUsesLineFragmentOrigin|NSStringDrawingUsesFontLeading
        attributes:[self bodyAttributes] context:nil];
    return _menuArtworkVisible ? std::max(CGFloat(240),CGFloat(148)+std::ceil(body.size.height)) :
         std::max(CGFloat(150),CGFloat(90)+std::ceil(body.size.height));
}
- (std::uint64_t)drawnToken { return _drawnToken; }
- (void)setBackgroundOffset:(std::int32_t)offset {
    if(_backgroundOffset==offset)return;
    _backgroundOffset=offset;[self setNeedsDisplay];
}
- (void)drawRect:(CGRect)rect {
    (void)rect;
    if(self.bounds.size.width<=32 || self.bounds.size.height<=0 || !_heading || !_body)return;
    [[UIColor colorWithRed:0.025 green:0.045 blue:0.075 alpha:1] setFill];
    UIRectFill(self.bounds);
    if(_menuArtworkVisible && _originalMenuArtwork) {
        const auto screen=g1::adaptive_ui::layout(self);
        g1::adaptive_ui::menu(_originalMenuArtwork,self.bounds,self.safeAreaInsets);
        const CGRect dest=g1::adaptive_ui::cg(screen.source);
        const auto titleSlot=screen.place(g1::original_ui::heading_slot);
        const CGRect headingRect=g1::adaptive_ui::cg(titleSlot);
        G1BitmapFontFace* face=[_largeOriginalFont supportsText:_heading] ? _largeOriginalFont : [self originalHeadingFace];
        if(face) {
            [face drawText:_heading rect:headingRect height:17*screen.scale
                alignment:NSTextAlignmentCenter];
        } else {
            NSMutableParagraphStyle* centered=[NSMutableParagraphStyle new];
            centered.alignment=NSTextAlignmentCenter;
            [_heading drawInRect:headingRect withAttributes:@{
                NSFontAttributeName:[UIFont boldSystemFontOfSize:std::min(CGFloat(16),headingRect.size.height)],
                NSForegroundColorAttributeName:UIColor.whiteColor,
                NSParagraphStyleAttributeName:centered}];
        }
        // The original 2D background does not itself include the native
        // campaign transcript. Keep that auxiliary text ONLY on deep menu
        // pages where it is useful; main-menu artwork stays unobstructed.
        if(_menuDetailsVisible && _body.length) {
            const CGRect textRect=CGRectMake(dest.origin.x+dest.size.width*0.22,
                dest.origin.y+dest.size.height*0.833,
                dest.size.width*0.56,dest.size.height*0.125);
            NSMutableParagraphStyle* centered=[NSMutableParagraphStyle new];
            centered.alignment=NSTextAlignmentCenter;
            centered.lineBreakMode=NSLineBreakByWordWrapping;
            [_body drawWithRect:textRect
                options:NSStringDrawingUsesLineFragmentOrigin|NSStringDrawingUsesFontLeading
                attributes:@{NSFontAttributeName:[UIFont systemFontOfSize:11 weight:UIFontWeightMedium],
                    NSForegroundColorAttributeName:UIColor.whiteColor,
                    NSParagraphStyleAttributeName:centered} context:nil];
        }
    } else {
        // Results/dialogue have distinct original state and presentation
        // guards. Retain readable text with the animated results accumulator.
        [[UIColor colorWithWhite:0.5 alpha:0.06] setFill];
        const CGFloat phase=std::fmod(static_cast<double>(_backgroundOffset)/65536.0,48.0);
        for(CGFloat y=phase-48;y<self.bounds.size.height;y+=48)
            UIRectFill(CGRectMake(0,y,self.bounds.size.width,1));
        const CGRect headingRect=CGRectMake(16,14,self.bounds.size.width-32,34);
        G1BitmapFontFace* face=[self originalHeadingFace];
        if(face)[face drawText:_heading rect:headingRect height:22 alignment:NSTextAlignmentLeft];
        else [_heading drawInRect:headingRect withAttributes:@{
            NSFontAttributeName:[UIFont systemFontOfSize:22 weight:UIFontWeightBold],
            NSForegroundColorAttributeName:[UIColor colorWithRed:1 green:0.84 blue:0.25 alpha:1]}];
        [_body drawWithRect:CGRectMake(16,62,self.bounds.size.width-32,self.bounds.size.height-74)
            options:NSStringDrawingUsesLineFragmentOrigin|NSStringDrawingUsesFontLeading
            attributes:[self bodyAttributes] context:nil];
    }
    _drawnToken=_token;
}
@end
