#include "GLESModelRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <sstream>
#include <tuple>

namespace g1 {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw FormatError(message);
}
template <std::size_t N> bool finite(const std::array<float, N> &value) {
    return std::all_of(value.begin(), value.end(), [](float x) { return std::isfinite(x); });
}
struct alignas(16) Uniform {
    M3GMatrix matrix;
    std::array<float, 4> blend, state;
    std::array<std::uint32_t, 4> flags;
};
static_assert(sizeof(Uniform) == 112 && offsetof(Uniform, blend) == 64 &&
                  offsetof(Uniform, state) == 80 && offsetof(Uniform, flags) == 96,
              "M3G Metal uniform layout changed");
static_assert(sizeof(M3GVertex) == 64 && offsetof(M3GVertex, normal) == 16 &&
                  offsetof(M3GVertex, color) == 32 && offsetof(M3GVertex, texcoord) == 48,
              "M3G Metal vertex layout changed");

// State and geometry admission checks are retained from the iOS renderer.
void validate(const M3GStaticModel &model, bool allow_empty = false) {
    require(allow_empty || (model.has_geometry && !model.surfaces.empty()),
            "M3G upload requires actual rigid geometry");
    require(model.surfaces.empty() != model.has_geometry,
            "M3G geometry flag differs from surface inventory");
    require(model.surfaces.size() <= 65536, "M3G surface count exceeds renderer bound");
    std::size_t vertices = 0;
    for (const auto &surface : model.surfaces) {
        const auto &a = surface.appearance;
        require(!surface.vertices.empty() && surface.vertices.size() % 3 == 0,
                "M3G surface is not an expanded triangle list");
        require(surface.vertices.size() <= 3000000 && vertices <= 3000000 - surface.vertices.size(),
                "M3G model exceeds bounded GPU vertex allocation");
        vertices += surface.vertices.size();
        require(finite(surface.model) && std::isfinite(surface.effective_alpha) &&
                    surface.effective_alpha >= 0 && surface.effective_alpha <= 1,
                "invalid M3G surface transform or alpha");
        require(!a.material,
                "M3G material lighting is not implemented by the rigid unlit renderer");
        require(a.blending >= 64 && a.blending <= 68, "unsupported M3G compositing mode");
        require(a.culling >= 160 && a.culling <= 162 && (a.shading == 164 || a.shading == 165) &&
                    (a.winding == 168 || a.winding == 169),
                "unsupported M3G polygon state");
        require(std::isfinite(a.alpha_threshold) && a.alpha_threshold >= 0 &&
                    a.alpha_threshold <= 1,
                "invalid M3G alpha-test threshold");
        require(a.depth_offset_factor == 0 && a.depth_offset_units == 0,
                "M3G nonzero polygon depth offset is outside this renderer slice");
        if (a.texture) {
            const auto &t = *a.texture;
            require(t.image_format >= 96 && t.image_format <= 100 && t.function >= 224 &&
                        t.function <= 228,
                    "unsupported M3G texture source or function");
            require(t.function != 225 || t.image_format == 99 || t.image_format == 100,
                    "M3G DECAL is undefined for this source image format");
            require((t.wrap_s == 240 || t.wrap_s == 241) && (t.wrap_t == 240 || t.wrap_t == 241),
                    "unsupported M3G texture wrapping");
            require(t.level_filter == 208 && (t.image_filter == 209 || t.image_filter == 210),
                    "M3G mipmapped texture levels are outside this renderer slice");
            require(finite(t.blend_color), "nonfinite M3G texture blend color");
            require(model.images.count(t.image_object) != 0,
                    "M3G texture image is absent from decoded model");
        }
        for (const auto &v : surface.vertices)
            require(finite(v.position) && finite(v.normal) && finite(v.color) && finite(v.texcoord),
                    "nonfinite M3G vertex attribute");
    }
    for (const auto &entry : model.images) {
        const auto &image = entry.second;
        require(image.width > 0 && image.height > 0 && image.width <= 8192 &&
                    image.height <= 8192 &&
                    image.pixels.size() == std::size_t(image.width) * image.height * 4,
                "invalid M3G decoded RGBA image");
    }
}

constexpr const char *vertex_shader = R"GLSL(#version 300 es
precision highp float;
layout(location=0) in vec4 aPosition;
layout(location=1) in vec4 aColor;
layout(location=2) in vec4 aTexcoord;
uniform mat4 uMatrix;
uniform vec4 uState;
out vec4 vColor;
out vec4 vTexcoord;
void main() {
    vec4 p=uMatrix*aPosition;
    p.z=2.0*p.z-p.w;
    gl_Position=p;
    vColor=aColor;
    vColor.a*=uState.x;
    vTexcoord=aTexcoord;
}
)GLSL";
constexpr const char *fragment_shader = R"GLSL(#version 300 es
precision highp float;
precision highp int;
in vec4 vColor;
in vec4 vTexcoord;
uniform vec4 uBlend;
uniform vec4 uState;
uniform uvec4 uFlags;
uniform sampler2D uImage;
uniform float uViewportTop;
out vec4 outColor;
void main() {
    if(uState.y!=0.0) {
        const uint bayer[16]=uint[16](0u,8u,2u,10u,12u,4u,14u,6u,3u,11u,1u,9u,15u,7u,13u,5u);
        // Metal's fragment position has a top-left origin; GLES has bottom-left.
        uvec2 p=uvec2(gl_FragCoord.x,uViewportTop-gl_FragCoord.y);
        float cut=(float(bayer[(p.y&3u)*4u+(p.x&3u)])+0.5)/16.0;
        bool vehicle=uState.y>1.5;
        if(vehicle ? cut>=uState.z : cut<uState.z)discard;
    }
    vec4 color=vColor;
    if(uFlags.z!=0u) {
        vec4 tex=texture(uImage,vTexcoord.xy/vTexcoord.w);
        uint format=uFlags.y;
        bool rgb=format!=96u;
        bool alpha=format==96u || format==98u || format==100u;
        switch(uFlags.x) {
        case 224u:
            if(rgb)color.rgb=color.rgb*(1.0-tex.rgb)+uBlend.rgb*tex.rgb;
            if(alpha)color.a*=tex.a;
            break;
        case 225u:
            color.rgb=format==99u ? tex.rgb : mix(color.rgb,tex.rgb,tex.a);
            break;
        case 226u:
            if(rgb)color.rgb+=tex.rgb;
            if(alpha)color.a*=tex.a;
            break;
        case 227u:
            if(rgb)color.rgb*=tex.rgb;
            if(alpha)color.a*=tex.a;
            break;
        case 228u:
            if(rgb)color.rgb=tex.rgb;
            if(alpha)color.a=tex.a;
            break;
        }
    }
    if(uState.y!=0.0)color.rgb=mix(color.rgb,vec3(0.65,0.88,1.0),uState.w);
    color=clamp(color,0.0,1.0);
    if(color.a<uBlend.a)discard;
    outColor=color;
}
)GLSL";

void check_gl(const char *operation) {
    const GLenum error = glGetError();
    if (error == 0x0507)
        throw GLContextLost(operation);
    if (error != GL_NO_ERROR) {
        std::ostringstream message;
        message << operation << " failed (GLES 0x" << std::hex << error << ")";
        throw std::runtime_error(message.str());
    }
}
GLuint compile(GLenum type, const char *source) {
    const GLuint result = glCreateShader(type);
    if (!result)
        throw std::runtime_error("M3G GLES shader allocation failed");
    glShaderSource(result, 1, &source, nullptr);
    glCompileShader(result);
    GLint success = 0;
    glGetShaderiv(result, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint size = 0;
        glGetShaderiv(result, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
        glGetShaderInfoLog(result, size, nullptr, log.data());
        glDeleteShader(result);
        throw std::runtime_error("M3G GLES shader compilation failed: " + log);
    }
    return result;
}
GLuint make_program() {
    GLuint vertex = 0, fragment = 0, result = 0;
    try {
        vertex = compile(GL_VERTEX_SHADER, vertex_shader);
        fragment = compile(GL_FRAGMENT_SHADER, fragment_shader);
        result = glCreateProgram();
        if (!result)
            throw std::runtime_error("M3G GLES program allocation failed");
        glAttachShader(result, vertex);
        glAttachShader(result, fragment);
        glLinkProgram(result);
        GLint success = 0;
        glGetProgramiv(result, GL_LINK_STATUS, &success);
        if (!success) {
            GLint size = 0;
            glGetProgramiv(result, GL_INFO_LOG_LENGTH, &size);
            std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
            glGetProgramInfoLog(result, size, nullptr, log.data());
            throw std::runtime_error("M3G GLES program link failed: " + log);
        }
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return result;
    } catch (...) {
        if (vertex)
            glDeleteShader(vertex);
        if (fragment)
            glDeleteShader(fragment);
        if (result)
            glDeleteProgram(result);
        throw;
    }
}

struct VertexBuffer {
    GLuint vao{}, buffer{};
    std::shared_ptr<GLContextLifetime> lifetime;
    ~VertexBuffer() {
        if (lifetime && lifetime->active) {
            if (vao)
                glDeleteVertexArrays(1, &vao);
            if (buffer)
                glDeleteBuffers(1, &buffer);
        }
    }
};
std::shared_ptr<VertexBuffer> vertex_buffer(const std::vector<M3GVertex> &input, bool flat,
                                            const std::shared_ptr<GLContextLifetime> &lifetime) {
    auto result = std::make_shared<VertexBuffer>();
    result->lifetime = lifetime;
    // JSR184 flat shading uses the third expanded triangle vertex. Duplicating
    // that color at upload preserves it on GLES without a vendor-specific
    // provoking-vertex extension; the immutable source asset is unchanged.
    std::vector<M3GVertex> flattened;
    const std::vector<M3GVertex> *vertices = &input;
    if (flat) {
        flattened = input;
        for (std::size_t i = 0; i < flattened.size(); i += 3)
            flattened[i].color = flattened[i + 1].color = flattened[i + 2].color;
        vertices = &flattened;
    }
    glGenVertexArrays(1, &result->vao);
    glGenBuffers(1, &result->buffer);
    if (!result->vao || !result->buffer)
        throw std::runtime_error("M3G GLES vertex allocation failed");
    glBindVertexArray(result->vao);
    glBindBuffer(GL_ARRAY_BUFFER, result->buffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices->size() * sizeof(M3GVertex)),
                 vertices->data(), GL_STATIC_DRAW);
    for (GLuint i = 0; i < 3; ++i)
        glEnableVertexAttribArray(i);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(M3GVertex), nullptr);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(M3GVertex),
                          reinterpret_cast<const void *>(offsetof(M3GVertex, color)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(M3GVertex),
                          reinterpret_cast<const void *>(offsetof(M3GVertex, texcoord)));
    glBindVertexArray(0);
    check_gl("M3G GLES vertex upload");
    return result;
}
using SamplerKey = std::tuple<std::uint8_t, std::uint8_t, std::uint8_t>;

void appearance_state(const M3GAppearanceState &a) {
    glColorMask(a.color_write, a.color_write, a.color_write, a.alpha_write);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(a.depth_test ? GL_LEQUAL : GL_ALWAYS);
    glDepthMask(a.depth_write);
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (a.culling == 162)
        glDisable(GL_CULL_FACE);
    else {
        glEnable(GL_CULL_FACE);
        glCullFace(a.culling == 160 ? GL_BACK : GL_FRONT);
    }
    glFrontFace(a.winding == 168 ? GL_CCW : GL_CW);
    if (a.blending == 68)
        glDisable(GL_BLEND);
    else
        glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    switch (a.blending) {
    case 64:
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA,
                            GL_ONE_MINUS_SRC_ALPHA);
        break;
    case 65:
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
        break;
    case 66:
        glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_DST_ALPHA, GL_ZERO);
        break;
    case 67:
        glBlendFuncSeparate(GL_DST_COLOR, GL_SRC_COLOR, GL_DST_ALPHA, GL_SRC_ALPHA);
        break;
    case 68:
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
        break;
    }
}
} // namespace

struct GLESModel::Impl {
    struct Surface {
        std::size_t mesh_object{}, submesh_index{};
        M3GMatrix authored_matrix;
        M3GAppearanceState appearance;
        float effective_alpha{};
        std::shared_ptr<VertexBuffer> vertices;
        GLTexturePtr texture;
        GLuint sampler{};
        std::size_t count{};
    };
    struct Sprite {
        std::size_t node_object{};
        std::uint8_t image_format{};
        M3GAppearanceState appearance;
        GLTexturePtr texture;
        GLuint sampler{};
    };
    std::shared_ptr<GLContextLifetime> lifetime;
    std::shared_ptr<const M3GRenderAsset> source;
    bool legacy_static{};
    std::vector<Surface> surfaces;
    std::vector<Sprite> sprites;
    std::map<std::size_t, GLTexturePtr> images;
    std::size_t vertex_count{}, enhanced_textures{};
};

struct GLESModelRenderer::Impl {
    std::shared_ptr<GLContextLifetime> lifetime = std::make_shared<GLContextLifetime>();
    GLuint program{};
    GLTexturePtr white;
    std::map<SamplerKey, GLuint> samplers;
    std::shared_ptr<VertexBuffer> sprite_buffer;
    GLint matrix{-1}, blend{-1}, state{-1}, flags{-1}, viewport_top{-1};
    CleanTextureLoader clean_loader;
    ~Impl() {
        if (lifetime->active) {
            white.reset();
            sprite_buffer.reset();
            for (const auto &entry : samplers)
                glDeleteSamplers(1, &entry.second);
            if (program)
                glDeleteProgram(program);
            lifetime->active = false;
        }
    }
    GLTexturePtr texture(const RgbaImage &image) {
        require(image.width && image.height && image.width <= 8192 && image.height <= 8192 &&
                    image.pixels.size() == std::size_t(image.width) * image.height * 4,
                "invalid M3G texture payload");
        GLint max_size = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
        require(image.width <= static_cast<unsigned>(max_size) &&
                    image.height <= static_cast<unsigned>(max_size),
                "M3G image exceeds this device's texture limit");
        GLuint name = 0;
        glGenTextures(1, &name);
        if (!name)
            throw std::runtime_error("M3G GLES texture allocation failed");
        GLTexturePtr result(new GLTexture(name, image.width, image.height, lifetime));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, name);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, image.pixels.data());
        check_gl("M3G GLES texture upload");
        return result;
    }
    GLuint sampler(const std::optional<M3GTextureState> &source) {
        const SamplerKey key =
            source ? SamplerKey{source->image_filter, source->wrap_s, source->wrap_t}
                   : SamplerKey{210, 240, 240};
        const auto found = samplers.find(key);
        if (found != samplers.end())
            return found->second;
        GLuint result = 0;
        glGenSamplers(1, &result);
        if (!result)
            throw std::runtime_error("M3G GLES sampler allocation failed");
        try {
            const auto filter = std::get<0>(key) == 209 ? GL_LINEAR : GL_NEAREST;
            glSamplerParameteri(result, GL_TEXTURE_MIN_FILTER, filter);
            glSamplerParameteri(result, GL_TEXTURE_MAG_FILTER, filter);
            glSamplerParameteri(result, GL_TEXTURE_WRAP_S,
                                std::get<1>(key) == 240 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            glSamplerParameteri(result, GL_TEXTURE_WRAP_T,
                                std::get<2>(key) == 240 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            check_gl("M3G GLES sampler creation");
            samplers.emplace(key, result);
            return result;
        } catch (...) {
            glDeleteSamplers(1, &result);
            throw;
        }
    }
    void begin() {
        require(lifetime->active, "M3G GLES context has been abandoned");
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        glUniform1f(viewport_top, static_cast<float>(viewport[1] + viewport[3]));
    }
    void uniform(const Uniform &u) {
        glUniformMatrix4fv(matrix, 1, GL_FALSE, u.matrix.data());
        glUniform4fv(blend, 1, u.blend.data());
        glUniform4fv(state, 1, u.state.data());
        glUniform4uiv(flags, 1, u.flags.data());
    }
};

std::size_t GLESModel::surface_count() const noexcept {
    return impl_ ? impl_->surfaces.size() : 0;
}
std::size_t GLESModel::vertex_count() const noexcept {
    return impl_ ? impl_->vertex_count : 0;
}
std::size_t GLESModel::texture_count() const noexcept {
    return impl_ ? impl_->images.size() : 0;
}
std::size_t GLESModel::sprite_count() const noexcept {
    return impl_ ? impl_->sprites.size() : 0;
}
std::size_t GLESModel::enhanced_texture_count() const noexcept {
    return impl_ ? impl_->enhanced_textures : 0;
}
const M3GRenderAsset &GLESModel::asset() const {
    require(impl_ && impl_->source, "M3G model has no uploaded source asset");
    return *impl_->source;
}
GLESModelRenderer::GLESModelRenderer() : impl_(std::make_unique<Impl>()) {
    impl_->program = make_program();
    impl_->matrix = glGetUniformLocation(impl_->program, "uMatrix");
    impl_->blend = glGetUniformLocation(impl_->program, "uBlend");
    impl_->state = glGetUniformLocation(impl_->program, "uState");
    impl_->flags = glGetUniformLocation(impl_->program, "uFlags");
    impl_->viewport_top = glGetUniformLocation(impl_->program, "uViewportTop");
    require(impl_->matrix >= 0 && impl_->blend >= 0 && impl_->state >= 0 && impl_->flags >= 0 &&
                impl_->viewport_top >= 0,
            "M3G GLES uniforms are missing");
    glUseProgram(impl_->program);
    glUniform1i(glGetUniformLocation(impl_->program, "uImage"), 0);
    impl_->white = impl_->texture(RgbaImage{1, 1, {255, 255, 255, 255}});
    impl_->sprite_buffer = vertex_buffer(std::vector<M3GVertex>(6), false, impl_->lifetime);
    check_gl("M3G GLES renderer initialization");
}
GLESModelRenderer::~GLESModelRenderer() = default;
void GLESModelRenderer::abandon_context() noexcept {
    impl_->lifetime->active = false;
}
void GLESModelRenderer::set_clean_texture_loader(CleanTextureLoader loader) {
    impl_->clean_loader = std::move(loader);
}

std::shared_ptr<GLESModel> GLESModelRenderer::upload(const M3GStaticModel &model) {
    validate(model);
    M3GRenderAsset asset;
    asset.geometry = model;
    for (std::size_t i = 0; i < model.surfaces.size(); ++i)
        asset.traversal.push_back({M3GDrawableKind::mesh, i});
    return upload_asset(asset, true);
}
std::shared_ptr<GLESModel> GLESModelRenderer::upload(const M3GRenderAsset &asset) {
    return upload_asset(asset, false);
}
std::shared_ptr<GLESModel> GLESModelRenderer::upload_modern(const M3GRenderAsset &asset,
                                                            MaterialEnhancement quality) {
    return upload_asset(asset, false, quality);
}

std::shared_ptr<GLESModel> GLESModelRenderer::upload_asset(const M3GRenderAsset &asset,
                                                           bool legacy_static,
                                                           MaterialEnhancement quality) {
    require(impl_->lifetime->active, "M3G GLES context has been abandoned");
    const auto &model = asset.geometry;
    validate(model, !asset.sprites.empty());
    require(asset.sprites.size() <= 65536, "M3G Sprite3D count exceeds renderer bound");
    for (const auto &sprite : asset.sprites) {
        const auto &a = sprite.appearance;
        require(finite(sprite.model) && std::isfinite(sprite.effective_alpha) &&
                    sprite.effective_alpha >= 0 && sprite.effective_alpha <= 1,
                "invalid M3G Sprite3D transform or alpha");
        require(sprite.image_format >= 96 && sprite.image_format <= 100 &&
                    model.images.count(sprite.image_object),
                "M3G Sprite3D image or format is absent");
        const auto &image = model.images.at(sprite.image_object);
        require(image.width <= 1024 && image.height <= 1024 && !(image.width & (image.width - 1)) &&
                    !(image.height & (image.height - 1)),
                "M3G Sprite3D requires a single power-of-two image block");
        require(!a.material && !a.texture && a.culling == 162,
                "M3G Sprite3D appearance is outside the recovered unlit path");
        require(a.blending >= 64 && a.blending <= 68 && std::isfinite(a.alpha_threshold) &&
                    a.alpha_threshold >= 0 && a.alpha_threshold <= 1 &&
                    a.depth_offset_factor == 0 && a.depth_offset_units == 0,
                "unsupported M3G Sprite3D compositing state");
        require(std::abs(std::int64_t(sprite.crop_pixels[2])) <= 1024 &&
                    std::abs(std::int64_t(sprite.crop_pixels[3])) <= 1024,
                "M3G Sprite3D crop dimension exceeds original bound");
    }
    for (const auto &reference : asset.traversal)
        require(reference.kind == M3GDrawableKind::mesh
                    ? reference.index < model.surfaces.size()
                    : reference.kind == M3GDrawableKind::sprite &&
                          reference.index < asset.sprites.size(),
                "M3G upload drawable reference is invalid");
    auto gpu = std::make_shared<GLESModel::Impl>();
    gpu->lifetime = impl_->lifetime;
    gpu->source = std::make_shared<const M3GRenderAsset>(asset);
    gpu->legacy_static = legacy_static;
    std::size_t enhanced_bytes = 0;
    for (const auto &entry : model.images) {
        const auto &image = entry.second;
        const std::size_t derived_bytes = std::size_t(image.width) * image.height * 16;
        bool upgraded = false;
        if (quality != MaterialEnhancement::original && image.width <= 1024 &&
            image.height <= 1024 && derived_bytes <= 12 * 1024 * 1024 - enhanced_bytes) {
            try {
                const auto cleaned = impl_->clean_loader ? impl_->clean_loader(image, "material", 2)
                                                         : std::optional<RgbaImage>{};
                const auto enhanced = cleaned ? *cleaned : enhance_material(image, quality);
                require(enhanced.width == image.width * 2 && enhanced.height == image.height * 2,
                        "M3G cleaned material dimensions differ");
                gpu->images.emplace(entry.first, impl_->texture(enhanced));
                enhanced_bytes += derived_bytes;
                ++gpu->enhanced_textures;
                upgraded = true;
            } catch (const std::exception &) { /* Same optional pack fallback as iOS. */
            }
        }
        if (!upgraded)
            gpu->images.emplace(entry.first, impl_->texture(image));
    }
    gpu->surfaces.reserve(model.surfaces.size());
    for (const auto &s : model.surfaces) {
        GLESModel::Impl::Surface out;
        out.mesh_object = s.mesh_object;
        out.submesh_index = s.submesh_index;
        out.authored_matrix = s.model;
        out.appearance = s.appearance;
        out.effective_alpha = s.effective_alpha;
        out.count = s.vertices.size();
        out.vertices = vertex_buffer(s.vertices, s.appearance.shading == 164, impl_->lifetime);
        out.texture = s.appearance.texture ? gpu->images.at(s.appearance.texture->image_object)
                                           : impl_->white;
        auto sampled = s.appearance.texture;
        if (quality != MaterialEnhancement::original && sampled)
            sampled->image_filter = 209;
        out.sampler = impl_->sampler(sampled);
        gpu->vertex_count += s.vertices.size();
        gpu->surfaces.push_back(std::move(out));
    }
    gpu->sprites.reserve(asset.sprites.size());
    for (const auto &s : asset.sprites) {
        GLESModel::Impl::Sprite out;
        out.node_object = s.node_object;
        out.image_format = s.image_format;
        out.appearance = s.appearance;
        out.texture = gpu->images.at(s.image_object);
        std::optional<M3GTextureState> sampled;
        if (quality != MaterialEnhancement::original) {
            sampled = M3GTextureState{};
            sampled->image_filter = 209;
            sampled->wrap_s = 240;
            sampled->wrap_t = 240;
        }
        out.sampler = impl_->sampler(sampled);
        gpu->sprites.push_back(std::move(out));
    }
    auto result = std::make_shared<GLESModel>();
    result->impl_ = std::move(gpu);
    return result;
}

void GLESModelRenderer::encode(const GLESModel &model, const M3GMatrix &view_projection,
                               const std::map<std::size_t, M3GMatrix> *pose) {
    require(impl_->lifetime->active && model.impl_,
            "M3G draw requires an active context and uploaded model");
    const auto &gpu = *model.impl_;
    require(gpu.legacy_static,
            "stateful M3G render assets require encode_frame with current node state");
    require(gpu.lifetime == impl_->lifetime, "M3G model belongs to a different GLES renderer");
    require(finite(view_projection), "nonfinite M3G camera matrix");
    std::vector<Uniform> uniforms;
    uniforms.reserve(gpu.surfaces.size());
    for (const auto &s : gpu.surfaces) {
        const auto &a = s.appearance;
        const M3GMatrix *transform = &s.authored_matrix;
        if (pose) {
            const auto found = pose->find(s.mesh_object);
            require(found != pose->end() && finite(found->second),
                    "M3G pose omits a drawn mesh or contains a nonfinite matrix");
            transform = &found->second;
        }
        Uniform u{};
        u.matrix = m3g_multiply(view_projection, *transform);
        require(finite(u.matrix), "M3G camera/model product is nonfinite");
        u.blend[3] = a.alpha_threshold;
        u.state[0] = s.effective_alpha;
        u.flags[3] = a.shading == 164 ? 1U : 0U;
        if (a.texture) {
            std::copy(a.texture->blend_color.begin(), a.texture->blend_color.end(),
                      u.blend.begin());
            u.flags[0] = a.texture->function;
            u.flags[1] = a.texture->image_format;
            u.flags[2] = 1;
        }
        uniforms.push_back(u);
    }
    std::vector<std::size_t> order(gpu.surfaces.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](const auto a, const auto b) {
        return gpu.surfaces[a].appearance.layer < gpu.surfaces[b].appearance.layer;
    });
    impl_->begin();
    for (const auto index : order) {
        const auto &s = gpu.surfaces[index];
        appearance_state(s.appearance);
        impl_->uniform(uniforms[index]);
        glBindTexture(GL_TEXTURE_2D, s.texture->id());
        glBindSampler(0, s.sampler);
        glBindVertexArray(s.vertices->vao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(s.count));
    }
    glBindVertexArray(0);
    check_gl("M3G GLES static draw");
}

void GLESModelRenderer::encode_frame(const std::vector<std::shared_ptr<GLESModel>> &models,
                                     const M3GPreparedFrame &frame) {
    require(impl_->lifetime->active && models.size() == frame.assets.size(),
            "M3G frame model/source count differs or context lost");
    for (std::size_t i = 0; i < models.size(); ++i) {
        require(models[i] && models[i]->impl_, "M3G frame includes an unuploaded model");
        const auto &gpu = *models[i]->impl_;
        require(gpu.lifetime == impl_->lifetime,
                "M3G frame model belongs to another GLES renderer");
        require(gpu.source.get() == frame.assets[i],
                "M3G frame was prepared for different source geometry");
    }
    std::vector<Uniform> uniforms;
    uniforms.reserve(frame.draws.size());
    for (const auto &draw : frame.draws) {
        require(draw.instance < models.size(), "M3G frame draw instance is absent");
        const auto &gpu = *models[draw.instance]->impl_;
        require(finite(draw.metal_matrix) && std::isfinite(draw.effective_alpha) &&
                    draw.effective_alpha >= 0 && draw.effective_alpha <= 1,
                "invalid M3G prepared frame transform or alpha");
        require(draw.assembly_role <= 2 && std::isfinite(draw.assembly_mix) &&
                    draw.assembly_mix >= 0 && draw.assembly_mix <= 1 &&
                    std::isfinite(draw.assembly_highlight) && draw.assembly_highlight >= 0 &&
                    draw.assembly_highlight <= 0.35F,
                "invalid procedural assembly coverage");
        Uniform u{};
        u.matrix = draw.metal_matrix;
        u.state = {{draw.effective_alpha, float(draw.assembly_role), draw.assembly_mix,
                    draw.assembly_highlight}};
        if (draw.kind == M3GDrawableKind::mesh) {
            require(draw.geometry_index < gpu.surfaces.size(), "M3G frame mesh index is invalid");
            const auto &s = gpu.surfaces[draw.geometry_index];
            require(s.mesh_object == draw.node_object, "M3G frame mesh identity differs");
            const auto &a = s.appearance;
            u.blend[3] = a.alpha_threshold;
            u.flags[3] = a.shading == 164 ? 1U : 0U;
            if (a.texture) {
                std::copy(a.texture->blend_color.begin(), a.texture->blend_color.end(),
                          u.blend.begin());
                u.flags[0] = a.texture->function;
                u.flags[1] = a.texture->image_format;
                u.flags[2] = 1;
            }
        } else {
            require(draw.kind == M3GDrawableKind::sprite &&
                        draw.geometry_index < gpu.sprites.size(),
                    "M3G frame Sprite3D index is invalid");
            const auto &s = gpu.sprites[draw.geometry_index];
            require(s.node_object == draw.node_object, "M3G frame Sprite3D identity differs");
            u.blend[3] = s.appearance.alpha_threshold;
            u.flags[0] = draw.effective_alpha >= 1 || s.image_format == 99 ? 228U : 227U;
            u.flags[1] = s.image_format;
            u.flags[2] = 1;
            if (draw.sprite_projection.drawn())
                for (const auto &v : draw.sprite_vertices)
                    require(finite(v.position) && finite(v.color) && finite(v.texcoord),
                            "nonfinite M3G projected Sprite3D vertex");
        }
        uniforms.push_back(u);
    }
    impl_->begin();
    for (std::size_t i = 0; i < frame.draws.size(); ++i) {
        const auto &draw = frame.draws[i];
        if (!draw.draws_geometry())
            continue;
        const auto &gpu = *models[draw.instance]->impl_;
        impl_->uniform(uniforms[i]);
        if (draw.kind == M3GDrawableKind::mesh) {
            const auto &s = gpu.surfaces[draw.geometry_index];
            appearance_state(s.appearance);
            glBindTexture(GL_TEXTURE_2D, s.texture->id());
            glBindSampler(0, s.sampler);
            glBindVertexArray(s.vertices->vao);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(s.count));
        } else {
            const auto &s = gpu.sprites[draw.geometry_index];
            appearance_state(s.appearance);
            glDisable(GL_CULL_FACE);
            glFrontFace(GL_CCW);
            glBindTexture(GL_TEXTURE_2D, s.texture->id());
            glBindSampler(0, s.sampler);
            glBindVertexArray(impl_->sprite_buffer->vao);
            glBindBuffer(GL_ARRAY_BUFFER, impl_->sprite_buffer->buffer);
            glBufferData(GL_ARRAY_BUFFER, sizeof(draw.sprite_vertices), draw.sprite_vertices.data(),
                         GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(draw.sprite_vertices.size()));
        }
    }
    glBindVertexArray(0);
    check_gl("M3G GLES prepared frame draw");
}
} // namespace g1
