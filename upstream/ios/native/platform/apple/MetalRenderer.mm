#include "MetalRenderer.hpp"

#include <limits>

namespace g1 {
namespace {
constexpr const char* shader = R"METAL(
#include <metal_stdlib>
using namespace metal;
struct Vertex { float4 position; float2 uv; float2 padding; };
struct Uniform { float4x4 matrix; float4 tint; float4 texture_offset; };
struct Out { float4 position [[position]]; float2 uv; float4 tint; };
vertex Out g1_vertex(uint index [[vertex_id]], const device Vertex* vertices [[buffer(0)]],
                     constant Uniform& uniform [[buffer(1)]]) {
    Out out;
    out.position = uniform.matrix * vertices[index].position;
    out.uv = vertices[index].uv + uniform.texture_offset.xy;
    out.tint = uniform.tint;
    return out;
}
fragment float4 g1_fragment(Out in [[stage_in]], texture2d<float> image [[texture(0)]],
                           sampler sample_state [[sampler(0)]]) {
    float4 color = image.sample(sample_state, in.uv) * in.tint;
    if (color.a <= 0.0039215686f) discard_fragment();
    return color;
}
)METAL";

struct alignas(16) Uniform {
    Matrix4 matrix;
    std::array<float, 4> tint;
    std::array<float, 4> texture_offset;
};
static_assert(sizeof(Uniform) == 96, "Metal uniform layout changed");

std::runtime_error metal_error(const char* message, NSError* error = nil) {
    return std::runtime_error(std::string(message) +
        (error ? ": " + std::string(error.localizedDescription.UTF8String) : ""));
}
} // namespace

Matrix4 identity_matrix() noexcept {
    return {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
}

Matrix4 multiply_matrix(const Matrix4& a, const Matrix4& b) noexcept {
    Matrix4 result{};
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t k = 0; k < 4; ++k)
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return result;
}

MetalRenderer::MetalRenderer(id<MTLDevice> device) : device_(device) {
    if (!device_) throw metal_error("Metal device is unavailable");
    NSError* error = nil;
    MTLCompileOptions* options = [MTLCompileOptions new];
    // This compiles the new native GPU shader, never original CPU code.
    id<MTLLibrary> library = [device_ newLibraryWithSource:
        [NSString stringWithUTF8String:shader] options:options error:&error];
    if (!library) throw metal_error("Metal shader compilation failed", error);
    MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.label = @"G1 native textured geometry";
    descriptor.vertexFunction = [library newFunctionWithName:@"g1_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"g1_fragment"];
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    auto color = descriptor.colorAttachments[0];
    color.blendingEnabled = YES;
    color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    color.sourceAlphaBlendFactor = MTLBlendFactorOne;
    color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeline_ = [device_ newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (!pipeline_) throw metal_error("Metal pipeline creation failed", error);
    MTLSamplerDescriptor* sample = [MTLSamplerDescriptor new];
    sample.minFilter = MTLSamplerMinMagFilterLinear;
    sample.magFilter = MTLSamplerMinMagFilterLinear;
    sample.mipFilter = MTLSamplerMipFilterNotMipmapped;
    sample.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sample.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_ = [device_ newSamplerStateWithDescriptor:sample];
    sample.minFilter = MTLSamplerMinMagFilterNearest;
    sample.magFilter = MTLSamplerMinMagFilterNearest;
    nearest_sampler_ = [device_ newSamplerStateWithDescriptor:sample];
    MTLDepthStencilDescriptor* depth = [MTLDepthStencilDescriptor new];
    depth.depthCompareFunction = MTLCompareFunctionLessEqual;
    depth.depthWriteEnabled = YES;
    depth_write_ = [device_ newDepthStencilStateWithDescriptor:depth];
    depth.depthWriteEnabled = NO;
    depth_read_ = [device_ newDepthStencilStateWithDescriptor:depth];
    depth.depthCompareFunction = MTLCompareFunctionAlways;
    depth_none_ = [device_ newDepthStencilStateWithDescriptor:depth];
    if (!sampler_ || !nearest_sampler_ || !depth_write_ || !depth_read_ || !depth_none_)
        throw metal_error("Metal render state allocation failed");
}

std::shared_ptr<MetalMesh> MetalRenderer::mesh(const std::vector<SceneVertex>& vertices) {
    if (vertices.empty()) return {};
    if (vertices.size() % 3 != 0 || vertices.size() > 1'000'000)
        throw FormatError("native render mesh is not a bounded triangle list");
    auto result = std::make_shared<MetalMesh>();
    result->vertices = [device_ newBufferWithBytes:vertices.data()
        length:vertices.size() * sizeof(SceneVertex) options:MTLResourceStorageModeShared];
    result->count = vertices.size();
    if (!result->vertices) throw metal_error("Metal vertex buffer allocation failed");
    return result;
}

id<MTLTexture> MetalRenderer::texture(const RgbaImage& image) {
    if (image.width == 0 || image.height == 0 || image.width > 8192 || image.height > 8192 ||
        image.pixels.size() != std::size_t(image.width) * image.height * 4)
        throw FormatError("invalid RGBA texture payload");
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:image.width height:image.height mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> result = [device_ newTextureWithDescriptor:descriptor];
    if (!result) throw metal_error("Metal texture allocation failed");
    [result replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height) mipmapLevel:0
        withBytes:image.pixels.data() bytesPerRow:std::size_t(image.width) * 4];
    return result;
}

void MetalRenderer::encode(id<MTLCommandBuffer> command, MTLRenderPassDescriptor* pass,
                            const std::vector<MetalDraw>& draws) {
    if (!command || !pass) throw metal_error("Metal command or render pass is missing");
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) throw metal_error("Metal render encoder creation failed");
    encoder.label = @"G1 native scene";
    try { encode_into(encoder,draws); } catch(...) { [encoder endEncoding]; throw; }
    [encoder endEncoding];
}
void MetalRenderer::encode_into(id<MTLRenderCommandEncoder> encoder,const std::vector<MetalDraw>& draws) {
    if(!encoder)throw metal_error("Shared scene encoder missing");
    for(const auto& draw:draws)if(draw.mesh && draw.mesh->count && !draw.texture)
        throw metal_error("draw texture is missing");
    [encoder setRenderPipelineState:pipeline_];
    // The recovered strip order is preserved. Culling is disabled in this
    // inspection renderer while original scene orientations are recovered.
    [encoder setCullMode:MTLCullModeNone];
    for (const auto& draw : draws) {
        if (!draw.mesh || draw.mesh->count == 0) continue;
        const Uniform uniform{draw.matrix, draw.tint,{{draw.uv_offset[0],draw.uv_offset[1],0,0}}};
        [encoder setDepthStencilState:draw.screen_space ? depth_none_ :
            (draw.depth_write ? depth_write_ : depth_read_)];
        [encoder setFragmentSamplerState:draw.nearest ? nearest_sampler_ : sampler_ atIndex:0];
        [encoder setVertexBuffer:draw.mesh->vertices offset:0 atIndex:0];
        [encoder setVertexBytes:&uniform length:sizeof(uniform) atIndex:1];
        [encoder setFragmentTexture:draw.texture atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:draw.mesh->count];
    }
}
} // namespace g1
