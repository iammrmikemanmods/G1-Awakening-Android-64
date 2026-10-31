#pragma once
#include "g1/M3GScenePose.hpp"
#include "g1/SpriteAnimation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace g1 {
// Authored native presentation policy, NOT recovered original animation.
// The original sprite player remains the action clock and completion owner.
struct TacticalMotion {
    bool walking{false}, transforming{false};
    float stride_phase{0};                 // cycles, position-derived
    float vehicle_fraction{0};             // robot=0, vehicle=1, reversible
};
inline float smooth_phase(float a,float b,float x) {
    const float t=std::clamp((x-a)/(b-a),0.0F,1.0F);return t*t*(3-2*t);
}
inline float transformation_fraction(const std::vector<AtlasStep>& steps,
                                      const SpriteAnimationState& state) {
    if(steps.empty()||state.step_index>=steps.size()||state.elapsed_ms<0)
        throw FormatError("Invalid transformation animation clock");
    if(state.completed)return state.reverse?0.0F:1.0F;
    double total=0,before=0;
    for(std::size_t i=0;i<steps.size();++i) {
        if(!steps[i].duration_units)throw FormatError("Zero transformation step duration");
        if(i<state.step_index)before+=steps[i].duration_units;
        total+=steps[i].duration_units;
    }
    return float(std::clamp((before+std::min<double>(state.elapsed_ms,
        steps[state.step_index].duration_units))/total,0.0,1.0));
}
inline float tactical_stride(float x,float z,std::uint8_t facing) {
    if(facing>3||!std::isfinite(x)||!std::isfinite(z))throw FormatError("Invalid stride coordinate");
    const double axis=facing<2?z:x;
    const double cycles=axis/0.32-0.5;
    return float(cycles-std::floor(cycles));
}
inline M3GMatrix motion_identity(){return {{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};}
inline M3GMatrix rigid_hinge(std::array<float,3> pivot,float radians,int axis=0) {
    if(axis<0||axis>2||!std::isfinite(radians))throw FormatError("Invalid rigid hinge");
    for(auto p:pivot)if(!std::isfinite(p))throw FormatError("Nonfinite hinge pivot");
    auto r=motion_identity();const float c=std::cos(radians),s=std::sin(radians);
    const int a=(axis+1)%3,b=(axis+2)%3;
    r[a*4+a]=c;r[b*4+b]=c;r[a*4+b]=s;r[b*4+a]=-s;
    for(int i=0;i<3;++i)r[12+i]=pivot[i]-(r[i]*pivot[0]+r[4+i]*pivot[1]+r[8+i]*pivot[2]);
    return r;
}
struct MechanicalPhase {
    float robot_fold{},vehicle_release{},exchange{},highlight{};
};
inline MechanicalPhase mechanical_phase(float vehicle_fraction) {
    if(!std::isfinite(vehicle_fraction)||vehicle_fraction<0||vehicle_fraction>1)
        throw FormatError("Transformation phase outside original action clock");
    const float p=vehicle_fraction;
    return {smooth_phase(0.04F,0.52F,p),1-smooth_phase(0.48F,0.96F,p),
        smooth_phase(0.46F,0.54F,p),std::min(0.35F,float(std::pow(std::sin(3.14159265359F*p),8))*0.35F)};
}
// Keep native clip metadata in tests; this is a stable screen-space coverage
// exchange rather than transparent ghosting or collapsing/scaling vertices.
inline bool mechanical_coverage(bool vehicle,float mix,unsigned x,unsigned y) noexcept {
    constexpr unsigned bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    const float cut=(bayer[(y&3)*4+(x&3)]+0.5F)/16.0F;
    return vehicle?cut<mix:cut>=mix;
}
} // namespace g1
