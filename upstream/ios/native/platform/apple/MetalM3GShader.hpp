#pragma once

// One source of truth for runtime compilation and the cloud metal-compiler
// gate. analysis_tools/m3g/extract_metal_shader.py extracts this exact body.
namespace g1 {
inline constexpr const char* m3g_metal_shader=R"G1M3G(
#include <metal_stdlib>
using namespace metal;

struct G1M3GVertex {
    float4 position;
    float4 normal;
    float4 color;
    float4 texcoord;
};
struct G1M3GUniform {
    float4x4 matrix;
    float4 blend; // RGB texture blend color, alpha-test threshold.
    float4 state; // Node alpha, assembly role, coverage exchange, subtle highlight.
    uint4 flags;  // Texture function, source format, enabled, flat shading.
};
struct G1M3GOut {
    float4 position [[position]];
    float4 color;
    float4 texcoord;
};

vertex G1M3GOut g1_m3g_vertex(uint index [[vertex_id]],
    const device G1M3GVertex* vertices [[buffer(0)]],
    constant G1M3GUniform& uniform [[buffer(1)]]) {
    G1M3GOut out;
    out.position=uniform.matrix*vertices[index].position;
    // JSR184 flat color comes from the third vertex. Expanded strip triangles
    // retain that vertex even when their first two indices swap for winding.
    const uint color_index=uniform.flags.w!=0 ? index-index%3+2 : index;
    out.color=vertices[color_index].color;
    out.color.a*=uniform.state.x; // Node alpha is applied BEFORE texturing.
    out.texcoord=vertices[index].texcoord;
    return out;
}

fragment float4 g1_m3g_fragment(G1M3GOut in [[stage_in]],
    constant G1M3GUniform& uniform [[buffer(0)]],
    texture2d<float> image [[texture(0)]],sampler image_sampler [[sampler(0)]]) {
    if(uniform.state.y!=0) {
        constexpr uint bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
        const uint2 p=uint2(in.position.xy);
        const float cut=(float(bayer[(p.y&3)*4+(p.x&3)])+0.5f)/16.0f;
        const bool vehicle=uniform.state.y>1.5f;
        if(vehicle ? cut>=uniform.state.z : cut<uniform.state.z)discard_fragment();
    }
    float4 color=in.color;
    if(uniform.flags.z!=0) {
        const float4 tex=image.sample(image_sampler,in.texcoord.xy/in.texcoord.w);
        const uint format=uniform.flags.y;
        const bool rgb=format!=96; // ALPHA has no color contribution.
        const bool alpha=format==96 || format==98 || format==100;
        switch(uniform.flags.x) {
        case 224: // BLEND
            if(rgb)color.rgb=color.rgb*(1.0f-tex.rgb)+uniform.blend.rgb*tex.rgb;
            if(alpha)color.a*=tex.a;
            break;
        case 225: // DECAL is defined only for RGB and RGBA (checked on CPU).
            color.rgb=format==99 ? tex.rgb : mix(color.rgb,tex.rgb,tex.a);
            break;
        case 226: // ADD
            if(rgb)color.rgb+=tex.rgb;
            if(alpha)color.a*=tex.a;
            break;
        case 227: // MODULATE
            if(rgb)color.rgb*=tex.rgb;
            if(alpha)color.a*=tex.a;
            break;
        case 228: // REPLACE
            if(rgb)color.rgb=tex.rgb;
            if(alpha)color.a=tex.a;
            break;
        }
    }
    if(uniform.state.y!=0)color.rgb=mix(color.rgb,float3(0.65f,0.88f,1.0f),uniform.state.w);
    color=clamp(color,0.0f,1.0f);
    // Original compositing uses GEQUAL, so equality is retained.
    if(color.a<uniform.blend.a)discard_fragment();
    return color;
}
)G1M3G";
} // namespace g1
