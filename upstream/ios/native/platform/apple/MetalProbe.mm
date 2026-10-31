#include "MetalRenderer.hpp"
#include "g1/IndexedBitmap.hpp"
#include "g1/SupportGeometry.hpp"

#include <fstream>
#include <iostream>

namespace {
std::vector<g1::SceneVertex> quad(float half, float depth) {
    const std::array<g1::SceneVertex, 4> vertices{{
        {{-half, half,depth,1},{0,0},{}}, {{-half,-half,depth,1},{0,1},{}},
        {{ half, half,depth,1},{1,0},{}}, {{ half,-half,depth,1},{1,1},{}}}};
    std::vector<g1::SceneVertex> result;
    for (const auto index : g1::sprite_triangle_indices) result.push_back(vertices[index]);
    return result;
}

g1::RgbaImage support_bitmap() {
    // Synthetic four-column, two-row BMP. Column0 is red/green, column1 is
    // keyed magenta, column2 blue and column3 yellow. A deliberately nonzero
    // palette high byte also exercises the original low24-bit key rule.
    g1::Bytes bytes{
        'B','M',82,0,0,0,0,0,0,0,74,0,0,0,
        40,0,0,0,4,0,0,0,2,0,0,0,1,0,8,0,
        0,0,0,0,8,0,0,0,0,0,0,0,0,0,0,0,5,0,0,0,5,0,0,0,
        255,0,255,170, 0,0,255,0, 0,255,0,0, 255,0,0,0, 0,255,255,0,
        2,0,3,4, 1,0,3,4};
    return g1::decode_indexed_bmp(bytes,0x00ff00ffU);
}

g1::OverlayDescriptor support_descriptor() {
    // Narrow v1 rectangles/variants, three frames and one explicit animation.
    // Frame1 lists blue before yellow: reverse part submission leaves blue
    // visible. These bytes describe only synthetic probe geometry.
    return g1::OverlayDescriptor({
        1,0,0,3,0, 0,0,2,2, 2,0,1,2, 3,0,1,2,
        0,3, 0,0, 1,0, 2,0,
        0,3,
        1,0,0,0,0,
        2,1,0,0,2,0,0,0,
        1,1,0,0,0,
        0,1,1,0,0,30},4,2);
}

g1::RgbaImage render(id<MTLDevice> device, g1::MetalRenderer& renderer,
                     const std::vector<g1::MetalDraw>& draws, unsigned extent = 64) {
    MTLTextureDescriptor* colorDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:extent height:extent mipmapped:NO];
    colorDesc.storageMode = MTLStorageModeShared;
    colorDesc.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> color = [device newTextureWithDescriptor:colorDesc];
    MTLTextureDescriptor* depthDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
        width:extent height:extent mipmapped:NO];
    depthDesc.storageMode = MTLStorageModePrivate;
    depthDesc.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> depth = [device newTextureWithDescriptor:depthDesc];
    if (!color || !depth) throw std::runtime_error("offscreen attachment allocation failed");
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor new];
    pass.colorAttachments[0].texture = color;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    pass.depthAttachment.texture = depth;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    renderer.encode(command, pass, draws);
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted)
        throw std::runtime_error(command.error.localizedDescription.UTF8String ?: "Metal command failed");
    g1::RgbaImage image{extent,extent,g1::Bytes(std::size_t(extent) * extent * 4)};
    [color getBytes:image.pixels.data() bytesPerRow:std::size_t(extent) * 4
        fromRegion:MTLRegionMake2D(0, 0, extent, extent) mipmapLevel:0];
    for (std::size_t i = 0; i < image.pixels.size(); i += 4)
        std::swap(image.pixels[i], image.pixels[i + 2]);
    return image;
}

void pixel(const g1::RgbaImage& image, unsigned x, unsigned y,
           std::array<int, 4> expected, const char* check) {
    const auto offset = (std::size_t(y) * image.width + x) * 4;
    for (std::size_t c = 0; c < 4; ++c)
        if (std::abs(int(image.pixels[offset + c]) - expected[c]) > 2)
            throw std::runtime_error(std::string(check) + " channel " + std::to_string(c) +
                " expected " + std::to_string(expected[c]) + " observed " +
                std::to_string(image.pixels[offset + c]));
}
} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        try {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device) {
                std::cout << "{\"metal_device_available\":false,\"pixel_test\":\"not run\"}\n";
                return 77; // CTest records a skip, never a graphics pass.
            }
            g1::MetalRenderer renderer(device);
            const auto full = renderer.mesh(quad(1, 0.5F));
            // Encode/decode through the same native PNG path used by game
            // assets. Known corner colors establish UV/row/alpha behavior.
            const g1::RgbaImage corners{2,2,{255,0,0,255, 0,255,0,128,
                                             0,0,255,0, 23,47,91,63}};
            id<MTLTexture> texture = renderer.texture(g1::decode_png(g1::encode_png(corners)));
            const auto matrix = g1::identity_matrix();
            const auto image = render(device, renderer, {{full,texture,matrix,{{1,1,1,1}},true}});
            pixel(image,8,8,{{255,0,0,255}},"top-left/row orientation");
            pixel(image,55,8,{{0,128,0,255}},"straight-alpha blending");
            pixel(image,8,55,{{0,0,0,255}},"transparent-fragment discard");
            pixel(image,55,55,{{6,12,22,255}},"RGBA channels and alpha");
            id<MTLTexture> white = renderer.texture({1,1,{255,255,255,255}});
            const auto near = renderer.mesh(quad(0.5F,0.2F));
            const auto far = renderer.mesh(quad(1,0.8F));
            const auto depth = render(device,renderer,{
                {full,white,matrix,{{1,0,0,1}},true},
                {near,white,matrix,{{0,1,0,1}},true},
                {far,white,matrix,{{0,0,1,1}},true}});
            pixel(depth,32,32,{{0,255,0,255}},"front fragment depth");
            pixel(depth,8,8,{{255,0,0,255}},"behind fragment rejection");

            // Resolve original logical pixels at 2x while exercising the same
            // native geometry, BMP decoder, texture upload, sampler and draw
            // states used by support presentation. This is a synthetic GPU
            // contract, not an original-scene or iPhone capture.
            constexpr unsigned support_extent = 640;
            const auto support_source = support_bitmap();
            const auto support_metadata = support_descriptor();
            id<MTLTexture> support_texture = renderer.texture(support_source);
            auto support_projection = matrix;
            support_projection[12] = -g1::support_world_anchor(0,0)[0];
            const auto support_mesh = [&](std::size_t frame) {
                return renderer.mesh(g1::support_overlay_vertices(support_metadata,frame,4,2,
                    support_projection,0,0,float(support_extent),float(support_extent)));
            };
            const auto crop_mesh = support_mesh(0);
            const auto parts_mesh = support_mesh(1);
            const auto blue_mesh = support_mesh(2);
            const auto support = render(device,renderer,{
                {full,white,matrix,{{1,1,1,1}},true},
                {crop_mesh,support_texture,matrix,{{1,1,1,1}},true,{},true,true}},support_extent);
            pixel(support,320,320,{{255,0,0,255}},"support exact v1 crop without +1 adjustment");
            pixel(support,320,323,{{0,255,0,255}},"support keyed BMP row orientation");
            pixel(support,321,320,{{255,0,0,255}},"support nearest opaque side of key boundary");
            pixel(support,322,320,{{255,255,255,255}},"support nearest transparent key gap");
            const auto reverse_parts = render(device,renderer,{
                {parts_mesh,support_texture,matrix,{{1,1,1,1}},true,{},true,true}},support_extent);
            pixel(reverse_parts,320,320,{{0,0,255,255}},"support reverse frame-part draw order");

            auto behind_matrix = matrix;
            behind_matrix[14] = 0.8F;
            const auto front_world = renderer.mesh(quad(1,0.2F));
            const auto ignores_depth = render(device,renderer,{
                {front_world,white,matrix,{{1,0,0,1}},true},
                {blue_mesh,support_texture,behind_matrix,{{1,1,1,1}},true,{},true,true}},support_extent);
            pixel(ignores_depth,320,320,{{0,0,255,255}},"support bypasses tactical depth comparison");

            auto ahead_matrix = matrix;
            ahead_matrix[14] = 0.1F;
            const auto retains_depth = render(device,renderer,{
                {far,white,matrix,{{1,1,1,1}},true},
                {blue_mesh,support_texture,ahead_matrix,{{1,1,1,1}},true,{},true,true},
                {full,white,matrix,{{1,0,0,1}},true}},support_extent);
            pixel(retains_depth,320,320,{{255,0,0,255}},"support does not write tactical depth");
            const auto restored_depth = render(device,renderer,{
                {front_world,white,matrix,{{1,1,1,1}},true},
                {blue_mesh,support_texture,behind_matrix,{{1,1,1,1}},true,{},true,true},
                {far,white,matrix,{{1,0,0,1}},true}},support_extent);
            pixel(restored_depth,320,320,{{0,0,255,255}},"ordinary draw restores tactical depth comparison");
            if (argc == 2) {
                const auto png = g1::encode_png(image);
                std::ofstream out(argv[1], std::ios::binary);
                out.write(reinterpret_cast<const char*>(png.data()), png.size());
                if (!out) throw std::runtime_error("cannot write probe PNG");
            }
            std::cout << "{\"metal_device_available\":true,\"device\":\""
                      << device.name.UTF8String << "\",\"pixel_checks\":14,"
                      << "\"pixel_channels_compared\":56,\"support_overlay_pixel_checks\":8,"
                      << "\"support_overlay_geometry\":\"synthetic\",\"result\":\"passed\","
                      << "\"original_game_data_used\":false,\"iphone_execution\":false}\n";
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "ERROR: " << error.what() << '\n';
            return 1;
        }
    }
}
