#pragma once

#import <UIKit/UIKit.h>
#include "NativeBitmapFont.hpp"
#include "NativeSourceArt.hpp"
#include "NativeAdaptiveUI.hpp"
#include "g1/M3GBattlePresentation.hpp"
#include <algorithm>

// Native point-based HUD over the original animated M3G presentation. Every
// health/damage value comes from the recovered controller; this view has no
// private animation clock, invented HP tween or gameplay acknowledgement.
@interface G1BattleCanvas : UIView
- (void)resetPresentation;
- (void)setDamageAnchor:(CGPoint)normalized token:(std::uint64_t)token revision:(std::uint64_t)revision;
- (void)loadOriginalFonts:(const g1::Archive&)archive;
- (void)showStatus:(const g1::M3GBattleStatus&)status left:(NSString*)left right:(NSString*)right
        leftFaction:(std::uint8_t)leftFaction rightFaction:(std::uint8_t)rightFaction;
- (BOOL)hasDrawnToken:(std::uint64_t)token revision:(std::uint64_t)revision;
@end

@implementation G1BattleCanvas {
    std::optional<g1::M3GBattleStatus> _status;
    NSString* _leftName;
    NSString* _rightName;
    std::uint64_t _drawnToken, _drawnRevision;
    G1BitmapFontFace* _originalNameFont;
    G1BitmapFontFace* _originalDamageFont;
    G1BitmapFontFace* _redFont;
    G1BitmapFontFace* _purpleFont;
    UIImage* _sourceFrame;
    UIImage* _redGrid;
    UIImage* _purpleGrid;
    std::uint8_t _leftFaction,_rightFaction;
    CGPoint _damageAnchor;
    std::uint64_t _damageAnchorToken,_damageAnchorRevision;
    G1BitmapFontFace* _hpRedFont;
    G1BitmapFontFace* _hpPurpleFont;
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) { self.opaque = NO;self.contentMode=UIViewContentModeRedraw; self.clipsToBounds = YES; self.isAccessibilityElement = YES; }
    return self;
}

- (void)loadOriginalFonts:(const g1::Archive&)archive {
    _originalNameFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:2];
    _originalDamageFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:3];
    _redFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:6];
    _hpRedFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:9];
    _purpleFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:7];
    _hpPurpleFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:10];
    _sourceFrame=g1::source_art::png(archive,1038,156,58);
    _redGrid=g1::source_art::png(archive,874,128,63);
    _purpleGrid=g1::source_art::png(archive,799,128,63);
    _drawnToken=0; _drawnRevision=0;
    [self setNeedsDisplay];
}

- (void)showStatus:(const g1::M3GBattleStatus&)status left:(NSString*)left right:(NSString*)right
        leftFaction:(std::uint8_t)leftFaction rightFaction:(std::uint8_t)rightFaction {
    if (_status && _status->token == status.token && _status->revision == status.revision &&
        _leftFaction==leftFaction && _rightFaction==rightFaction &&
        (!status.damage_text || _status->phase_time_ms==status.phase_time_ms)) return;
    _leftFaction=leftFaction;_rightFaction=rightFaction;
    if (!_status || _status->token != status.token) { _drawnToken = 0; _drawnRevision = 0; }
    _status = status;
    _leftName = [left copy]; _rightName = [right copy];
    [self setNeedsDisplay];
}

- (void)setDamageAnchor:(CGPoint)normalized token:(std::uint64_t)token revision:(std::uint64_t)revision {
    if(!std::isfinite(normalized.x)||!std::isfinite(normalized.y))return;
    _damageAnchor=normalized;_damageAnchorToken=token;_damageAnchorRevision=revision;
    if(_status && _status->damage_text)[self setNeedsDisplay];
}
- (void)resetPresentation {
    _status.reset(); _leftName = nil; _rightName = nil;
    _drawnToken = 0; _drawnRevision = 0;
    self.accessibilityLabel = @"";
    [self setNeedsDisplay];
}

- (BOOL)hasDrawnToken:(std::uint64_t)token revision:(std::uint64_t)revision {
    return token != 0 && _drawnToken == token && _drawnRevision == revision;
}

- (void)drawCombatant:(NSString*)name health:(std::int32_t)health
                    x:(CGFloat)x faction:(std::uint8_t)faction {
    // 1038 is the original 156x58 mechanical plate with alpha windows;
    // 874/799 are the genuine red/purple animated-style grid textures.
    UIImage* grid=faction==0?_redGrid:_purpleGrid;
    if(grid)[grid drawInRect:CGRectMake(x+6,7,144,50)];
    if(_sourceFrame)[_sourceFrame drawInRect:CGRectMake(x,0,156,58)];
    UIColor* color=faction==0 ?
        [UIColor colorWithRed:1 green:0.32 blue:0.33 alpha:1] :
        [UIColor colorWithRed:0.8 green:0.36 blue:1 alpha:1];
    G1BitmapFontFace* factionFont=faction==0?_redFont:_purpleFont;
    G1BitmapFontFace* hpFont=faction==0?_hpRedFont:_hpPurpleFont;
    NSString* hp=[NSString stringWithFormat:@"%d",int(health)];
    const CGRect hpRect=CGRectMake(x+61,16,79,17);
    if([hpFont supportsText:hp] &&
       [hpFont sizeForText:hp height:16].width<hpRect.size.width)
        [hpFont drawText:hp rect:hpRect height:16 alignment:NSTextAlignmentCenter];
    else [hp drawInRect:hpRect withAttributes:@{NSFontAttributeName:
         [UIFont fontWithName:@"Menlo-Bold" size:16] ?: [UIFont boldSystemFontOfSize:16],
         NSForegroundColorAttributeName:color}];
    const CGRect nameRect=CGRectMake(x+25,36,113,18);
    if([factionFont supportsText:name] &&
       [factionFont sizeForText:name height:12].width<=nameRect.size.width)
        [factionFont drawText:name rect:nameRect height:12 alignment:NSTextAlignmentCenter];
    else [name drawInRect:nameRect withAttributes:@{NSFontAttributeName:
         [UIFont fontWithName:@"Menlo-Bold" size:12] ?: [UIFont boldSystemFontOfSize:12],
         NSForegroundColorAttributeName:color}];
}

- (void)drawRect:(CGRect)rect {
    (void)rect;
    if(!_status || self.bounds.size.width<=0 || self.bounds.size.height<=0)return;
    CGContextRef ctx=UIGraphicsGetCurrentContext();if(!ctx)return;
    const auto l=g1::adaptive::layout(self.bounds.size.width,self.bounds.size.height);
    if(l.scale<=0)return;
    const CGFloat w=l.virtual_width,h=l.virtual_height;
    const auto& status=*_status;
    CGContextSaveGState(ctx);CGContextScaleCTM(ctx,l.scale,l.scale);
    CGContextSetInterpolationQuality(ctx,kCGInterpolationHigh);
    if(status.health_visible) {
        CGContextSaveGState(ctx);CGContextTranslateCTM(ctx,0,h-58);
        [self drawCombatant:_leftName health:status.display_health[0] x:0 faction:_leftFaction];
        [self drawCombatant:_rightName health:status.display_health[1] x:w-156 faction:_rightFaction];
        CGContextRestoreGState(ctx);
    }
    // Damage lifetime/value/side are owned by the original battle controller.
    // This is presentation only; no fabricated damage or second combat clock.
    if(status.damage_text) {
        NSString* amount=[NSString stringWithFormat:@"%d",int(*status.damage_text)];
        const bool actualAnchor=_damageAnchorToken==status.token && _damageAnchorRevision==status.revision;
        const CGFloat x=w*(actualAnchor?std::clamp(_damageAnchor.x,CGFloat(.08),CGFloat(.92)):
                             (status.damage_on_left?0.25:0.75));
        const CGFloat y=h*(actualAnchor?std::clamp(_damageAnchor.y,CGFloat(.12),CGFloat(.70)):.35);
        const CGFloat lift=std::clamp(CGFloat(status.phase_time_ms)/40,CGFloat(0),CGFloat(14));
        NSMutableParagraphStyle* centered=[NSMutableParagraphStyle new];centered.alignment=NSTextAlignmentCenter;
        const CGRect damageRect=CGRectMake(x-55,y-lift,110,45);
        if([_originalDamageFont supportsText:amount] &&
           [_originalDamageFont sizeForText:amount height:34].width<=damageRect.size.width)
            [_originalDamageFont drawText:amount rect:damageRect height:34 alignment:NSTextAlignmentCenter];
        else [amount drawInRect:damageRect withAttributes:@{
            NSFontAttributeName:[UIFont boldSystemFontOfSize:34],
            NSForegroundColorAttributeName:UIColor.whiteColor,
            NSStrokeColorAttributeName:UIColor.blackColor,NSStrokeWidthAttributeName:@(-7),
            NSParagraphStyleAttributeName:centered}];
    }
    CGContextRestoreGState(ctx);
    _drawnToken=status.token;_drawnRevision=status.revision;
    self.accessibilityLabel=[NSString stringWithFormat:@"%@ HP %d. %@ HP %d.",
        _leftName,int(status.display_health[0]),_rightName,int(status.display_health[1])];
}

@end
