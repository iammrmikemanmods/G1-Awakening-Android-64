#pragma once

#include "GLESRenderer.hpp"
#include "g1/M3GRenderFrame.hpp"
#include "g1/MaterialEnhancement.hpp"
#include <functional>
#include <optional>

namespace g1 {

class GLESModel {
  public:
    std::size_t surface_count() const noexcept;
    std::size_t vertex_count() const noexcept;
    std::size_t texture_count() const noexcept;
    std::size_t sprite_count() const noexcept;
    std::size_t enhanced_texture_count() const noexcept;
    const M3GRenderAsset &asset() const;

  private:
    friend class GLESModelRenderer;
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};

class GLESModelRenderer {
  public:
    // Same source-pixel-keyed optional material pack as the iOS host.
    using CleanTextureLoader =
        std::function<std::optional<RgbaImage>(const RgbaImage &, const char *, unsigned)>;
    GLESModelRenderer();
    ~GLESModelRenderer();
    GLESModelRenderer(const GLESModelRenderer &) = delete;
    GLESModelRenderer &operator=(const GLESModelRenderer &) = delete;
    void set_clean_texture_loader(CleanTextureLoader);
    std::shared_ptr<GLESModel> upload(const M3GStaticModel &);
    std::shared_ptr<GLESModel> upload(const M3GRenderAsset &);
    std::shared_ptr<GLESModel> upload_modern(const M3GRenderAsset &, MaterialEnhancement);
    void encode(const GLESModel &, const M3GMatrix &,
                const std::map<std::size_t, M3GMatrix> *pose = nullptr);
    void encode_frame(const std::vector<std::shared_ptr<GLESModel>> &, const M3GPreparedFrame &);
    void abandon_context() noexcept;

  private:
    std::shared_ptr<GLESModel>
    upload_asset(const M3GRenderAsset &, bool legacy_static,
                 MaterialEnhancement quality = MaterialEnhancement::original);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace g1
