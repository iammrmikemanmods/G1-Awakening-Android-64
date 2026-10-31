#include "g1/SupportGeometry.hpp"

#include <cmath>

namespace g1 {

std::array<float,3> support_world_anchor(std::uint8_t tile_x,std::uint8_t tile_y) noexcept {
    const float x=(static_cast<float>(tile_x)+0.1F)*32.0F;
    const auto z=(static_cast<std::uint32_t>(tile_y)+1U)*32U;
    return {{x/100.0F,0.0F,static_cast<float>(z)/100.0F}};
}

std::vector<SceneVertex> support_overlay_vertices(
    const OverlayDescriptor& descriptor,std::size_t frame_index,
    std::uint32_t image_width,std::uint32_t image_height,
    const std::array<float,16>& matrix,
    std::uint8_t tile_x,std::uint8_t tile_y,
    float drawable_width,float drawable_height) {
    if(!std::isfinite(drawable_width) || !std::isfinite(drawable_height) ||
       drawable_width<=0 || drawable_height<=0 || drawable_width>16384 || drawable_height>16384 ||
       image_width==0 || image_height==0 || image_width>256 || image_height>256)
        throw FormatError("invalid support overlay viewport or image size");
    for(const float value:matrix)
        if(!std::isfinite(value))throw FormatError("nonfinite support projection");
    if(frame_index>=descriptor.frames().size())throw FormatError("missing support overlay frame");
    const auto anchor=support_world_anchor(tile_x,tile_y);
    const std::array<float,4> point{{anchor[0],anchor[1],anchor[2],1}};
    std::array<float,4> clip{};
    for(std::size_t row=0;row<4;++row)
        for(std::size_t column=0;column<4;++column)
            clip[row]+=matrix[column*4+row]*point[column];
    for(const float value:clip)
        if(!std::isfinite(value))throw FormatError("support projection overflow");
    // No 2D draw for a point behind the current camera.
    if(clip[3]<=0)return {};
    constexpr float logical_height=320.0F;
    const float logical_width=drawable_width*logical_height/drawable_height;
    const float anchor_x=std::trunc((clip[0]/clip[3]*0.5F+0.5F)*logical_width);
    const float anchor_y=std::trunc((0.5F-clip[1]/clip[3]*0.5F)*logical_height);
    if(!std::isfinite(anchor_x) || !std::isfinite(anchor_y))
        throw FormatError("support screen anchor overflow");
    std::vector<SceneVertex> output;
    const auto& parts=descriptor.frames()[frame_index].parts;
    output.reserve(parts.size()*6);
    // Original 0x5e3aa..0x5e402 visits parts from last to first.
    for(auto it=parts.rbegin();it!=parts.rend();++it) {
        const auto& variant=descriptor.variants().at(it->variant_index);
        if(variant.orientation!=0)throw FormatError("unsupported support overlay transform");
        const auto& crop=descriptor.rectangles().at(variant.rectangle_index);
        if(crop.x<0 || crop.y<0 || crop.width<=0 || crop.height<=0 ||
           static_cast<std::uint32_t>(crop.x+crop.width)>image_width ||
           static_cast<std::uint32_t>(crop.y+crop.height)>image_height)
            throw FormatError("support crop does not match uploaded image");
        const float x=anchor_x+static_cast<float>(it->offset_x);
        const float y=anchor_y+static_cast<float>(it->offset_y);
        const float left=x*2.0F/logical_width-1.0F;
        const float right=(x+crop.width)*2.0F/logical_width-1.0F;
        const float top=1.0F-y*2.0F/logical_height;
        const float bottom=1.0F-(y+crop.height)*2.0F/logical_height;
        const float u0=static_cast<float>(crop.x)/static_cast<float>(image_width);
        const float v0=static_cast<float>(crop.y)/static_cast<float>(image_height);
        const float u1=static_cast<float>(crop.x+crop.width)/static_cast<float>(image_width);
        const float v1=static_cast<float>(crop.y+crop.height)/static_cast<float>(image_height);
        const std::array<SceneVertex,4> vertices{{
            {{left,top,0,1},{u0,v0},{}},{{left,bottom,0,1},{u0,v1},{}},
            {{right,top,0,1},{u1,v0},{}},{{right,bottom,0,1},{u1,v1},{}}}};
        for(const auto index:sprite_triangle_indices)output.push_back(vertices[index]);
    }
    return output;
}

} // namespace g1
