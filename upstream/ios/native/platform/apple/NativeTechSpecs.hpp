#pragma once

#import <UIKit/UIKit.h>
#import <MetalKit/MetalKit.h>
#include "NativeBitmapFont.hpp"
#include "NativeSourceArt.hpp"
#include "MetalM3GRenderer.hpp"
#include "g1/TechSpecsModel.hpp"
#include <algorithm>
#include <cmath>
#include <memory>

// Source 417 (Decepticon) and 1224 (Autobot) are the COMPLETE original
// 480x320 character Tech Specs frames, including both faction arrow banks,
// robot/vehicle sockets, caption and Energon windows. No generated art.
@interface G1TechSpecsCanvas : UIView
- (void)loadOriginalArt:(const g1::Archive&)archive;
- (void)show:(const g1::TechSpecsData&)specs name:(NSString*)name quote:(NSString*)quote
       details:(NSArray<NSString*>*)details portrait:(UIImage*)portrait;
@end

@implementation G1TechSpecsCanvas {
    UIImage* _autobotSheet;
    UIImage* _decepticonSheet;
    UIImage* _energyIcon;
    UIImage* _robotRedIcon;
    UIImage* _vehicleRedIcon;
    UIImage* _robotPurpleIcon;
    G1BitmapFontFace* _originalFont;
    G1BitmapFontFace* _accentRedFont;
    G1BitmapFontFace* _accentPurpleFont;
    G1BitmapFontFace* _titleRedFont;
    G1BitmapFontFace* _titlePurpleFont;
    NSString* _name;
    NSString* _quote;
    NSArray<NSString*>* _details;
    UIImage* _portrait;
    g1::TechSpecsData _data;
    BOOL _hasData;
}
- (instancetype)initWithFrame:(CGRect)frame {
    self=[super initWithFrame:frame];
    if(self){self.opaque=YES;self.contentMode=UIViewContentModeRedraw;self.backgroundColor=UIColor.blackColor;
        self.clipsToBounds=YES;self.isAccessibilityElement=NO;}
    return self;
}
- (void)loadOriginalArt:(const g1::Archive&)archive {
    _autobotSheet=g1::source_art::png(archive,1224,480,320);
    _decepticonSheet=g1::source_art::png(archive,417,480,320);
    _energyIcon=g1::source_art::png(archive,709,51,54);
    _robotRedIcon=g1::source_art::png(archive,442,52,49);
    _vehicleRedIcon=g1::source_art::png(archive,904,52,49);
    _robotPurpleIcon=g1::source_art::png(archive,657,52,49);
    _originalFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:2];
    _accentRedFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:4];
    _accentPurpleFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:5];
    _titleRedFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:6];
    _titlePurpleFont=[[G1BitmapFontFace alloc] initWithArchive:archive slot:7];
}
- (void)show:(const g1::TechSpecsData&)specs name:(NSString*)name quote:(NSString*)quote
       details:(NSArray<NSString*>*)details portrait:(UIImage*)portrait {
    _data=specs;_hasData=YES;_name=[name copy];_quote=[quote copy];
    _details=[details copy];_portrait=portrait;
    self.accessibilityLabel=[NSString stringWithFormat:
      @"Tech Specs: %@. Robot attack %d to %d, defense %d, speed %u. Vehicle attack %d to %d, defense %d, speed %u. Energon cost %d. %@",
      name,int(specs.form[0].combat.attack_low),int(specs.form[0].combat.attack_high),
      int(specs.form[0].combat.defense),unsigned(specs.form[0].speed),
      int(specs.form[1].combat.attack_low),int(specs.form[1].combat.attack_high),
      int(specs.form[1].combat.defense),unsigned(specs.form[1].speed),int(specs.energon_price),quote];
    [self setNeedsDisplay];
}
- (void)smallLine:(NSString*)line area:(CGRect)area height:(CGFloat)height color:(UIColor*)color
          align:(NSTextAlignment)align {
    if(!line.length)return;
    G1BitmapFontFace* face=[color isEqual:UIColor.whiteColor]?_originalFont:
        height>=16?(_data.faction==0?_titleRedFont:_titlePurpleFont):
        (_data.faction==0?_accentRedFont:_accentPurpleFont);
    if(face && [face supportsText:line] &&
       [face sizeForText:line height:height].width<=area.size.width) {
        [face drawText:line rect:area height:height alignment:align];
    } else {
        NSMutableParagraphStyle* style=[NSMutableParagraphStyle new];style.alignment=align;
        [line drawInRect:area withAttributes:@{
           NSFontAttributeName:[UIFont fontWithName:@"Menlo-Bold" size:height] ?:
                                 [UIFont boldSystemFontOfSize:height],
           NSForegroundColorAttributeName:color,NSParagraphStyleAttributeName:style}];
    }
}
- (void)drawWrapped:(NSString*)text x:(CGFloat)x y:(CGFloat)y width:(CGFloat)width
              maxLines:(NSUInteger)maxLines fontHeight:(CGFloat)height color:(UIColor*)color {
    if(!text.length)return;
    // Source font is single-line. Wrap with original character metrics rather
    // than drawing off the edge of the original left/right metadata windows.
    NSMutableArray<NSString*>* lines=[NSMutableArray new];
    NSMutableString* current=[NSMutableString new];
    for(NSString* word in [text componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]) {
        if(!word.length)continue;
        NSString* proposed=current.length?[NSString stringWithFormat:@"%@ %@",current,word]:word;
        const CGFloat measured=([_originalFont supportsText:proposed] ?
                 [_originalFont sizeForText:proposed height:height].width :
                 proposed.length*height*0.67);
        if(current.length && measured>width){[lines addObject:[current copy]];[current setString:word];}
        else [current setString:proposed];
    }
    if(current.length)[lines addObject:[current copy]];
    const CGFloat spacing=height+1.5;
    for(NSUInteger i=0;i<MIN(maxLines,lines.count);++i)
        [self smallLine:lines[i] area:CGRectMake(x,y+spacing*i,width,height+2)
                height:height color:color align:NSTextAlignmentLeft];
}
- (void)drawRect:(CGRect)rect {
    (void)rect;
    if(!_hasData || self.bounds.size.width<=0 || self.bounds.size.height<=0)return;
    CGContextRef ctx=UIGraphicsGetCurrentContext();if(!ctx)return;
    CGContextSaveGState(ctx);
    const CGFloat scale=self.bounds.size.height/320.0;
    const CGFloat extra=self.bounds.size.width/scale-480.0;
    CGContextScaleCTM(ctx,scale,scale);
    CGContextSetInterpolationQuality(ctx,kCGInterpolationNone);
    CGContextSetShouldAntialias(ctx,false);
    [[(_data.faction==0?_autobotSheet:_decepticonSheet)
        resizableImageWithCapInsets:UIEdgeInsetsMake(36,123,12,125) resizingMode:UIImageResizingModeTile]
        drawInRect:CGRectMake(0,0,480+extra,320)];
    UIColor* accent=_data.faction==0 ?
       [UIColor colorWithRed:1 green:.38 blue:.4 alpha:1] :
       [UIColor colorWithRed:.8 green:.38 blue:1 alpha:1];
    [self smallLine:_name area:CGRectMake(115,4,253+extra,25) height:18 color:accent
              align:NSTextAlignmentCenter];
    for(NSUInteger form=0;form<2;++form) {
        if(form>=_data.forms)continue;
        const auto& entry=_data.form[form];
        const CGFloat x=form==0?78:363+extra;
        [self smallLine:@"ATK" area:CGRectMake(x,51,39,16) height:13 color:accent align:NSTextAlignmentCenter];
        NSString* atk=entry.combat.attack_low<=0 && entry.combat.attack_high<=0 ? @"N/A" :
          [NSString stringWithFormat:@"%d-%d",int(entry.combat.attack_low),int(entry.combat.attack_high)];
        [self smallLine:atk area:CGRectMake(x-3,65,45,14) height:11 color:UIColor.whiteColor
                  align:NSTextAlignmentCenter];
        [self smallLine:@"DEF" area:CGRectMake(x,77,39,16) height:13 color:accent align:NSTextAlignmentCenter];
        [self smallLine:[NSString stringWithFormat:@"%d",int(entry.combat.defense)]
                  area:CGRectMake(x-3,91,45,14) height:11 color:UIColor.whiteColor align:NSTextAlignmentCenter];
        [self smallLine:@"SPD" area:CGRectMake(x,101,39,16) height:13 color:accent align:NSTextAlignmentCenter];
        [self smallLine:[NSString stringWithFormat:@"%u",unsigned(entry.speed)]
                  area:CGRectMake(x-3,115,45,14) height:11 color:UIColor.whiteColor align:NSTextAlignmentCenter];
    }
    UIImage* unitSymbol=_data.faction==0?_robotRedIcon:_robotPurpleIcon;
    [unitSymbol drawInRect:CGRectMake(74,139,42,39)];
    if(_data.faction==0)[_vehicleRedIcon drawInRect:CGRectMake(363+extra,139,42,39)];
    // Only source-confirmed abilities are supplied by the caller. The original
    // tactical core owns combat bonuses; no unverified ability is invented.
    CGFloat y=184;
    for(NSString* info in _details) {
        [self drawWrapped:info x:14 y:y width:139 maxLines:2 fontHeight:8 color:UIColor.whiteColor];
        y+=11;
        if(y>225)break;
    }
    [self drawWrapped:_quote x:360+extra y:183 width:107 maxLines:4 fontHeight:8 color:UIColor.whiteColor];
    if(_energyIcon)[_energyIcon drawInRect:CGRectMake(44,247,37,38)];
    [self smallLine:[NSString stringWithFormat:@"%d",int(_data.energon_price)]
              area:CGRectMake(49,287,30,20) height:16 color:accent align:NSTextAlignmentCenter];
    // Small original-game portrait when a source identity is known. The full
    // original 3D character mesh is drawn independently in the central Metal view.
    if(_portrait)[_portrait drawInRect:g1::adaptive_ui::aspect_fit(_portrait,CGRectMake(387+extra,246,62,63))];
    CGContextRestoreGState(ctx);
}
@end

// An actual M3G character model, not a still-image cutout. Uses the existing
// native Metal adapter and source skins. Read-only: does not advance battle
// logic, apply health changes, consume actions, or write a save.
@interface G1TechSpecsModelView : MTKView <MTKViewDelegate>
- (instancetype)initWithMetalDevice:(id<MTLDevice>)device;
- (BOOL)displayUnit:(std::uint8_t)type form:(std::uint8_t)form
            archive:(const g1::Archive&)archive;
- (NSString*)lastModelError;
@end

@implementation G1TechSpecsModelView {
    std::unique_ptr<g1::TechSpecsModel> _sourceModel;
    std::unique_ptr<g1::MetalM3GRenderer> _renderer;
    std::shared_ptr<g1::MetalM3GModel> _uploaded;
    id<MTLCommandQueue> _commands;
    float _orbitAngle;
    NSString* _lastModelError;
}
- (instancetype)initWithMetalDevice:(id<MTLDevice>)device {
    self=[super initWithFrame:CGRectZero device:device];
    if(self) {
        self.delegate=self;
        self.colorPixelFormat=MTLPixelFormatBGRA8Unorm;
        self.depthStencilPixelFormat=MTLPixelFormatDepth32Float;
        self.framebufferOnly=YES;
        self.opaque=NO;self.contentMode=UIViewContentModeRedraw;
        self.backgroundColor=UIColor.clearColor;
        self.layer.opaque=NO;
        self.clearColor=MTLClearColorMake(0,0,0,0);
        self.paused=YES;self.enableSetNeedsDisplay=YES;
        _commands=[device newCommandQueue];
        UIPanGestureRecognizer* turn=[[UIPanGestureRecognizer alloc]
            initWithTarget:self action:@selector(turnModel:)];
        turn.maximumNumberOfTouches=1;
        [self addGestureRecognizer:turn];
        self.isAccessibilityElement=YES;
        self.accessibilityLabel=@"Original rotating Transformer 3D model. Drag to rotate.";
    }
    return self;
}
- (BOOL)displayUnit:(std::uint8_t)type form:(std::uint8_t)form
            archive:(const g1::Archive&)archive {
    try {
        if(!_sourceModel || _sourceModel->type()!=type) {
            auto next=std::make_unique<g1::TechSpecsModel>(archive,type);
            if(!_renderer)_renderer=std::make_unique<g1::MetalM3GRenderer>(self.device);
            auto uploaded=_renderer->upload(next->asset());
            _sourceModel=std::move(next);
            _uploaded=std::move(uploaded);
            _orbitAngle=0;
        }
        _sourceModel->set_form(form);
        _lastModelError=nil;self.hidden=NO;
        [self setNeedsDisplay];return YES;
    }catch(const std::exception& error) {
        _sourceModel.reset();_uploaded.reset();
        _lastModelError=[NSString stringWithUTF8String:error.what()];self.hidden=YES;
        return NO;
    }
}
- (NSString*)lastModelError {return _lastModelError;}
- (void)turnModel:(UIPanGestureRecognizer*)gesture {
    const CGPoint delta=[gesture translationInView:self];
    if(std::isfinite(delta.x) && self.bounds.size.width>0) {
        _orbitAngle+=static_cast<float>(delta.x/self.bounds.size.width)*1.6F;
        [gesture setTranslation:CGPointZero inView:self];
        [self setNeedsDisplay];
    }
}
- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {(void)view;(void)size;[self setNeedsDisplay];}
- (void)drawInMTKView:(MTKView*)view {
    if(!_sourceModel || !_uploaded || !_renderer || !_commands)return;
    @autoreleasepool {
        try {
            id<CAMetalDrawable> drawable=view.currentDrawable;
            MTLRenderPassDescriptor* pass=view.currentRenderPassDescriptor;
            if(!drawable || !pass)return;
            const CGSize size=view.drawableSize;
            if(size.width<1 || size.height<1)return;
            pass.colorAttachments[0].loadAction=MTLLoadActionClear;
            pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,0);
            pass.depthAttachment.loadAction=MTLLoadActionClear;
            pass.depthAttachment.clearDepth=1;
            const auto prepared=_sourceModel->prepare(
                static_cast<std::uint32_t>(size.width),static_cast<std::uint32_t>(size.height),_orbitAngle);
            id<MTLCommandBuffer> buffer=[_commands commandBuffer];
            id<MTLRenderCommandEncoder> encoder=[buffer renderCommandEncoderWithDescriptor:pass];
            if(!encoder)throw g1::FormatError("Original M3G model Metal encoder unavailable");
            _renderer->encode_frame(encoder,{_uploaded},prepared);
            [encoder endEncoding];[buffer presentDrawable:drawable];[buffer commit];
        } catch(const std::exception& error) {
            _lastModelError=[NSString stringWithUTF8String:error.what()];self.hidden=YES;
        }
    }
}
@end
