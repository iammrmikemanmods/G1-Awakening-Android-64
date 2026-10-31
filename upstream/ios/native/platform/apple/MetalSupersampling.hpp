#pragma once
#import <Metal/Metal.h>
#include "g1/PresentationQuality.hpp"
#include <memory>
#include <stdexcept>

namespace g1 {
// True single-sample higher-resolution render targets plus a downsample pass.
// UIKit is drawn separately at native display resolution. No simulation ticks,
// random values, presentation acknowledgements or save state live here.
class MetalSupersampling {
    id<MTLDevice> device_;
    id<MTLRenderPipelineState> resolve_;
    id<MTLTexture> color_,depth_;
    bool active_{},fresh_{};
    std::uint32_t width_{},height_{};
    std::uint64_t allocation_fallbacks_{};
public:
    explicit MetalSupersampling(id<MTLDevice> device):device_(device) {
        if(!device_)throw std::runtime_error("Supersampling has no Metal device");
        const char* source=R"METAL(
#include <metal_stdlib>
using namespace metal;
struct G1ResolveOut {float4 position [[position]];float2 uv;};
vertex G1ResolveOut g1_resolve_vertex(uint i [[vertex_id]]) {
    const float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};
    G1ResolveOut o;o.position=float4(p[i],0,1);o.uv=float2((p[i].x+1)*0.5,(1-p[i].y)*0.5);return o;
}
fragment float4 g1_resolve_fragment(G1ResolveOut p [[stage_in]],texture2d<float> t [[texture(0)]],
                                   constant float2& footprint [[buffer(0)]]) {
    constexpr sampler s(coord::normalized,address::clamp_to_edge,filter::linear);
    float2 d=footprint*0.25;
    return (t.sample(s,p.uv+float2(-d.x,-d.y))+t.sample(s,p.uv+float2(d.x,-d.y))+
            t.sample(s,p.uv+float2(-d.x,d.y))+t.sample(s,p.uv+float2(d.x,d.y)))*0.25;
}
)METAL";
        NSError* error=nil;
        id<MTLLibrary> library=[device_ newLibraryWithSource:[NSString stringWithUTF8String:source] options:nil error:&error];
        if(!library)throw std::runtime_error("Supersampling resolve shader failed");
        MTLRenderPipelineDescriptor* descriptor=[MTLRenderPipelineDescriptor new];
        descriptor.label=@"G1 real supersampling resolve";
        descriptor.vertexFunction=[library newFunctionWithName:@"g1_resolve_vertex"];
        descriptor.fragmentFunction=[library newFunctionWithName:@"g1_resolve_fragment"];
        descriptor.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
        resolve_=[device_ newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if(!resolve_)throw std::runtime_error("Supersampling resolve pipeline failed");
    }
    void release_targets() noexcept {color_=nil;depth_=nil;width_=height_=0;active_=false;}
    bool active() const noexcept{return active_;}
    std::uint64_t allocation_fallbacks() const noexcept{return allocation_fallbacks_;}
    MTLRenderPassDescriptor* begin(MTLRenderPassDescriptor* native,const quality::Plan& requested) {
        active_=false;fresh_=false;
        if(!requested.supersampling){release_targets();return native;}
        if(!color_||width_!=requested.width||height_!=requested.height){
            MTLTextureDescriptor* descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
                MTLPixelFormatBGRA8Unorm width:requested.width height:requested.height mipmapped:NO];
            descriptor.storageMode=MTLStorageModePrivate;
            descriptor.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
            id<MTLTexture> next=[device_ newTextureWithDescriptor:descriptor];
            descriptor.pixelFormat=MTLPixelFormatDepth32Float;descriptor.usage=MTLTextureUsageRenderTarget;
            id<MTLTexture> nextDepth=[device_ newTextureWithDescriptor:descriptor];
            if(!next||!nextDepth){release_targets();++allocation_fallbacks_;return native;}
            color_=next;depth_=nextDepth;width_=requested.width;height_=requested.height;fresh_=true;
        }
        MTLRenderPassDescriptor* pass=[native copy];
        pass.colorAttachments[0].texture=color_;pass.colorAttachments[0].resolveTexture=nil;
        pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        pass.depthAttachment.texture=depth_;pass.depthAttachment.resolveTexture=nil;
        pass.depthAttachment.storeAction=MTLStoreActionStore;
        active_=true;return pass;
    }
    void sanitize_new_target(MTLRenderPassDescriptor* pass) const {
        if(!active_||!fresh_)return;
        if(pass.colorAttachments[0].loadAction==MTLLoadActionLoad)
            pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        if(pass.depthAttachment.loadAction==MTLLoadActionLoad)
            pass.depthAttachment.loadAction=MTLLoadActionClear;
    }
    void resolve(id<MTLCommandBuffer> command,id<MTLTexture> destination) {
        if(!active_)return;
        MTLRenderPassDescriptor* pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=destination;
        pass.colorAttachments[0].loadAction=MTLLoadActionDontCare;
        pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        id<MTLRenderCommandEncoder> encoder=[command renderCommandEncoderWithDescriptor:pass];
        if(!encoder)throw std::runtime_error("Supersampling resolve encoder unavailable");
        [encoder setRenderPipelineState:resolve_];
        [encoder setFragmentTexture:color_ atIndex:0];
        const float footprint[2]={1.0F/destination.width,1.0F/destination.height};
        [encoder setFragmentBytes:footprint length:sizeof(footprint) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
    }
};
} // namespace g1
