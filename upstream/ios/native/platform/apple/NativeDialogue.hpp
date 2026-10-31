#pragma once

#import <UIKit/UIKit.h>
#include "g1/MissionExecution.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

// Native TextKit layout and drawing. The mission receives measured line
// geometry and completed drawing feedback, never a guessed character count.
@interface G1DialogueCanvas : UIView
- (void)resetPresentation;
- (void)setOriginalText:(NSString*)text;
- (void)setPresentationScale:(CGFloat)scale;
- (void)showProgress:(const g1::DialogueProgress&)progress;
- (g1::DialogueLayout)layoutFeedback;
- (NSUInteger)drawnRevision;
@end

@implementation G1DialogueCanvas {
    NSTextStorage* _storage;
    NSLayoutManager* _layout;
    NSTextContainer* _container;
    UIFont* _font;
    NSString* _originalText;
    std::vector<NSRange> _lines;
    std::vector<CGRect> _lineRects;
    g1::DialogueProgress _progress;
    NSRange _drawRange;
    CGPoint _drawOrigin;
    CGFloat _layoutWidth;
    NSInteger _lineHeight, _inkHeight;
    NSUInteger _revision, _drawnRevision;
    bool _requestedFull, _drawnFull;
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.opaque = NO;
        self.clipsToBounds = YES;
        self.isAccessibilityElement = YES;
        // A compact mono bitmap-like face stays legible at the source 480x320
        // panel size, rather than the oversized system font from Build012.
        _font = [UIFont fontWithName:@"Menlo-Regular" size:11] ?: [UIFont systemFontOfSize:11 weight:UIFontWeightMedium];
        _storage = [NSTextStorage new];
        _layout = [NSLayoutManager new];
        _layout.usesFontLeading = NO;
        _container = [[NSTextContainer alloc] initWithSize:CGSizeMake(1,CGFLOAT_MAX)];
        _container.lineFragmentPadding = 0;
        [_layout addTextContainer:_container];
        [_storage addLayoutManager:_layout];
        _lineHeight = static_cast<NSInteger>(std::ceil(_font.lineHeight)) + 2;
        _inkHeight = static_cast<NSInteger>(std::ceil(_font.ascender - _font.descender));
    }
    return self;
}

- (void)setPresentationScale:(CGFloat)scale {
    const CGFloat points=std::max(CGFloat(8),11*scale);
    if(std::abs(_font.pointSize-points)<0.05)return;
    _font=[UIFont fontWithName:@"Menlo-Regular" size:points] ?: [UIFont systemFontOfSize:points];
    _lineHeight=static_cast<NSInteger>(std::ceil(_font.lineHeight))+2;
    _inkHeight=static_cast<NSInteger>(std::ceil(_font.ascender-_font.descender));
    NSString* text=_originalText ?: @"";_originalText=nil;
    [self setOriginalText:text];
}
- (void)setOriginalText:(NSString*)text {
    if ([_originalText isEqualToString:text]) return;
    _originalText = [text copy];
    NSMutableParagraphStyle* paragraph = [NSMutableParagraphStyle new];
    paragraph.minimumLineHeight = _lineHeight;
    paragraph.maximumLineHeight = _lineHeight;
    paragraph.lineBreakMode = NSLineBreakByWordWrapping;
    [_storage setAttributedString:[[NSAttributedString alloc] initWithString:text attributes:@{
        NSFontAttributeName:_font, NSForegroundColorAttributeName:UIColor.whiteColor,
        NSParagraphStyleAttributeName:paragraph, NSLigatureAttributeName:@0}]];
    _layoutWidth = 0;
    ++_revision;
    _drawnFull = false;
    [self setNeedsLayout];
    [self setNeedsDisplay];
}

- (void)resetPresentation {
    _originalText = nil; _progress = {};
    _lines.clear(); _lineRects.clear();
    _drawRange = NSMakeRange(0,0); _drawOrigin = CGPointZero;
    _requestedFull = false; _drawnFull = false; _drawnRevision = 0;
    self.accessibilityLabel = @"";
    [self setOriginalText:@""];
}

- (void)ensureLineLayout {
    if (self.bounds.size.width <= 0) return;
    if (_layoutWidth != self.bounds.size.width) {
        _layoutWidth = self.bounds.size.width;
        _container.size = CGSizeMake(_layoutWidth,CGFLOAT_MAX);
        _lines.clear(); _lineRects.clear();
        [_layout ensureLayoutForTextContainer:_container];
        const NSRange all = [_layout glyphRangeForTextContainer:_container];
        [_layout enumerateLineFragmentsForGlyphRange:all usingBlock:
            ^(CGRect rect, CGRect used, NSTextContainer* container, NSRange glyphs, BOOL* stop) {
                (void)used; (void)container; (void)stop;
                _lines.push_back(glyphs); _lineRects.push_back(rect);
            }];
        if (!_lineRects.empty())
            _lineHeight = static_cast<NSInteger>(std::ceil(_lineRects.front().size.height));
        ++_revision; _drawnFull = false;
        [self setNeedsDisplay];
    }
}

- (void)updateDrawRange {
    [self ensureLineLayout];
    const auto first = std::min(static_cast<std::size_t>(std::max(_progress.first_line,0)),_lines.size());
    NSRange draw = NSMakeRange(0,0);
    CGPoint origin = CGPointZero;
    bool full = true;
    if (first < _lines.size()) {
        const CGFloat top = _lineRects[first].origin.y;
        std::size_t end = first;
        while (end < _lines.size() &&
               _lineRects[end].origin.y - top + _inkHeight < self.bounds.size.height)
            ++end;
        if (end == first && self.bounds.size.height >= _inkHeight) ++end;
        if (end > first) {
            const NSRange pageGlyphs = NSMakeRange(_lines[first].location,
                NSMaxRange(_lines[end-1]) - _lines[first].location);
            const NSRange chars = [_layout characterRangeForGlyphRange:pageGlyphs actualGlyphRange:nullptr];
            const NSInteger budget = std::max(g1::dialogue_character_budget(_progress),0);
            const NSUInteger shown = std::min(static_cast<NSUInteger>(budget),chars.length);
            NSRange visible = NSMakeRange(chars.location,shown);
            if (visible.length)
                visible = [_originalText rangeOfComposedCharacterSequencesForRange:visible];
            draw = [_layout glyphRangeForCharacterRange:visible actualCharacterRange:nullptr];
            draw = NSIntersectionRange(draw,pageGlyphs);
            full = shown >= chars.length;
            origin = CGPointMake(0,-top);
            self.accessibilityLabel = visible.length ? [_originalText substringWithRange:visible] : @"";
        } else {
            full = false;
        }
    }
    if (!NSEqualRanges(draw,_drawRange) || !CGPointEqualToPoint(origin,_drawOrigin) || full != _requestedFull) {
        _drawRange = draw; _drawOrigin = origin; _requestedFull = full;
        ++_revision; _drawnFull = false;
        [self setNeedsDisplay];
    }
}

- (void)layoutSubviews { [super layoutSubviews]; [self updateDrawRange]; }
- (void)showProgress:(const g1::DialogueProgress&)progress {
    _progress = progress;
    [self updateDrawRange];
}
- (g1::DialogueLayout)layoutFeedback {
    [self updateDrawRange];
    return {static_cast<std::int32_t>(_lines.size()),static_cast<std::int32_t>(_lineHeight),
            static_cast<std::int32_t>(_inkHeight),0,
            static_cast<std::int32_t>(_layoutWidth > 0 ? std::floor(self.bounds.size.height) : 0),
            _drawnRevision == _revision && _drawnFull};
}
- (NSUInteger)drawnRevision { return _drawnRevision; }
- (void)drawRect:(CGRect)rect {
    (void)rect;
    [self updateDrawRange];
    if (_drawRange.length) [_layout drawGlyphsForGlyphRange:_drawRange atPoint:_drawOrigin];
    _drawnRevision = _revision;
    _drawnFull = _requestedFull;
}
@end
