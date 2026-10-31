#include "MetalM3GRenderer.hpp"
#include "MetalRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace {
constexpr g1::M3GMatrix identity{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
std::size_t checks{};

g1::M3GSurface quad(std::size_t id,float half,float depth,std::array<float,4> color) {
    g1::M3GSurface out;out.mesh_object=id;out.appearance_object=id;out.model=identity;
    out.appearance.culling=162;
    const std::array<g1::M3GVertex,4> vertices{{
        {{{-half,half,depth,1}},{},color,{{0,0,0,1}}},
        {{{-half,-half,depth,1}},{},color,{{0,1,0,1}}},
        {{{half,half,depth,1}},{},color,{{1,0,0,1}}},
        {{{half,-half,depth,1}},{},color,{{1,1,0,1}}}}};
    for(const auto index:{0,1,2,2,1,3})out.vertices.push_back(vertices[index]);
    return out;
}

g1::M3GRenderAsset mesh_asset(std::vector<g1::M3GSurface> surfaces) {
    g1::M3GRenderAsset asset;asset.geometry.surfaces=std::move(surfaces);
    asset.geometry.has_geometry=!asset.geometry.surfaces.empty();
    for(std::size_t i=0;i<asset.geometry.surfaces.size();++i)
        asset.traversal.push_back({g1::M3GDrawableKind::mesh,i});
    return asset;
}

g1::RgbaImage render(id<MTLDevice> device,g1::MetalM3GRenderer& renderer,
                    const g1::M3GRenderAsset& asset,float effective_alpha=1,bool modern=false,bool shared=false,float assembly_mix=-1) {
    auto gpu=modern?renderer.upload_modern(asset,g1::MaterialEnhancement::environment):renderer.upload(asset);
    if(modern) {
        if(!gpu->enhanced_texture_count())throw std::runtime_error("Modern GPU material upload did not run");
        for(const auto& [id,image]:asset.geometry.images)
            if(gpu->asset().geometry.images.at(id).width!=image.width ||
               gpu->asset().geometry.images.at(id).pixels!=image.pixels)
                throw std::runtime_error("Modern enhancement changed source/crop identity");
    }
    std::map<std::size_t,g1::M3GEvaluatedNode> nodes;
    std::map<std::size_t,std::int32_t> grouping;
    for(const auto& surface:asset.geometry.surfaces) {
        nodes.emplace(surface.mesh_object,g1::M3GEvaluatedNode{surface.model,effective_alpha,true,0xffffffffU});
        grouping.emplace(surface.appearance_object,static_cast<std::int32_t>(surface.appearance_object));
    }
    for(const auto& sprite:asset.sprites) {
        nodes.emplace(sprite.node_object,g1::M3GEvaluatedNode{sprite.model,effective_alpha,true,0xffffffffU});
        grouping.emplace(sprite.appearance_object,static_cast<std::int32_t>(sprite.appearance_object));
    }
    g1::M3GRenderInstance instance;instance.asset=&gpu->asset();instance.nodes=&nodes;
    instance.mesh_appearance_serials=&grouping;instance.sprite_appearance_identities=&grouping;
    g1::M3GRenderView view;view.viewport_pixels={{64,64}};
    auto frame=g1::prepare_m3g_frame({instance},view);
    if(assembly_mix>=0)for(auto& d:frame.draws){d.assembly_role=d.node_object==1?1:2;d.assembly_mix=assembly_mix;}

    MTLTextureDescriptor* colorDesc=[MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:64 height:64 mipmapped:NO];
    colorDesc.storageMode=MTLStorageModeShared;colorDesc.usage=MTLTextureUsageRenderTarget;
    id<MTLTexture> color=[device newTextureWithDescriptor:colorDesc];
    MTLTextureDescriptor* depthDesc=[MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:64 height:64 mipmapped:NO];
    depthDesc.storageMode=MTLStorageModePrivate;depthDesc.usage=MTLTextureUsageRenderTarget;
    id<MTLTexture> depth=[device newTextureWithDescriptor:depthDesc];
    if(!color || !depth)throw std::runtime_error("M3G offscreen attachment allocation failed");
    MTLRenderPassDescriptor* pass=[MTLRenderPassDescriptor new];
    pass.colorAttachments[0].texture=color;pass.colorAttachments[0].loadAction=MTLLoadActionClear;
    pass.colorAttachments[0].storeAction=MTLStoreActionStore;
    pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
    pass.depthAttachment.texture=depth;pass.depthAttachment.loadAction=MTLLoadActionClear;
    pass.depthAttachment.storeAction=MTLStoreActionDontCare;pass.depthAttachment.clearDepth=1;
    id<MTLCommandQueue> queue=[device newCommandQueue];id<MTLCommandBuffer> command=[queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder=[command renderCommandEncoderWithDescriptor:pass];
    if(!queue || !command || !encoder)throw std::runtime_error("M3G offscreen encoder allocation failed");
    const MTLViewport viewport{0,0,64,64,0,1};[encoder setViewport:viewport];
    std::unique_ptr<g1::MetalRenderer> terrain;
    std::shared_ptr<g1::MetalMesh> terrainMesh;
    id<MTLTexture> terrainTexture=nil;
    if(shared) {
        terrain=std::make_unique<g1::MetalRenderer>(device);
        std::vector<g1::SceneVertex> vertices;
        const std::array<g1::SceneVertex,4> corners{{
            {{{-1,1,0.25F,1}},{{0,0}}},{{{-1,-1,0.25F,1}},{{0,1}}},
            {{{0,1,0.25F,1}},{{1,0}}},{{{0,-1,0.25F,1}},{{1,1}}}}};
        for(auto index:{0,1,2,2,1,3})vertices.push_back(corners[index]);
        terrainMesh=terrain->mesh(vertices);terrainTexture=terrain->texture({1,1,{0,255,0,255}});
        terrain->encode_into(encoder,{{terrainMesh,terrainTexture,identity,{{1,1,1,1}},true}});
    }
    renderer.encode_frame(encoder,{gpu},frame);[encoder endEncoding];
    [command commit];[command waitUntilCompleted];
    if(command.status!=MTLCommandBufferStatusCompleted)
        throw std::runtime_error(command.error.localizedDescription.UTF8String ?: "M3G Metal command failed");
    g1::RgbaImage image{64,64,g1::Bytes(64*64*4)};
    [color getBytes:image.pixels.data() bytesPerRow:64*4 fromRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0];
    for(std::size_t i=0;i<image.pixels.size();i+=4)std::swap(image.pixels[i],image.pixels[i+2]);
    return image;
}

void pixel(const g1::RgbaImage& image,unsigned x,unsigned y,std::array<int,4> expected,const char* label) {
    ++checks;
    for(std::size_t c=0;c<4;++c) {
        const int actual=image.pixels[(std::size_t(y)*image.width+x)*4+c];
        if(std::abs(actual-expected[c])>2)
            throw std::runtime_error(std::string(label)+" channel "+std::to_string(c)+
                " expected "+std::to_string(expected[c])+" observed "+std::to_string(actual));
    }
}
} // namespace

int main(int argc,char** argv) {
    @autoreleasepool {
        try {
            id<MTLDevice> device=MTLCreateSystemDefaultDevice();
            if(!device) {std::cout<<"{\"metal_device_available\":false,\"pixel_test\":\"not run\"}\n";return 77;}
            g1::MetalM3GRenderer renderer(device);
            auto surface=quad(1,1,0,{{1,1,1,1}});surface.appearance.blending=64;
            surface.appearance.texture=g1::M3GTextureState{};
            surface.appearance.texture->image_object=8;surface.appearance.texture->image_format=100;
            surface.appearance.texture->wrap_s=surface.appearance.texture->wrap_t=240;
            auto asset=mesh_asset({surface});
            const g1::RgbaImage corners{2,2,{255,0,0,255, 0,255,0,128, 0,0,255,0, 23,47,91,63}};
            asset.geometry.images.emplace(8,g1::decode_png(g1::encode_png(corners)));
            const auto image=render(device,renderer,asset);
            pixel(image,8,8,{{255,0,0,255}},"M3G mesh UV and row orientation");
            pixel(image,55,8,{{0,128,0,191}},"M3G original source-alpha color and alpha blending");
            pixel(image,8,55,{{0,0,0,255}},"M3G zero-alpha fragment");
            pixel(image,55,55,{{6,12,22,208}},"M3G RGBA channel path");

            const auto depth=render(device,renderer,mesh_asset({quad(1,1,0,{{1,0,0,1}}),
                quad(2,0.5F,-0.5F,{{0,1,0,1}}),quad(3,1,0.75F,{{0,0,1,1}})}));
            pixel(depth,32,32,{{0,255,0,255}},"M3G nearer fragment");
            pixel(depth,8,8,{{255,0,0,255}},"M3G farther fragment rejection");
            auto always=quad(2,1,0.75F,{{0,0,1,1}});always.appearance.depth_test=false;
            const auto independent=render(device,renderer,mesh_asset({quad(1,1,-0.75F,{{1,0,0,1}}),
                always,quad(3,1,0,{{0,1,0,1}})}));
            pixel(independent,32,32,{{0,255,0,255}},"M3G ALWAYS test still writes independent depth");

            const auto enhanced=render(device,renderer,asset,1,true);
            pixel(enhanced,2,2,{{255,0,0,255}},"Modern enhanced texture preserves UV corner");
            pixel(enhanced,2,60,{{0,0,0,255}},"Modern enhancement preserves transparent corner");
            const auto mixed=render(device,renderer,mesh_asset({quad(1,1,0,{{1,0,0,1}})}),1,false,true);
            pixel(mixed,8,32,{{0,255,0,255}},"Shared terrain occludes 3D model");
            pixel(mixed,55,32,{{255,0,0,255}},"3D model remains visible beside terrain");

            // Complementary coverage must work even when texture REPLACE
            // ignores the node alpha: never a vanished or doubled transition.
            auto robot=quad(1,1,0,{{1,1,1,1}}),car=quad(2,1,0,{{1,1,1,1}});
            robot.appearance.texture=g1::M3GTextureState{};robot.appearance.texture->function=228;
            robot.appearance.texture->image_object=40;robot.appearance.texture->image_format=100;
            car.appearance.texture=*robot.appearance.texture;car.appearance.texture->image_object=41;
            auto assembly=mesh_asset({robot,car});
            assembly.geometry.images.emplace(40,g1::RgbaImage{1,1,{255,0,0,255}});
            assembly.geometry.images.emplace(41,g1::RgbaImage{1,1,{0,0,255,255}});
            constexpr unsigned mask[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
            for(float mix:{0.0F,.5F,1.0F}){
                const auto result=render(device,renderer,assembly,1,false,false,mix);
                for(unsigned y=20;y<24;++y)for(unsigned x=20;x<24;++x){
                    bool vehicle=(mask[(y&3)*4+(x&3)]+.5F)/16<mix;
                    pixel(result,x,y,vehicle?std::array<int,4>{{0,0,255,255}}:std::array<int,4>{{255,0,0,255}},
                        "Rigid assembly complementary opaque coverage");
                }
            }

            asset.geometry.surfaces[0].appearance.blending=68;
            asset.geometry.surfaces[0].appearance.texture->function=228;
            const auto replace=render(device,renderer,asset,0.25F);
            pixel(replace,55,8,{{0,255,0,128}},"M3G RGBA REPLACE overwrites previously applied node alpha");

            g1::M3GRenderAsset sprite_asset;
            g1::RgbaImage sprite_image{32,32,g1::Bytes(32*32*4)};
            for(std::size_t y=0;y<32;++y)for(std::size_t x=0;x<32;++x) {
                const auto source=((y/16)*2+x/16)*4;
                std::copy_n(corners.pixels.begin()+static_cast<std::ptrdiff_t>(source),4,
                    sprite_image.pixels.begin()+static_cast<std::ptrdiff_t>((y*32+x)*4));
            }
            sprite_asset.geometry.images.emplace(8,std::move(sprite_image));
            g1::M3GSpriteAsset sprite;sprite.node_object=9;sprite.image_object=8;sprite.appearance_object=10;
            sprite.model=identity;sprite.scaled=false;sprite.image_format=100;sprite.crop_pixels={{0,0,32,32}};
            sprite.appearance.blending=64;sprite.appearance.culling=162;sprite_asset.sprites.push_back(sprite);
            sprite_asset.traversal.push_back({g1::M3GDrawableKind::sprite,0});
            const auto billboard=render(device,renderer,sprite_asset);
            pixel(billboard,24,24,{{255,0,0,255}},"M3G Sprite3D upper-left rectangle and UV");
            pixel(billboard,40,24,{{0,128,0,191}},"M3G Sprite3D alpha compositing");
            pixel(billboard,24,40,{{0,0,0,255}},"M3G Sprite3D transparent texel");
            pixel(billboard,40,40,{{6,12,22,208}},"M3G Sprite3D lower-right rectangle and UV");
            if(argc==2) {
                const auto png=g1::encode_png(image);std::ofstream out(argv[1],std::ios::binary);
                out.write(reinterpret_cast<const char*>(png.data()),static_cast<std::streamsize>(png.size()));
                if(!out)throw std::runtime_error("cannot write M3G probe PNG");
            }
            std::cout<<"{\"metal_device_available\":true,\"device\":\""<<device.name.UTF8String
                <<"\",\"pixel_checks\":"<<checks<<",\"pixel_channels_compared\":"<<checks*4
                <<",\"result\":\"passed\",\"original_game_data_used\":false,\"iphone_execution\":false}\n";
            return 0;
        } catch(const std::exception& error) {std::cerr<<"ERROR: "<<error.what()<<'\n';return 1;}
    }
}
