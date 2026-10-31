#pragma once

#include <GLES3/gl3.h>
#include "g1/Scene.hpp"
#include <memory>
#include <stdexcept>
#include <utility>

namespace g1 {
using Matrix4 = std::array<float, 16>; // Source matrices retain Metal depth 0..1.

// GL errors can report context loss before the next EGL swap/fence. Preserve
// that signal for the Android host's existing context recreation path.
class GLContextLost : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// A lost EGL context must be abandoned before old objects are destroyed. This
// prevents a recycled GL name from being deleted in a replacement context.
struct GLContextLifetime {
    bool active{true};
};

class GLTexture {
  public:
    ~GLTexture();
    GLTexture(const GLTexture &) = delete;
    GLTexture &operator=(const GLTexture &) = delete;
    GLuint id() const noexcept {
        return id_;
    }
    std::uint32_t width() const noexcept {
        return width_;
    }
    std::uint32_t height() const noexcept {
        return height_;
    }
    bool live() const noexcept {
        return lifetime_ && lifetime_->active;
    }

  private:
    friend class GLESRenderer;
    friend class GLESModelRenderer;
    GLTexture(GLuint id, std::uint32_t width, std::uint32_t height,
              std::shared_ptr<GLContextLifetime> lifetime)
        : id_(id), width_(width), height_(height), lifetime_(std::move(lifetime)) {}
    GLuint id_{};
    std::uint32_t width_{}, height_{};
    std::shared_ptr<GLContextLifetime> lifetime_;
};
using GLTexturePtr = std::shared_ptr<GLTexture>;

struct GLESMesh {
    ~GLESMesh();
    GLESMesh(const GLESMesh &) = delete;
    GLESMesh &operator=(const GLESMesh &) = delete;
    std::size_t count{};

  private:
    friend class GLESRenderer;
    GLESMesh() = default;
    GLuint buffer_{}, vao_{};
    std::shared_ptr<GLContextLifetime> lifetime_;
};

struct GLESDraw {
    std::shared_ptr<GLESMesh> mesh;
    GLTexturePtr texture;
    Matrix4 matrix{};
    std::array<float, 4> tint{{1, 1, 1, 1}};
    bool depth_write{true};
    std::array<float, 2> uv_offset{};
    bool screen_space{};
    bool nearest{};
};

class GLESRenderer {
  public:
    GLESRenderer();
    ~GLESRenderer();
    GLESRenderer(const GLESRenderer &) = delete;
    GLESRenderer &operator=(const GLESRenderer &) = delete;
    std::shared_ptr<GLESMesh> mesh(const std::vector<SceneVertex> &);
    GLTexturePtr texture(const RgbaImage &);
    // The caller owns framebuffer, viewport and clear. Does not swap buffers.
    void encode(const std::vector<GLESDraw> &);
    // Translate the iOS four-tap supersampling resolve to the native framebuffer.
    void resolve_supersampling(GLuint color, int output_width, int output_height);
    void abandon_context() noexcept;

  private:
    std::shared_ptr<GLContextLifetime> lifetime_;
    GLuint program_{}, linear_sampler_{}, nearest_sampler_{};
    GLuint resolve_program_{}, resolve_vao_{};
    GLint resolve_footprint_{-1};
    GLint matrix_{-1}, tint_{-1}, offset_{-1};
};

Matrix4 identity_matrix() noexcept;
Matrix4 multiply_matrix(const Matrix4 &, const Matrix4 &) noexcept;
} // namespace g1
