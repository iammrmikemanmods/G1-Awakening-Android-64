#pragma once
#include "g1/M3GRenderFrame.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
namespace g1 {
// A presentation-only anchor derived from the actual posed draw matrices.
// Never infer a combat result or use the global gameplay random generator.
inline std::optional<std::array<float,2>> projected_actor_top(const M3GPreparedFrame& frame) {
    float xmin=1,ymax=-1,xmax=-1;bool found=false;
    for(const auto& draw:frame.draws) {
        if(draw.kind!=M3GDrawableKind::mesh || draw.effective_alpha<=0 || draw.instance>=frame.assets.size())continue;
        const auto* asset=frame.assets[draw.instance];
        if(!asset || draw.geometry_index>=asset->geometry.surfaces.size())continue;
        for(const auto& vertex:asset->geometry.surfaces[draw.geometry_index].vertices) {
            std::array<float,4> clip{};
            for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)clip[r]+=draw.metal_matrix[c*4+r]*vertex.position[c];
            if(!std::isfinite(clip[3]) || clip[3]<=0.0001F)continue;
            const float x=clip[0]/clip[3],y=clip[1]/clip[3],z=clip[2]/clip[3];
            if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||z<0||z>1)continue;
            xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymax=std::max(ymax,y);found=true;
        }
    }
    if(!found || xmax < -1 || xmin > 1 || ymax < -1)return std::nullopt;
    return std::array<float,2>{{std::clamp((xmin+xmax)*.25F+.5F,.08F,.92F),
                                std::clamp(.5F-ymax*.5F,.12F,.70F)}};
}
} // namespace g1
