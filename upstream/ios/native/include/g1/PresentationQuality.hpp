#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace g1::quality {
enum class Mode {classic,modern};
struct Capability {
    std::uint64_t memory_bytes{3ULL*1024*1024*1024};
    std::uint32_t maximum_dimension{8192};
    bool low_power{},thermal_serious{},memory_pressure{};
};
struct Plan {
    std::uint32_t width{},height{};
    double scale{1};
    bool supersampling{},soft_contact_shadows{};
    // UI enhancement is always enabled, independent of world texture mode.
    bool enhanced_ui{true};
    std::uint64_t estimated_surface_bytes{};
};
inline Plan plan(std::uint32_t width,std::uint32_t height,Mode mode,Capability c={}) {
    if(width==0||height==0||width>32768||height>32768||c.maximum_dimension<64)
        throw std::invalid_argument("Invalid render output size/capability");
    Plan p{};p.width=width;p.height=height;
    if(mode==Mode::classic)return p;
    p.soft_contact_shadows=true;
    if(c.memory_pressure||c.thermal_serious||c.low_power)return p;
    const double budget=c.memory_bytes<2ULL*1024*1024*1024?32.0*1024*1024:
        c.memory_bytes<4ULL*1024*1024*1024?96.0*1024*1024:192.0*1024*1024;
    // Conservative three-frame color+depth budget; default 1.5 per axis is
    // 2.25 shaded pixels per display pixel, NOT an MSAA sample-count claim.
    double scale=std::min({1.5,std::sqrt(budget/(24.0*width*height)),
        double(c.maximum_dimension)/width,double(c.maximum_dimension)/height});
    if(scale<1.1)return p;
    p.width=std::max(width,static_cast<std::uint32_t>(std::floor(width*scale)));
    p.height=std::max(height,static_cast<std::uint32_t>(std::floor(height*scale)));
    p.scale=std::min(double(p.width)/width,double(p.height)/height);
    p.supersampling=p.width>width&&p.height>height;
    p.estimated_surface_bytes=std::uint64_t(p.width)*p.height*24;
    return p;
}
} // namespace g1::quality
