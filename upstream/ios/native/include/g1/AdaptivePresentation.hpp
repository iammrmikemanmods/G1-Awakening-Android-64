#pragma once
#include "g1/OriginalUI.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace g1::adaptive {
using Rect=original_ui::Rect;
struct Insets { double top{},left{},bottom{},right{}; };
enum class Anchor { start,center,end };
struct Layout {
    Rect full,safe,source;
    double scale{},virtual_width{},virtual_height{};
    Rect place(Rect r,Anchor horizontal=Anchor::center,Anchor vertical=Anchor::center) const noexcept {
        const double dx=horizontal==Anchor::start?0:horizontal==Anchor::end?virtual_width-480:(virtual_width-480)/2;
        const double dy=vertical==Anchor::start?0:vertical==Anchor::end?virtual_height-320:(virtual_height-320)/2;
        return {safe.x+(r.x+dx)*scale,safe.y+(r.y+dy)*scale,r.width*scale,r.height*scale};
    }
    Rect bottom(double source_height) const noexcept {
        return {safe.x,safe.bottom()-source_height*scale,safe.width,source_height*scale};
    }
};
inline Layout layout(double width,double height,Insets insets={}) {
    const auto valid=[](double x){return std::isfinite(x)&&x>=0;};
    if(!valid(width)||!valid(height)||!valid(insets.top)||!valid(insets.left)||
       !valid(insets.bottom)||!valid(insets.right))throw std::invalid_argument("Nonfinite/negative Apple viewport");
    Layout out{};out.full={0,0,width,height};
    if(width==0||height==0)return out;
    out.safe={std::min(insets.left,width),std::min(insets.top,height),
        std::max(0.0,width-insets.left-insets.right),std::max(0.0,height-insets.top-insets.bottom)};
    if(out.safe.width<=0||out.safe.height<=0)return out;
    out.scale=std::min(out.safe.width/480.0,out.safe.height/320.0);
    out.virtual_width=out.safe.width/out.scale;out.virtual_height=out.safe.height/out.scale;
    out.source=out.place({0,0,480,320});
    return out;
}
// Shared source-to-native map for two fixed-width sides and an expandable
// middle. Art and hit targets call the SAME functions. Portraits stay square.
struct HorizontalPanel {
    double scale{},width{},left{},right{};
    double extra() const noexcept {return width/scale-480.0;}
    double x(double source) const noexcept {
        if(source<=left)return source*scale;
        if(source>=480-right)return (source+extra())*scale;
        return (left+(source-left)*(480-left-right+extra())/(480-left-right))*scale;
    }
    Rect rect(Rect source,Anchor anchor=Anchor::start) const noexcept {
        const double shift=anchor==Anchor::end?extra():anchor==Anchor::center?extra()/2:0;
        return {(source.x+shift)*scale,source.y*scale,source.width*scale,source.height*scale};
    }
};
inline bool contains(Rect outer,Rect inner,double e=0.001) noexcept {
    return inner.x>=outer.x-e&&inner.y>=outer.y-e&&inner.right()<=outer.right()+e&&inner.bottom()<=outer.bottom()+e;
}
} // namespace g1::adaptive
