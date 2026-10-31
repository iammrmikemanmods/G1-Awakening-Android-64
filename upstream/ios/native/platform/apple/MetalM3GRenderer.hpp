#pragma once

#import <Metal/Metal.h>

#include "g1/M3GRenderFrame.hpp"
#include "g1/MaterialEnhancement.hpp"
#include <memory>

namespace g1 {

// GPU resources for one decoded rigid subtree. Geometry, appearances, images
// and original surface order are immutable after upload. Pose updates supply
// node matrices separately, so an unrelated model cannot reuse these buffers.
class MetalM3GModel {
public:
    std::size_t surface_count() const noexcept;
    std::size_t vertex_count() const noexcept;
    std::size_t texture_count() const noexcept;
    std::size_t sprite_count() const noexcept;
    std::size_t enhanced_texture_count() const noexcept;
    // Prepare frame instances using this retained immutable asset, so the
    // encoder can verify that a CPU plan refers to the uploaded geometry.
    const M3GRenderAsset& asset() const;
private:
    friend class MetalM3GRenderer;
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};

// Dedicated JSR184 rigid/unlit pipeline. It preserves decoded homogeneous UVs,
// vertex colors, texture environment and compositing/polygon state. Required
// lighting, mipmaps or nonzero depth bias fail before a model is uploaded.
// The caller owns the single-sample render pass, viewport, camera and
// presentation token. Color/depth formats must match the constructor. This
// adapter represents the original globally depth-enabled renderer capability;
// per-Appearance depth testing selects LEQUAL/ALWAYS with independent writes.
class MetalM3GRenderer {
public:
    explicit MetalM3GRenderer(id<MTLDevice>,
        MTLPixelFormat color_format=MTLPixelFormatBGRA8Unorm,
        MTLPixelFormat depth_format=MTLPixelFormatDepth32Float);
    ~MetalM3GRenderer();
    MetalM3GRenderer(const MetalM3GRenderer&)=delete;
    MetalM3GRenderer& operator=(const MetalM3GRenderer&)=delete;

    std::shared_ptr<MetalM3GModel> upload(const M3GStaticModel&);
    std::shared_ptr<MetalM3GModel> upload(const M3GRenderAsset&);
    std::shared_ptr<MetalM3GModel> upload_modern(const M3GRenderAsset&,MaterialEnhancement);
    // A null pose uses authored matrices. A supplied pose must contain every
    // drawn mesh. These are model/world matrices; view_projection is already
    // the caller's Metal clip transform (depth0..1). No axis conversion occurs.
    // Surfaces are stable-sorted by layer; equal-layer ordering is the original
    // decoded traversal. This is not a recovered battle transparency sorter.
    void encode(id<MTLRenderCommandEncoder>,const MetalM3GModel&,
        const M3GMatrix& view_projection,
        const std::map<std::size_t,M3GMatrix>* pose=nullptr);
    // The portable planner supplies one checked, original mesh/Sprite3D
    // queue across all attached instances. Its camera and pose are explicit;
    // no battle phase or presentation completion is inferred by this encoder.
    void encode_frame(id<MTLRenderCommandEncoder>,
        const std::vector<std::shared_ptr<MetalM3GModel>>&,
        const M3GPreparedFrame&);
private:
    std::shared_ptr<MetalM3GModel> upload_asset(const M3GRenderAsset&,bool legacy_static,
        MaterialEnhancement quality=MaterialEnhancement::original);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace g1
