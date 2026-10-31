#pragma once
#include "g1/Terrain.hpp"
#include <set>
#include <tuple>

namespace g1::visual_border {
struct Placement {
    std::int32_t x{},y{};
    std::uint8_t tile{};
};
struct Plan {
    std::uint16_t original_width{},original_height{};
    std::uint8_t style{};
    std::uint32_t original_grid_resource{};
    std::vector<Placement> decorative;
    bool approved{};
};
// Decorative data has no MovementBoard/GameSession reference or API. This
// validator rejects any placement that would cover a playable map cell.
inline void validate(const Plan& plan,const TerrainGrid& original,std::uint8_t style,
                     std::uint32_t grid_resource) {
    if(plan.original_width!=original.width||plan.original_height!=original.height||
       plan.style!=style||plan.original_grid_resource!=grid_resource)
        throw FormatError("Visual extension belongs to a different mission map");
    if(plan.decorative.size()>32768)throw FormatError("Unbounded decorative map extension");
    std::set<std::pair<std::int32_t,std::int32_t>> occupied;
    std::set<std::uint8_t> palette(original.tiles.begin(),original.tiles.end());
    for(const auto& p:plan.decorative) {
        if(p.x>=0&&p.y>=0&&p.x<original.width&&p.y<original.height)
            throw FormatError("Visual extension overlaps original gameplay boundary");
        if(p.x < -32||p.y < -32||p.x>=std::int32_t(original.width)+32||p.y>=std::int32_t(original.height)+32)
            throw FormatError("Visual extension exceeds bounded decorative ring");
        if(p.tile>=32||!palette.count(p.tile))throw FormatError("Visual extension imports a tile not present in this map");
        if(!occupied.emplace(p.x,p.y).second)throw FormatError("Duplicate decorative map cell");
    }
}
struct Beam {float ax{},az{},bx{},bz{};};
inline std::vector<Beam> boundary(std::uint16_t width,std::uint16_t height) {
    if(!width||!height||width>128||height>128)throw FormatError("Invalid Energon boundary footprint");
    const float w=width*.32F,h=height*.32F;
    return {{0,0,w,0},{w,0,w,h},{w,h,0,h},{0,h,0,0}};
}
} // namespace g1::visual_border
