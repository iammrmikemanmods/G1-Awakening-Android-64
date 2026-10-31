#include "MetalM3GRenderer.hpp"
#include "MetalM3GShader.hpp"
#include "CleanTextureStore.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <tuple>

namespace g1 {
namespace {
void require(bool value,const char* message) { if(!value)throw FormatError(message); }
std::runtime_error metal_error(const char* message,NSError* error=nil) {
    return std::runtime_error(std::string(message)+(error ? ": "+
        std::string(error.localizedDescription.UTF8String) : ""));
}
template<std::size_t N> bool finite(const std::array<float,N>& value) {
    return std::all_of(value.begin(),value.end(),[](float x){return std::isfinite(x);});
}
struct alignas(16) Uniform {
    M3GMatrix matrix;
    std::array<float,4> blend,state;
    std::array<std::uint32_t,4> flags;
};
static_assert(sizeof(Uniform)==112 && offsetof(Uniform,blend)==64 &&
    offsetof(Uniform,state)==80 && offsetof(Uniform,flags)==96,"M3G Metal uniform layout changed");
static_assert(sizeof(M3GVertex)==64 && offsetof(M3GVertex,normal)==16 &&
    offsetof(M3GVertex,color)==32 && offsetof(M3GVertex,texcoord)==48,"M3G Metal vertex layout changed");

void validate(const M3GStaticModel& model,bool allow_empty=false) {
    require(allow_empty || (model.has_geometry && !model.surfaces.empty()),"M3G upload requires actual rigid geometry");
    require(model.surfaces.empty()!=model.has_geometry,"M3G geometry flag differs from surface inventory");
    require(model.surfaces.size()<=65536,"M3G surface count exceeds renderer bound");
    std::size_t vertices=0;
    for(const auto& surface:model.surfaces) {
        const auto& a=surface.appearance;
        require(!surface.vertices.empty() && surface.vertices.size()%3==0,
                "M3G surface is not an expanded triangle list");
        require(surface.vertices.size()<=3000000 && vertices<=3000000-surface.vertices.size(),
                "M3G model exceeds bounded GPU vertex allocation");
        vertices+=surface.vertices.size();
        require(finite(surface.model) && std::isfinite(surface.effective_alpha) &&
                surface.effective_alpha>=0 && surface.effective_alpha<=1,"invalid M3G surface transform or alpha");
        require(!a.material,"M3G material lighting is not implemented by the rigid unlit renderer");
        require(a.blending>=64 && a.blending<=68,"unsupported M3G compositing mode");
        require(a.culling>=160 && a.culling<=162 && (a.shading==164 || a.shading==165) &&
                (a.winding==168 || a.winding==169),"unsupported M3G polygon state");
        require(std::isfinite(a.alpha_threshold) && a.alpha_threshold>=0 && a.alpha_threshold<=1,
                "invalid M3G alpha-test threshold");
        require(a.depth_offset_factor==0 && a.depth_offset_units==0,
                "M3G nonzero polygon depth offset is outside this renderer slice");
        if(a.texture) {
            const auto& t=*a.texture;
            require(t.image_format>=96 && t.image_format<=100 && t.function>=224 && t.function<=228,
                    "unsupported M3G texture source or function");
            require(t.function!=225 || t.image_format==99 || t.image_format==100,
                    "M3G DECAL is undefined for this source image format");
            require((t.wrap_s==240 || t.wrap_s==241) && (t.wrap_t==240 || t.wrap_t==241),
                    "unsupported M3G texture wrapping");
            require(t.level_filter==208 && (t.image_filter==209 || t.image_filter==210),
                    "M3G mipmapped texture levels are outside this renderer slice");
            require(finite(t.blend_color),"nonfinite M3G texture blend color");
            require(model.images.count(t.image_object)!=0,"M3G texture image is absent from decoded model");
        }
        for(const auto& v:surface.vertices)
            require(finite(v.position) && finite(v.normal) && finite(v.color) && finite(v.texcoord),
                    "nonfinite M3G vertex attribute");
    }
    for(const auto& entry:model.images) {
        const auto& image=entry.second;
        require(image.width>0 && image.height>0 && image.width<=8192 && image.height<=8192 &&
            image.pixels.size()==std::size_t(image.width)*image.height*4,"invalid M3G decoded RGBA image");
    }
}

using PipelineKey=std::tuple<std::uint8_t,bool,bool>;
using SamplerKey=std::tuple<std::uint8_t,std::uint8_t,std::uint8_t>;
}

struct MetalM3GModel::Impl {
    struct Surface {
        std::size_t mesh_object{},submesh_index{};
        M3GMatrix authored_matrix;
        M3GAppearanceState appearance;
        float effective_alpha{};
        id<MTLBuffer> vertices;
        id<MTLTexture> texture;
        id<MTLRenderPipelineState> pipeline;
        id<MTLSamplerState> sampler;
        id<MTLDepthStencilState> depth;
        NSUInteger count{};
    };
    struct Sprite {
        std::size_t node_object{};
        std::uint8_t image_format{};
        M3GAppearanceState appearance;
        id<MTLTexture> texture;
        id<MTLRenderPipelineState> pipeline;
        id<MTLSamplerState> sampler;
        id<MTLDepthStencilState> depth;
    };
    id<MTLDevice> device;
    MTLPixelFormat color_format{},depth_format{};
    std::shared_ptr<const M3GRenderAsset> source;
    bool legacy_static{};
    std::vector<Surface> surfaces;
    std::vector<Sprite> sprites;
    std::map<std::size_t,id<MTLTexture>> images;
    std::size_t vertex_count{}, enhanced_textures{};
};

struct MetalM3GRenderer::Impl {
    id<MTLDevice> device;
    id<MTLFunction> vertex_function,fragment_function;
    id<MTLTexture> white_texture;
    MTLPixelFormat color_format{},depth_format{};
    std::map<PipelineKey,id<MTLRenderPipelineState>> pipelines;
    std::map<SamplerKey,id<MTLSamplerState>> samplers;
    std::map<unsigned,id<MTLDepthStencilState>> depths;

    id<MTLTexture> texture(const RgbaImage& image) {
        MTLTextureDescriptor* descriptor=[MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:image.width height:image.height mipmapped:NO];
        descriptor.usage=MTLTextureUsageShaderRead;
        descriptor.storageMode=MTLStorageModeShared;
        id<MTLTexture> result=[device newTextureWithDescriptor:descriptor];
        if(!result)throw metal_error("M3G texture allocation failed");
        [result replaceRegion:MTLRegionMake2D(0,0,image.width,image.height) mipmapLevel:0
            withBytes:image.pixels.data() bytesPerRow:std::size_t(image.width)*4];
        return result;
    }
    id<MTLRenderPipelineState> pipeline(const M3GAppearanceState& a) {
        const PipelineKey key{a.blending,a.color_write,a.alpha_write};
        const auto found=pipelines.find(key);if(found!=pipelines.end())return found->second;
        MTLRenderPipelineDescriptor* d=[MTLRenderPipelineDescriptor new];
        d.label=@"G1 native rigid M3G";
        d.vertexFunction=vertex_function;d.fragmentFunction=fragment_function;
        d.depthAttachmentPixelFormat=depth_format;
        auto c=d.colorAttachments[0];c.pixelFormat=color_format;
        c.writeMask=static_cast<MTLColorWriteMask>((a.color_write ?
            MTLColorWriteMaskRed|MTLColorWriteMaskGreen|MTLColorWriteMaskBlue : 0) |
            (a.alpha_write ? MTLColorWriteMaskAlpha : 0));
        c.blendingEnabled=a.blending!=68;
        c.rgbBlendOperation=MTLBlendOperationAdd;c.alphaBlendOperation=MTLBlendOperationAdd;
        switch(a.blending) {
        case 64: // Cs*As + Cd*(1-As), including the alpha channel.
            c.sourceRGBBlendFactor=c.sourceAlphaBlendFactor=MTLBlendFactorSourceAlpha;
            c.destinationRGBBlendFactor=c.destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;break;
        case 65:
            c.sourceRGBBlendFactor=c.sourceAlphaBlendFactor=MTLBlendFactorSourceAlpha;
            c.destinationRGBBlendFactor=c.destinationAlphaBlendFactor=MTLBlendFactorOne;break;
        case 66:
            c.sourceRGBBlendFactor=MTLBlendFactorDestinationColor;c.sourceAlphaBlendFactor=MTLBlendFactorDestinationAlpha;
            c.destinationRGBBlendFactor=c.destinationAlphaBlendFactor=MTLBlendFactorZero;break;
        case 67:
            c.sourceRGBBlendFactor=MTLBlendFactorDestinationColor;c.sourceAlphaBlendFactor=MTLBlendFactorDestinationAlpha;
            c.destinationRGBBlendFactor=MTLBlendFactorSourceColor;c.destinationAlphaBlendFactor=MTLBlendFactorSourceAlpha;break;
        case 68:
            c.sourceRGBBlendFactor=c.sourceAlphaBlendFactor=MTLBlendFactorOne;
            c.destinationRGBBlendFactor=c.destinationAlphaBlendFactor=MTLBlendFactorZero;break;
        }
        NSError* error=nil;id<MTLRenderPipelineState> result=[device newRenderPipelineStateWithDescriptor:d error:&error];
        if(!result)throw metal_error("M3G pipeline creation failed",error);
        pipelines.emplace(key,result);return result;
    }
    id<MTLSamplerState> sampler(const std::optional<M3GTextureState>& texture_state) {
        const SamplerKey key=texture_state ? SamplerKey{texture_state->image_filter,texture_state->wrap_s,texture_state->wrap_t}
            : SamplerKey{210,240,240};
        const auto found=samplers.find(key);if(found!=samplers.end())return found->second;
        MTLSamplerDescriptor* d=[MTLSamplerDescriptor new];d.normalizedCoordinates=YES;
        d.minFilter=d.magFilter=std::get<0>(key)==209 ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.mipFilter=MTLSamplerMipFilterNotMipmapped;
        d.sAddressMode=std::get<1>(key)==240 ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
        d.tAddressMode=std::get<2>(key)==240 ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
        id<MTLSamplerState> result=[device newSamplerStateWithDescriptor:d];
        if(!result)throw metal_error("M3G sampler allocation failed");
        samplers.emplace(key,result);return result;
    }
    id<MTLDepthStencilState> depth(const M3GAppearanceState& a) {
        const unsigned key=unsigned(a.depth_test)|(unsigned(a.depth_write)<<1U);
        const auto found=depths.find(key);if(found!=depths.end())return found->second;
        MTLDepthStencilDescriptor* d=[MTLDepthStencilDescriptor new];
        // Original b5120: b5224 selects LEQUAL/ALWAYS from mode+0x22, then
        // b522c applies mode+0x23 through glDepthMask independently. This
        // adapter's render pass has the global depth capability enabled.
        d.depthCompareFunction=a.depth_test ? MTLCompareFunctionLessEqual : MTLCompareFunctionAlways;
        d.depthWriteEnabled=a.depth_write;
        id<MTLDepthStencilState> result=[device newDepthStencilStateWithDescriptor:d];
        if(!result)throw metal_error("M3G depth-state allocation failed");
        depths.emplace(key,result);return result;
    }
};

std::size_t MetalM3GModel::surface_count() const noexcept { return impl_ ? impl_->surfaces.size() : 0; }
std::size_t MetalM3GModel::vertex_count() const noexcept { return impl_ ? impl_->vertex_count : 0; }
std::size_t MetalM3GModel::texture_count() const noexcept { return impl_ ? impl_->images.size() : 0; }
std::size_t MetalM3GModel::sprite_count() const noexcept { return impl_ ? impl_->sprites.size() : 0; }
const M3GRenderAsset& MetalM3GModel::asset() const {
    require(impl_ && impl_->source,"M3G model has no uploaded source asset");return *impl_->source;
}

MetalM3GRenderer::MetalM3GRenderer(id<MTLDevice> device,MTLPixelFormat color,MTLPixelFormat depth)
    :impl_(std::make_unique<Impl>()) {
    if(!device)throw metal_error("M3G Metal device is unavailable");
    require(color==MTLPixelFormatBGRA8Unorm || color==MTLPixelFormatRGBA8Unorm,
            "M3G renderer requires a linear normalized RGBA color target");
    require(depth==MTLPixelFormatDepth32Float,"M3G renderer requires a Depth32Float target");
    impl_->device=device;impl_->color_format=color;impl_->depth_format=depth;
    NSError* error=nil;MTLCompileOptions* options=[MTLCompileOptions new];options.fastMathEnabled=NO;
    id<MTLLibrary> library=[device newLibraryWithSource:[NSString stringWithUTF8String:m3g_metal_shader]
        options:options error:&error];
    if(!library)throw metal_error("M3G Metal shader compilation failed",error);
    impl_->vertex_function=[library newFunctionWithName:@"g1_m3g_vertex"];
    impl_->fragment_function=[library newFunctionWithName:@"g1_m3g_fragment"];
    if(!impl_->vertex_function || !impl_->fragment_function)throw metal_error("M3G shader entry point is missing");
    RgbaImage white;white.width=1;white.height=1;white.pixels={255,255,255,255};
    impl_->white_texture=impl_->texture(white);
}
MetalM3GRenderer::~MetalM3GRenderer()=default;
std::size_t MetalM3GModel::enhanced_texture_count() const noexcept {return impl_?impl_->enhanced_textures:0;}
std::shared_ptr<MetalM3GModel> MetalM3GRenderer::upload_modern(const M3GRenderAsset& asset,MaterialEnhancement quality) {
    return upload_asset(asset,false,quality);
}

std::shared_ptr<MetalM3GModel> MetalM3GRenderer::upload(const M3GStaticModel& model) {
    validate(model);
    M3GRenderAsset asset;asset.geometry=model;
    for(std::size_t i=0;i<model.surfaces.size();++i)asset.traversal.push_back({M3GDrawableKind::mesh,i});
    return upload_asset(asset,true);
}
std::shared_ptr<MetalM3GModel> MetalM3GRenderer::upload(const M3GRenderAsset& asset) {
    return upload_asset(asset,false);
}
std::shared_ptr<MetalM3GModel> MetalM3GRenderer::upload_asset(const M3GRenderAsset& asset,bool legacy_static,MaterialEnhancement quality) {
    const auto& model=asset.geometry;
    validate(model,!asset.sprites.empty());
    require(asset.sprites.size()<=65536,"M3G Sprite3D count exceeds renderer bound");
    for(const auto& sprite:asset.sprites) {
        const auto& a=sprite.appearance;
        require(finite(sprite.model) && std::isfinite(sprite.effective_alpha) &&
                sprite.effective_alpha>=0 && sprite.effective_alpha<=1,"invalid M3G Sprite3D transform or alpha");
        require(sprite.image_format>=96 && sprite.image_format<=100 && model.images.count(sprite.image_object),
                "M3G Sprite3D image or format is absent");
        const auto& image=model.images.at(sprite.image_object);
        require(image.width<=1024 && image.height<=1024 &&
                !(image.width&(image.width-1)) && !(image.height&(image.height-1)),
                "M3G Sprite3D requires a single power-of-two image block");
        require(!a.material && !a.texture && a.culling==162,
                "M3G Sprite3D appearance is outside the recovered unlit path");
        require(a.blending>=64 && a.blending<=68 && std::isfinite(a.alpha_threshold) &&
                a.alpha_threshold>=0 && a.alpha_threshold<=1 && a.depth_offset_factor==0 && a.depth_offset_units==0,
                "unsupported M3G Sprite3D compositing state");
        require(std::abs(std::int64_t(sprite.crop_pixels[2]))<=1024 &&
                std::abs(std::int64_t(sprite.crop_pixels[3]))<=1024,"M3G Sprite3D crop dimension exceeds original bound");
    }
    for(const auto& reference:asset.traversal)
        require(reference.kind==M3GDrawableKind::mesh ? reference.index<model.surfaces.size() :
                reference.kind==M3GDrawableKind::sprite && reference.index<asset.sprites.size(),
                "M3G upload drawable reference is invalid");
    // All required data/state checks precede GPU allocations.
    auto gpu=std::make_shared<MetalM3GModel::Impl>();gpu->device=impl_->device;
    gpu->color_format=impl_->color_format;gpu->depth_format=impl_->depth_format;
    gpu->source=std::make_shared<const M3GRenderAsset>(asset);
    gpu->legacy_static=legacy_static;
    std::size_t enhancedBytes=0;
    for(const auto& entry:model.images) {
        const auto& image=entry.second;
        const std::size_t derivedBytes=std::size_t(image.width)*image.height*16;
        bool upgraded=false;
        if(quality!=MaterialEnhancement::original && image.width<=1024 && image.height<=1024 &&
           derivedBytes<=12*1024*1024-enhancedBytes) {
            // GPU pixels only. The 12MiB ceiling admits the original forest
            // sky AND ground. Failed derived allocations retry source pixels.
            // Source dimensions, Sprite3D crops and animation keys stay exact.
            try {
                const auto cleaned=clean_texture(image,"material",2);
                const auto enhanced=cleaned?*cleaned:enhance_material(image,quality);
                gpu->images.emplace(entry.first,impl_->texture(enhanced));
                enhancedBytes+=derivedBytes;++gpu->enhanced_textures;upgraded=true;
            } catch(const std::exception&) { /* original allocation below */ }
        }
        if(!upgraded)gpu->images.emplace(entry.first,impl_->texture(image));
    }
    gpu->surfaces.reserve(model.surfaces.size());
    for(const auto& s:model.surfaces) {
        MetalM3GModel::Impl::Surface out;
        out.mesh_object=s.mesh_object;out.submesh_index=s.submesh_index;out.authored_matrix=s.model;
        out.appearance=s.appearance;out.effective_alpha=s.effective_alpha;out.count=s.vertices.size();
        out.vertices=[impl_->device newBufferWithBytes:s.vertices.data()
            length:s.vertices.size()*sizeof(M3GVertex) options:MTLResourceStorageModeShared];
        if(!out.vertices)throw metal_error("M3G vertex allocation failed");
        out.texture=s.appearance.texture ? gpu->images.at(s.appearance.texture->image_object) : impl_->white_texture;
        out.pipeline=impl_->pipeline(s.appearance);
        auto sampled=s.appearance.texture;
        if(quality!=MaterialEnhancement::original && sampled)sampled->image_filter=209;
        out.sampler=impl_->sampler(sampled);out.depth=impl_->depth(s.appearance);
        gpu->vertex_count+=s.vertices.size();gpu->surfaces.push_back(std::move(out));
    }
    gpu->sprites.reserve(asset.sprites.size());
    for(const auto& source:asset.sprites) {
        MetalM3GModel::Impl::Sprite out;out.node_object=source.node_object;out.image_format=source.image_format;
        out.appearance=source.appearance;out.texture=gpu->images.at(source.image_object);
        out.pipeline=impl_->pipeline(source.appearance);
        std::optional<M3GTextureState> sampled;
        if(quality!=MaterialEnhancement::original){sampled=M3GTextureState{};sampled->image_filter=209;sampled->wrap_s=240;sampled->wrap_t=240;}
        out.sampler=impl_->sampler(sampled);
        out.depth=impl_->depth(source.appearance);gpu->sprites.push_back(std::move(out));
    }
    auto result=std::make_shared<MetalM3GModel>();result->impl_=std::move(gpu);return result;
}

void MetalM3GRenderer::encode(id<MTLRenderCommandEncoder> encoder,const MetalM3GModel& model,
    const M3GMatrix& view_projection,const std::map<std::size_t,M3GMatrix>* pose) {
    require(encoder && model.impl_,"M3G draw requires an encoder and uploaded model");
    const auto& gpu=*model.impl_;
    require(gpu.legacy_static,"stateful M3G render assets require encode_frame with current node state");
    require(gpu.device==impl_->device && gpu.color_format==impl_->color_format && gpu.depth_format==impl_->depth_format,
            "M3G model belongs to a different Metal renderer configuration");
    require(finite(view_projection),"nonfinite M3G camera matrix");
    // Validate every matrix and prepare all uniforms before recording a draw.
    // A later missing node or overflowing product must not partly draw a pose.
    std::vector<Uniform> uniforms;uniforms.reserve(gpu.surfaces.size());
    for(const auto& s:gpu.surfaces) {
        const auto& a=s.appearance;
        const M3GMatrix* transform=&s.authored_matrix;
        if(pose) {
            const auto found=pose->find(s.mesh_object);
            require(found!=pose->end() && finite(found->second),"M3G pose omits a drawn mesh or contains a nonfinite matrix");
            transform=&found->second;
        }
        Uniform uniform{};
        uniform.matrix=m3g_multiply(view_projection,*transform);
        require(finite(uniform.matrix),"M3G camera/model product is nonfinite");
        uniform.blend[3]=a.alpha_threshold;uniform.state[0]=s.effective_alpha;uniform.flags[3]=a.shading==164 ? 1U : 0U;
        if(a.texture) {
            std::copy(a.texture->blend_color.begin(),a.texture->blend_color.end(),uniform.blend.begin());
            uniform.flags[0]=a.texture->function;uniform.flags[1]=a.texture->image_format;uniform.flags[2]=1;
        }
        uniforms.push_back(uniform);
    }
    // Preserve the previous rigid API's stable layer ordering. Stateful scene
    // encoding below uses the recovered common queue from the CPU frame plan.
    std::vector<std::size_t> order(gpu.surfaces.size());std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](const auto a,const auto b) {
        return gpu.surfaces[a].appearance.layer<gpu.surfaces[b].appearance.layer;
    });
    [encoder setTriangleFillMode:MTLTriangleFillModeFill];[encoder setDepthBias:0 slopeScale:0 clamp:0];
    for(const auto i:order) {
        const auto& s=gpu.surfaces[i];const auto& a=s.appearance;const auto& uniform=uniforms[i];
        [encoder setRenderPipelineState:s.pipeline];[encoder setDepthStencilState:s.depth];
        [encoder setCullMode:a.culling==160 ? MTLCullModeBack : a.culling==161 ? MTLCullModeFront : MTLCullModeNone];
        [encoder setFrontFacingWinding:a.winding==168 ? MTLWindingCounterClockwise : MTLWindingClockwise];
        [encoder setVertexBuffer:s.vertices offset:0 atIndex:0];
        [encoder setVertexBytes:&uniform length:sizeof(uniform) atIndex:1];
        [encoder setFragmentBytes:&uniform length:sizeof(uniform) atIndex:0];
        [encoder setFragmentTexture:s.texture atIndex:0];[encoder setFragmentSamplerState:s.sampler atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:s.count];
    }
}
void MetalM3GRenderer::encode_frame(id<MTLRenderCommandEncoder> encoder,
    const std::vector<std::shared_ptr<MetalM3GModel>>& models,const M3GPreparedFrame& frame) {
    require(encoder && models.size()==frame.assets.size(),"M3G frame model/source count differs");
    for(std::size_t i=0;i<models.size();++i) {
        require(models[i] && models[i]->impl_,"M3G frame includes an unuploaded model");
        const auto& gpu=*models[i]->impl_;
        require(gpu.device==impl_->device && gpu.color_format==impl_->color_format && gpu.depth_format==impl_->depth_format,
                "M3G frame model belongs to another Metal renderer configuration");
        require(gpu.source.get()==frame.assets[i],"M3G frame was prepared for different source geometry");
    }
    std::vector<Uniform> uniforms;uniforms.reserve(frame.draws.size());
    // Resolve and check every draw before emitting commands into the pass.
    for(const auto& draw:frame.draws) {
        require(draw.instance<models.size(),"M3G frame draw instance is absent");
        const auto& gpu=*models[draw.instance]->impl_;
        require(finite(draw.metal_matrix) && std::isfinite(draw.effective_alpha) &&
                draw.effective_alpha>=0 && draw.effective_alpha<=1,"invalid M3G prepared frame transform or alpha");
        Uniform uniform{};uniform.matrix=draw.metal_matrix;uniform.state[0]=draw.effective_alpha;
        require(draw.assembly_role<=2 && std::isfinite(draw.assembly_mix) && draw.assembly_mix>=0 && draw.assembly_mix<=1 &&
            std::isfinite(draw.assembly_highlight) && draw.assembly_highlight>=0 && draw.assembly_highlight<=0.35F,
            "Invalid procedural assembly coverage");
        uniform.state[1]=draw.assembly_role;uniform.state[2]=draw.assembly_mix;uniform.state[3]=draw.assembly_highlight;
        if(draw.kind==M3GDrawableKind::mesh) {
            require(draw.geometry_index<gpu.surfaces.size(),"M3G frame mesh index is invalid");
            const auto& source=gpu.surfaces[draw.geometry_index];
            require(source.mesh_object==draw.node_object,"M3G frame mesh identity differs");
            const auto& a=source.appearance;
            uniform.blend[3]=a.alpha_threshold;uniform.flags[3]=a.shading==164 ? 1U : 0U;
            if(a.texture) {
                std::copy(a.texture->blend_color.begin(),a.texture->blend_color.end(),uniform.blend.begin());
                uniform.flags[0]=a.texture->function;uniform.flags[1]=a.texture->image_format;uniform.flags[2]=1;
            }
        } else {
            require(draw.kind==M3GDrawableKind::sprite && draw.geometry_index<gpu.sprites.size(),
                    "M3G frame Sprite3D index is invalid");
            const auto& source=gpu.sprites[draw.geometry_index];
            require(source.node_object==draw.node_object,"M3G frame Sprite3D identity differs");
            uniform.blend[3]=source.appearance.alpha_threshold;
            // Original b7d6c..b7db4: full alpha or RGB uses REPLACE; finite
            // alpha<1 with other formats uses MODULATE. Sampler is base-level
            // NEAREST + CLAMP_TO_EDGE from the original RenderTexRect call.
            uniform.flags[0]=draw.effective_alpha>=1 || source.image_format==99 ? 228U : 227U;
            uniform.flags[1]=source.image_format;uniform.flags[2]=1;
            if(draw.sprite_projection.drawn())for(const auto& vertex:draw.sprite_vertices)
                require(finite(vertex.position) && finite(vertex.color) && finite(vertex.texcoord),
                        "nonfinite M3G projected Sprite3D vertex");
        }
        uniforms.push_back(uniform);
    }
    [encoder setTriangleFillMode:MTLTriangleFillModeFill];[encoder setDepthBias:0 slopeScale:0 clamp:0];
    for(std::size_t i=0;i<frame.draws.size();++i) {
        const auto& draw=frame.draws[i];if(!draw.draws_geometry())continue;
        const auto& gpu=*models[draw.instance]->impl_;const auto& uniform=uniforms[i];
        if(draw.kind==M3GDrawableKind::mesh) {
            const auto& source=gpu.surfaces[draw.geometry_index];const auto& a=source.appearance;
            [encoder setRenderPipelineState:source.pipeline];[encoder setDepthStencilState:source.depth];
            [encoder setCullMode:a.culling==160 ? MTLCullModeBack : a.culling==161 ? MTLCullModeFront : MTLCullModeNone];
            [encoder setFrontFacingWinding:a.winding==168 ? MTLWindingCounterClockwise : MTLWindingClockwise];
            [encoder setVertexBuffer:source.vertices offset:0 atIndex:0];
            [encoder setVertexBytes:&uniform length:sizeof(uniform) atIndex:1];
            [encoder setFragmentBytes:&uniform length:sizeof(uniform) atIndex:0];
            [encoder setFragmentTexture:source.texture atIndex:0];[encoder setFragmentSamplerState:source.sampler atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:source.count];
        } else {
            const auto& source=gpu.sprites[draw.geometry_index];
            [encoder setRenderPipelineState:source.pipeline];[encoder setDepthStencilState:source.depth];
            [encoder setCullMode:MTLCullModeNone];[encoder setFrontFacingWinding:MTLWindingCounterClockwise];
            [encoder setVertexBytes:draw.sprite_vertices.data() length:sizeof(draw.sprite_vertices) atIndex:0];
            [encoder setVertexBytes:&uniform length:sizeof(uniform) atIndex:1];
            [encoder setFragmentBytes:&uniform length:sizeof(uniform) atIndex:0];
            [encoder setFragmentTexture:source.texture atIndex:0];[encoder setFragmentSamplerState:source.sampler atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:draw.sprite_vertices.size()];
        }
    }
}
} // namespace g1
