#include "g1/SupportGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {

std::size_t checks{};

void require(bool condition,const char* message) {
    ++checks;
    if(!condition)throw std::runtime_error(message);
}

void near(double actual,double expected,double tolerance,const char* message) {
    require(std::isfinite(actual) && std::abs(actual-expected)<=tolerance,message);
}

g1::OverlayDescriptor descriptor() {
    // An independent two-crop v1 fixture. Frame 0 lists crop A then crop B;
    // the original painter order must draw B first. Frame 1 is a blink frame.
    const g1::Bytes bytes{
        1,0,0,2,0,                 // Version, narrow coordinates, two crops, no extra image.
        8,4,16,8,                  // A: [8,24) x [4,12).
        32,16,8,4,                 // B: [32,40) x [16,20).
        0,2,0,0,1,0,               // Two identity variants.
        0,2,                       // Two frames.
        2,0,251,3,1,7,247,0,       // A at (-5,+3), B at (+7,-9), no explicit bounds.
        0,0,                       // Empty frame, no explicit bounds.
        0,1,2,0,0,30,1,0,30        // No extra points; one two-step animation.
    };
    return g1::OverlayDescriptor(bytes,64,32);
}

std::array<float,16> projected_anchor(float x,float y,float w=1.0F) {
    // Deliberately constant clip position, independent of tile/world math.
    // This isolates 2D crop placement and viewport scaling.
    std::array<float,16> matrix{};
    matrix[12]=x;matrix[13]=y;matrix[15]=w;
    return matrix;
}

void verify_part(const std::vector<g1::SceneVertex>& vertices,std::size_t start,
                 float viewport_width,float viewport_height,
                 const std::array<double,4>& pixel_bounds,
                 const std::array<double,4>& uv_bounds) {
    require(vertices.size()>=start+6,"support part lacks two triangles");
    double min_x=std::numeric_limits<double>::infinity();
    double max_x=-min_x,min_y=min_x,max_y=-min_x;
    double min_u=min_x,max_u=-min_x,min_v=min_x,max_v=-min_x;
    for(std::size_t i=start;i<start+6;++i) {
        const auto& v=vertices[i];
        const double x=(double(v.position[0])+1.0)*viewport_width/2.0;
        const double y=(1.0-double(v.position[1]))*viewport_height/2.0;
        min_x=std::min(min_x,x);max_x=std::max(max_x,x);
        min_y=std::min(min_y,y);max_y=std::max(max_y,y);
        min_u=std::min(min_u,double(v.uv[0]));max_u=std::max(max_u,double(v.uv[0]));
        min_v=std::min(min_v,double(v.uv[1]));max_v=std::max(max_v,double(v.uv[1]));
        require(v.position[2]==0 && v.position[3]==1,"screen overlay has unintended depth or homogeneous weight");
    }
    const std::array<double,4> actual_pixels{{min_x,min_y,max_x,max_y}};
    const std::array<double,4> actual_uv{{min_u,min_v,max_u,max_v}};
    for(std::size_t i=0;i<4;++i) {
        near(actual_pixels[i],pixel_bounds[i],0.0003,"support drawable pixel bounds differ");
        near(actual_uv[i],uv_bounds[i],0.000001,"support exact crop UV differs (origin must not add one pixel)");
    }
    for(std::size_t i=start;i<start+6;i+=3) {
        const auto& a=vertices[i].position;
        const auto& b=vertices[i+1].position;
        const auto& c=vertices[i+2].position;
        const float cross=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
        require(cross>0,"support part lost consistent front-facing winding");
    }
}

template<class F>void reject(F function) {
    bool rejected=false;
    try{function();}catch(const g1::FormatError&){rejected=true;}
    require(rejected,"invalid support geometry input was accepted");
}

} // namespace

int main() {
    try {
        const auto asset=descriptor();
        const auto matrix=projected_anchor(0.003F,0.007F);
        // Original-sized logical viewport: projected (240.72,158.88) truncates
        // to (240,158). Neither rounding nor preserving fractions is allowed.
        const auto original=g1::support_overlay_vertices(asset,0,64,32,matrix,0,0,480,320);
        require(original.size()==12,"support reverse-order fixture did not emit two parts");
        verify_part(original,0,480,320,{{247,149,255,153}},{{0.5,0.5,0.625,0.625}});
        verify_part(original,6,480,320,{{235,161,251,169}},{{0.125,0.125,0.375,0.375}});
        // XR policy: logical height stays 320 and both axes scale by 828/320.
        // Widescreen logical anchor x truncates to 347; y still truncates158.
        // Expected bounds below are precomputed in actual drawable pixels.
        const auto xr=g1::support_overlay_vertices(asset,0,64,32,matrix,0,0,1792,828);
        verify_part(xr,0,1792,828,{{915.975,385.5375,936.675,395.8875}},
                    {{0.5,0.5,0.625,0.625}});
        verify_part(xr,6,1792,828,{{884.925,416.5875,926.325,437.2875}},
                    {{0.125,0.125,0.375,0.375}});
        require(g1::support_overlay_vertices(asset,1,64,32,matrix,0,0,1792,828).empty(),
                "empty support blink frame emitted geometry");

        const auto zero=g1::support_world_anchor(0,0);
        near(zero[0],0.032,0.0000001,"support anchor lost original +0.1 tile x offset");
        require(zero[1]==0,"support anchor is not on ground plane");
        near(zero[2],0.32,0.0000001,"support anchor lost original next-tile z boundary");
        const auto tile=g1::support_world_anchor(3,2);
        near(tile[0],0.992,0.0000002,"support world x differs");
        near(tile[2],0.96,0.0000001,"support world z differs");
        const auto edge=g1::support_world_anchor(255,255);
        near(edge[0],81.632,0.00001,"support x overflowed byte-sized boundary");
        near(edge[2],81.92,0.00001,"support next-tile z overflowed byte-sized boundary");

        std::array<float,16> world_projection{};
        world_projection[0]=0.125F;world_projection[9]=-0.5F;world_projection[15]=1;
        const auto projected=g1::support_overlay_vertices(asset,0,64,32,world_projection,3,2,480,320);
        // World anchor (.992,0,.96) projects to (269.76,236.8) logical pixels.
        verify_part(projected,0,480,320,{{276,227,284,231}},{{0.5,0.5,0.625,0.625}});
        verify_part(projected,6,480,320,{{264,239,280,247}},{{0.125,0.125,0.375,0.375}});

        // Negative screen coordinates truncate toward zero, not floor. Leave
        // offscreen triangles for GPU clipping instead of shifting the sprite.
        const auto negative=g1::support_overlay_vertices(asset,0,64,32,
            projected_anchor(-1.001F,1.002F),0,0,480,320);
        verify_part(negative,0,480,320,{{7,-9,15,-5}},{{0.5,0.5,0.625,0.625}});
        verify_part(negative,6,480,320,{{-5,3,11,11}},{{0.125,0.125,0.375,0.375}});
        for(const float w:{0.0F,-1.0F})
            require(g1::support_overlay_vertices(asset,0,64,32,projected_anchor(0,0,w),0,0,480,320).empty(),
                    "support point behind camera emitted geometry");

        for(const float bad:{0.0F,-1.0F,16385.0F,std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()}) {
            reject([&]{(void)g1::support_overlay_vertices(asset,0,64,32,matrix,0,0,bad,320);});
            reject([&]{(void)g1::support_overlay_vertices(asset,0,64,32,matrix,0,0,480,bad);});
        }
        reject([&]{(void)g1::support_overlay_vertices(asset,2,64,32,matrix,0,0,480,320);});
        for(const std::uint32_t width:{0U,39U,257U})
            reject([&]{(void)g1::support_overlay_vertices(asset,0,width,32,matrix,0,0,480,320);});
        for(const std::uint32_t height:{0U,19U,257U})
            reject([&]{(void)g1::support_overlay_vertices(asset,0,64,height,matrix,0,0,480,320);});
        auto invalid=matrix;invalid[0]=std::numeric_limits<float>::quiet_NaN();
        reject([&]{(void)g1::support_overlay_vertices(asset,0,64,32,invalid,0,0,480,320);});
        invalid={};invalid[0]=std::numeric_limits<float>::max();invalid[15]=1;
        reject([&]{(void)g1::support_overlay_vertices(asset,0,64,32,invalid,255,255,480,320);});
        std::cout<<"{\"support_geometry_checks\":"<<checks
            <<",\"synthetic_descriptor\":true,\"physical_device_test\":false,\"passed\":true}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"support geometry failed: "<<error.what()<<'\n';return 1;}
}
