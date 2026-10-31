#include "MetalSupersampling.hpp"
#include <iostream>
#include <array>
int main(){@autoreleasepool{try{
 id<MTLDevice> device=MTLCreateSystemDefaultDevice();
 if(!device){std::cout<<"{\"skipped\":\"no Metal device\"}\n";return 77;}
 g1::MetalSupersampling ssaa(device);
 auto plan=g1::quality::plan(32,24,g1::quality::Mode::modern);
 MTLTextureDescriptor* d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
    width:32 height:24 mipmapped:NO];
 d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageRenderTarget;
 id<MTLTexture> output=[device newTextureWithDescriptor:d];
 MTLRenderPassDescriptor* native=[MTLRenderPassDescriptor renderPassDescriptor];
 native.colorAttachments[0].texture=output;native.colorAttachments[0].storeAction=MTLStoreActionStore;
 auto pass=ssaa.begin(native,plan);
 if(!ssaa.active()||pass.colorAttachments[0].texture.width!=48||pass.colorAttachments[0].texture.height!=36)
    throw std::runtime_error("Real higher-resolution targets were not allocated");
 pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].clearColor=MTLClearColorMake(1,0.25,0,1);
 pass.depthAttachment.loadAction=MTLLoadActionClear;pass.depthAttachment.clearDepth=1;
 id<MTLCommandQueue> queue=[device newCommandQueue];id<MTLCommandBuffer> buffer=[queue commandBuffer];
 id<MTLRenderCommandEncoder> e=[buffer renderCommandEncoderWithDescriptor:pass];[e endEncoding];
 ssaa.resolve(buffer,output);[buffer commit];[buffer waitUntilCompleted];
 if(buffer.status!=MTLCommandBufferStatusCompleted)throw std::runtime_error("SSAA GPU command failed");
 std::array<unsigned char,4> pixel{};
 [output getBytes:pixel.data() bytesPerRow:4 fromRegion:MTLRegionMake2D(16,12,1,1) mipmapLevel:0];
 if(pixel[0]!=0||pixel[1]<63||pixel[1]>65||pixel[2]!=255||pixel[3]!=255)
    throw std::runtime_error("Resolved pixel mismatch");
 auto classic=g1::quality::plan(32,24,g1::quality::Mode::classic);
 if(ssaa.begin(native,classic)!=native||ssaa.active())throw std::runtime_error("Classic fallback failed");
 std::cout<<"{\"supersampling_target\":[48,36],\"output\":[32,24],\"resolve_pixel_passed\":true,\"classic_bypass_passed\":true,\"original_game_data_used\":false,\"iphone_execution\":false}\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}}
