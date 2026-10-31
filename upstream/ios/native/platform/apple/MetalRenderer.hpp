#pragma once

#import <Metal/Metal.h>

#include "g1/Scene.hpp"
#include <memory>

namespace g1 {
using Matrix4 = std::array<float, 16>; // Column-major, Metal clip depth 0..1.

struct MetalMesh {
    id<MTLBuffer> vertices;
    NSUInteger count{};
};

struct MetalDraw {
    std::shared_ptr<MetalMesh> mesh;
    id<MTLTexture> texture;
    Matrix4 matrix;
    std::array<float, 4> tint{{1, 1, 1, 1}};
    bool depth_write{true};
    std::array<float,2> uv_offset{};
    bool screen_space{}; // 2D effects neither test nor write tactical depth.
    bool nearest{}; // Explicit native pixel-art policy for the keyed BMP.
};

class MetalRenderer {
public:
    explicit MetalRenderer(id<MTLDevice> device);
    std::shared_ptr<MetalMesh> mesh(const std::vector<SceneVertex>&);
    id<MTLTexture> texture(const RgbaImage&);
    void encode(id<MTLCommandBuffer>, MTLRenderPassDescriptor*,
                const std::vector<MetalDraw>&);
    // Shared terrain/3D-unit pass: caller owns encoder begin/end and depth.
    void encode_into(id<MTLRenderCommandEncoder>,const std::vector<MetalDraw>&);
private:
    id<MTLDevice> device_;
    id<MTLRenderPipelineState> pipeline_;
    id<MTLSamplerState> sampler_, nearest_sampler_;
    id<MTLDepthStencilState> depth_write_, depth_read_, depth_none_;
};

Matrix4 identity_matrix() noexcept;
Matrix4 multiply_matrix(const Matrix4&, const Matrix4&) noexcept;
} // namespace g1
