#pragma once

#import <UIKit/UIKit.h>
#include "NativeBitmapFont.hpp"
#include "NativeSourceArt.hpp"
#include "g1/OriginalPresentation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

// The original tactical status strip: resource 514 (full-width silver frame),
// 874 (red holographic backing), 709 (blue Energon indicator), and the actual
// indexed source portrait BMPs. The renderer only displays native session
// values. It owns no gameplay clock, input state, or save operations.
@interface G1TacticalHUDCanvas : UIView
- (void)loadOriginalArt:(const g1::Archive&)archive;
- (void)displayUnit:(NSString*)name health:(std::int32_t)health
         attackLow:(std::int32_t)attackLow attackHigh:(std::int32_t)attackHigh
            armor:(std::int32_t)armor speed:(std::int32_t)speed
           funds:(std::int32_t)funds;
- (void)displayMission:(NSString*)title objective:(NSString*)objective
                 funds:(std::int32_t)funds;
- (void)clearStatus;
@end

@implementation G1TacticalHUDCanvas {
    UIImage* _frameArtwork;
    UIImage* _redBackground;
    UIImage* _energonArtwork;
    UIImage* _portraitArtwork;
    NSMutableDictionary<NSNumber*,UIImage*>* _portraits;
    G1BitmapFontFace* _font;
    G1BitmapFontFace* _redFont;
    NSString* _name;
    NSString* _objective;
    std::int32_t _hp,_low,_high,_def,_speed,_funds;
    BOOL _showsUnit;
}

- (instancetype)initWithFrame:(CGRect)frame {
    if((self=[super initWithFrame:frame])) {
        self.opaque=NO;self.clipsToBounds=YES;
        self.contentMode=UIViewContentModeRedraw;
        self.isAccessibilityElement=YES;
        _portraits=[NSMutableDictionary new];
    }
    return self;
}

- (void)loadOriginalArt:(const g1::Archive&)archive {
    _frameArtwork=g1::source_art::png(archive,514,480,62);
    _redBackground=g1::source_art::png(archive,874,128,63);
    _energonArtwork=g1::source_art::png(archive,709,51,54);
    _font=[[G1BitmapFontFace alloc] initWithArchive:archive slot:2];
    _redFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:4];
    // The four confirmed main story portraits come from the original BMP
    // resources (masked magenta), not from the M3G texture atlas.
    for(const auto info : {g1::source_art::Portrait{731,54,50},
                           g1::source_art::Portrait{396,70,66},
                           g1::source_art::Portrait{878,73,70},
                           g1::source_art::Portrait{670,54,51}})
        _portraits[@(info.id)]=g1::source_art::indexed_bmp(archive,info.id,info.width,info.height);
    [self setNeedsDisplay];
}

- (void)displayUnit:(NSString*)name health:(std::int32_t)health
         attackLow:(std::int32_t)attackLow attackHigh:(std::int32_t)attackHigh
            armor:(std::int32_t)armor speed:(std::int32_t)speed
           funds:(std::int32_t)funds {
    NSString* title=name ?: @"";
    const auto info=g1::source_art::tactical_portrait(title);
    UIImage* portrait=info.id ? _portraits[@(info.id)] : nil;
    if(_showsUnit && [_name isEqualToString:title] && !_objective &&
       _hp==health && _low==attackLow && _high==attackHigh && _def==armor &&
       _speed==speed && _funds==funds && _portraitArtwork==portrait)return;
    _showsUnit=YES;_name=[title copy];_objective=nil;
    _hp=health;_low=attackLow;_high=attackHigh;_def=armor;_speed=speed;_funds=funds;
    _portraitArtwork=portrait;
    self.accessibilityLabel=[NSString stringWithFormat:@"%@. Health %d. Attack %d to %d. Defense %d. Speed %d. Energon %d.",
        title,int(health),int(attackLow),int(attackHigh),int(armor),int(speed),int(funds)];
    [self setNeedsDisplay];
}

- (void)displayMission:(NSString*)title objective:(NSString*)objective
                 funds:(std::int32_t)funds {
    NSString* heading=title ?: @"";
    NSString* info=objective ?: @"";
    if(!_showsUnit && [_name isEqualToString:heading] && [_objective isEqualToString:info] && _funds==funds)return;
    _showsUnit=NO;_name=[heading copy];_objective=[info copy];_funds=funds;
    _portraitArtwork=nil;
    self.accessibilityLabel=[NSString stringWithFormat:@"%@. %@. Energon %d.",heading,info,int(funds)];
    [self setNeedsDisplay];
}
- (void)clearStatus {
    _name=nil;_objective=nil;_portraitArtwork=nil;_showsUnit=NO;_funds=0;
    self.accessibilityLabel=@"";[self setNeedsDisplay];
}

- (void)drawLine:(NSString*)line in:(CGRect)area size:(CGFloat)height color:(UIColor*)color {
    if(!line.length)return;
    G1BitmapFontFace* face=[color isEqual:UIColor.whiteColor]?_font:_redFont;
    if([face supportsText:line] && [face sizeForText:line height:height].width<=area.size.width) {
        [face drawText:line rect:area height:height alignment:NSTextAlignmentLeft];
    } else {
        [line drawWithRect:area options:NSStringDrawingUsesLineFragmentOrigin
            attributes:@{NSFontAttributeName:[UIFont fontWithName:@"Menlo-Bold" size:height] ?: [UIFont boldSystemFontOfSize:height],
                         NSForegroundColorAttributeName:color ?: UIColor.whiteColor} context:nil];
    }
}

- (void)drawRect:(CGRect)unused {
    (void)unused;
    if(!_frameArtwork || self.bounds.size.width<=0 || self.bounds.size.height<=0)return;
    CGContextRef ctx=UIGraphicsGetCurrentContext();if(!ctx)return;
    CGContextSaveGState(ctx);
    // One uniform source-space scale: never stretch the legacy grid, robot
    // portraits or control artwork independently along X and Y.
    const CGFloat scale=self.bounds.size.height/62.0;
    const CGFloat extra=self.bounds.size.width/scale-480.0;
    CGContextScaleCTM(ctx,scale,scale);
    CGContextSetInterpolationQuality(ctx,kCGInterpolationNone);
    CGContextSetShouldAntialias(ctx,false);
    if(_redBackground){
        [_redBackground drawInRect:CGRectMake(59,11,285+extra,50)];
        [_redBackground drawInRect:CGRectMake(367+extra,10,62,52)];
    }
    [[_frameArtwork resizableImageWithCapInsets:UIEdgeInsetsMake(0,72,0,142)]
        drawInRect:CGRectMake(0,0,480+extra,62)];
    if(_energonArtwork)[_energonArtwork drawInRect:g1::adaptive_ui::aspect_fit(_energonArtwork,CGRectMake(433+extra,11,47,50))];
    if(_portraitArtwork)[_portraitArtwork drawInRect:g1::adaptive_ui::aspect_fit(_portraitArtwork,CGRectMake(1,0,60,61))];
    UIColor* red=[UIColor colorWithRed:1 green:0.36 blue:0.37 alpha:1];
    if(_showsUnit){
        [self drawLine:_name in:CGRectMake(94,21,126+extra,18) size:13 color:red];
        [self drawLine:[NSString stringWithFormat:@"%d",int(_hp)]
            in:CGRectMake(73,39,46,18) size:12 color:red];
        [self drawLine:@"ATK" in:CGRectMake(217+extra,20,49,14) size:11 color:red];
        [self drawLine:@"DEF" in:CGRectMake(279+extra,20,37,14) size:11 color:red];
        [self drawLine:@"SPD" in:CGRectMake(316+extra,20,37,14) size:11 color:red];
        [self drawLine:[NSString stringWithFormat:@"%d-%d",int(_low),int(_high)]
            in:CGRectMake(217+extra,39,57,17) size:12 color:red];
        [self drawLine:[NSString stringWithFormat:@"%d",int(_def)]
            in:CGRectMake(282+extra,39,34,17) size:12 color:red];
        [self drawLine:[NSString stringWithFormat:@"%d",int(_speed)]
            in:CGRectMake(318+extra,39,32,17) size:12 color:red];
    }else{
        [self drawLine:_name in:CGRectMake(85,21,243+extra,14) size:12 color:red];
        [self drawLine:_objective in:CGRectMake(84,37,249+extra,17) size:9 color:UIColor.whiteColor];
    }
    [self drawLine:[NSString stringWithFormat:@"%d",int(_funds)]
         in:CGRectMake(400+extra,39,29,16) size:11 color:red];
    CGContextRestoreGState(ctx);
}
@end
