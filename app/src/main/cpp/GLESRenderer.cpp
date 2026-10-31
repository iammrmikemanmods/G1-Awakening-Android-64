#include "GLESRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>

namespace g1 {
namespace {
constexpr const char *vertex_shader = R"GLSL(#version 300 es
precision highp float;
layout(location=0) in vec4 aPosition;
layout(location=1) in vec2 aUV;
uniform mat4 uMatrix;
uniform vec2 uOffset;
out vec2 vUV;
void main() {
    vec4 p=uMatrix*aPosition;
    // The source host and portable frame planner supply depth in [0,w].
    // OpenGL ES clips in [-w,w]; preserve source depth after this conversion.
    p.z=2.0*p.z-p.w;
    gl_Position=p;
    vUV=aUV+uOffset;
}
)GLSL";
constexpr const char *fragment_shader = R"GLSL(#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uImage;
uniform vec4 uTint;
out vec4 outColor;
void main() {
    vec4 color=texture(uImage,vUV)*uTint;
    if(color.a<=0.0039215686)discard;
    outColor=color;
}
)GLSL";

constexpr const char *resolve_vertex_shader = R"GLSL(#version 300 es
precision highp float;
out vec2 vUV;
void main() {
    vec2 p=gl_VertexID==0?vec2(-1,-1):(gl_VertexID==1?vec2(3,-1):vec2(-1,3));
    gl_Position=vec4(p,0,1);
    // Both the source FBO and destination use GLES bottom-left texture coordinates.
    vUV=(p+1.0)*0.5;
}
)GLSL";
constexpr const char *resolve_fragment_shader = R"GLSL(#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uImage;
uniform vec2 uFootprint;
out vec4 outColor;
void main() {
    vec2 d=uFootprint*0.25;
    outColor=(texture(uImage,vUV+vec2(-d.x,-d.y))+
              texture(uImage,vUV+vec2( d.x,-d.y))+
              texture(uImage,vUV+vec2(-d.x, d.y))+
              texture(uImage,vUV+vec2( d.x, d.y)))*0.25;
}
)GLSL";

void check_gl(const char *operation) {
    const auto error = glGetError();
    if (error == 0x0507) // GL_CONTEXT_LOST, also on GLES3 via robustness extensions.
        throw GLContextLost(operation);
    if (error != GL_NO_ERROR) {
        std::ostringstream message;
        message << operation << " failed (GLES 0x" << std::hex << error << ")";
        throw std::runtime_error(message.str());
    }
}

GLuint compile(GLenum type, const char *source) {
    const GLuint shader = glCreateShader(type);
    if (!shader)
        throw std::runtime_error("GLES shader allocation failed");
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint succeeded = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &succeeded);
    if (!succeeded) {
        GLint size = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
        glGetShaderInfoLog(shader, size, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("GLES shader compilation failed: " + log);
    }
    return shader;
}
GLuint program(const char *vertex_source = vertex_shader,
               const char *fragment_source = fragment_shader) {
    GLuint vertex = 0, fragment = 0, result = 0;
    try {
        vertex = compile(GL_VERTEX_SHADER, vertex_source);
        fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
        result = glCreateProgram();
        if (!result)
            throw std::runtime_error("GLES program allocation failed");
        glAttachShader(result, vertex);
        glAttachShader(result, fragment);
        glLinkProgram(result);
        GLint succeeded = 0;
        glGetProgramiv(result, GL_LINK_STATUS, &succeeded);
        if (!succeeded) {
            GLint size = 0;
            glGetProgramiv(result, GL_INFO_LOG_LENGTH, &size);
            std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
            glGetProgramInfoLog(result, size, nullptr, log.data());
            throw std::runtime_error("GLES program link failed: " + log);
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
} // namespace

Matrix4 identity_matrix() noexcept {
    return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
}
Matrix4 multiply_matrix(const Matrix4 &a, const Matrix4 &b) noexcept {
    Matrix4 result{};
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t k = 0; k < 4; ++k)
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return result;
}

GLTexture::~GLTexture() {
    if (live() && id_)
        glDeleteTextures(1, &id_);
}
GLESMesh::~GLESMesh() {
    if (lifetime_ && lifetime_->active) {
        if (vao_)
            glDeleteVertexArrays(1, &vao_);
        if (buffer_)
            glDeleteBuffers(1, &buffer_);
    }
}

GLESRenderer::GLESRenderer() : lifetime_(std::make_shared<GLContextLifetime>()) {
    try {
        program_ = program();
        matrix_ = glGetUniformLocation(program_, "uMatrix");
        tint_ = glGetUniformLocation(program_, "uTint");
        offset_ = glGetUniformLocation(program_, "uOffset");
        if (matrix_ < 0 || tint_ < 0 || offset_ < 0)
            throw std::runtime_error("GLES scene uniforms are missing");
        glUseProgram(program_);
        glUniform1i(glGetUniformLocation(program_, "uImage"), 0);
        glGenSamplers(1, &linear_sampler_);
        glGenSamplers(1, &nearest_sampler_);
        if (!linear_sampler_ || !nearest_sampler_)
            throw std::runtime_error("GLES sampler allocation failed");
        for (GLuint sampler : {linear_sampler_, nearest_sampler_}) {
            const GLint filter = sampler == linear_sampler_ ? GL_LINEAR : GL_NEAREST;
            glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, filter);
            glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, filter);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        check_gl("GLES scene renderer initialization");
    } catch (...) {
        if (program_)
            glDeleteProgram(program_);
        if (linear_sampler_)
            glDeleteSamplers(1, &linear_sampler_);
        if (nearest_sampler_)
            glDeleteSamplers(1, &nearest_sampler_);
        lifetime_->active = false;
        throw;
    }
}
GLESRenderer::~GLESRenderer() {
    if (lifetime_->active) {
        if (resolve_program_)
            glDeleteProgram(resolve_program_);
        if (resolve_vao_)
            glDeleteVertexArrays(1, &resolve_vao_);
        if (program_)
            glDeleteProgram(program_);
        if (linear_sampler_)
            glDeleteSamplers(1, &linear_sampler_);
        if (nearest_sampler_)
            glDeleteSamplers(1, &nearest_sampler_);
        lifetime_->active = false;
    }
}
void GLESRenderer::abandon_context() noexcept {
    lifetime_->active = false;
}

void GLESRenderer::resolve_supersampling(GLuint color, int width, int height) {
    if (!lifetime_->active)
        throw std::runtime_error("GLES context has been abandoned");
    if (!color || width <= 0 || height <= 0)
        throw FormatError("Invalid supersampling source or destination");
    if (!resolve_program_) {
        resolve_program_ = program(resolve_vertex_shader, resolve_fragment_shader);
        resolve_footprint_ = glGetUniformLocation(resolve_program_, "uFootprint");
        glGenVertexArrays(1, &resolve_vao_);
        if (resolve_footprint_ < 0 || !resolve_vao_)
            throw std::runtime_error("GLES supersampling resolve initialization failed");
        glUseProgram(resolve_program_);
        glUniform1i(glGetUniformLocation(resolve_program_, "uImage"), 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glUseProgram(resolve_program_);
    glUniform2f(resolve_footprint_, 1.0F / width, 1.0F / height);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color);
    glBindSampler(0, linear_sampler_);
    glBindVertexArray(resolve_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    check_gl("GLES four-tap supersampling resolve");
}

std::shared_ptr<GLESMesh> GLESRenderer::mesh(const std::vector<SceneVertex> &vertices) {
    if (!lifetime_->active)
        throw std::runtime_error("GLES context has been abandoned");
    if (vertices.empty())
        return {};
    if (vertices.size() % 3 || vertices.size() > 1'000'000)
        throw FormatError("native render mesh is not a bounded triangle list");
    auto result = std::shared_ptr<GLESMesh>(new GLESMesh());
    result->lifetime_ = lifetime_;
    result->count = vertices.size();
    glGenBuffers(1, &result->buffer_);
    glGenVertexArrays(1, &result->vao_);
    if (!result->buffer_ || !result->vao_)
        throw std::runtime_error("GLES scene buffer allocation failed");
    glBindVertexArray(result->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, result->buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(SceneVertex)),
                 vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(SceneVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                          reinterpret_cast<const void *>(offsetof(SceneVertex, uv)));
    glBindVertexArray(0);
    check_gl("GLES scene mesh upload");
    return result;
}

GLTexturePtr GLESRenderer::texture(const RgbaImage &image) {
    if (!lifetime_->active)
        throw std::runtime_error("GLES context has been abandoned");
    if (!image.width || !image.height || image.width > 8192 || image.height > 8192 ||
        image.pixels.size() != std::size_t(image.width) * image.height * 4)
        throw FormatError("invalid RGBA texture payload");
    GLint maximum = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if (image.width > static_cast<unsigned>(maximum) ||
        image.height > static_cast<unsigned>(maximum))
        throw FormatError("RGBA image exceeds this device's texture limit");
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture)
        throw std::runtime_error("GLES texture allocation failed");
    auto result = GLTexturePtr(new GLTexture(texture, image.width, image.height, lifetime_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(image.width),
                 static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 image.pixels.data());
    check_gl("GLES texture upload");
    return result;
}

void GLESRenderer::encode(const std::vector<GLESDraw> &draws) {
    if (!lifetime_->active)
        throw std::runtime_error("GLES context has been abandoned");
    // Reject stale/mismatched resources before drawing any part of the frame.
    for (const auto &draw : draws)
        if (draw.mesh && draw.mesh->count) {
            if (draw.mesh->lifetime_ != lifetime_ || !draw.texture || !draw.texture->live())
                throw std::runtime_error("GLES draw includes missing or stale resources");
            if (!std::all_of(draw.matrix.begin(), draw.matrix.end(),
                             [](float n) { return std::isfinite(n); }))
                throw FormatError("GLES scene matrix is nonfinite");
        }
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    for (const auto &draw : draws) {
        if (!draw.mesh || !draw.mesh->count)
            continue;
        if (draw.screen_space) {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
        } else {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(draw.depth_write ? GL_TRUE : GL_FALSE);
        }
        glUniformMatrix4fv(matrix_, 1, GL_FALSE, draw.matrix.data());
        glUniform4fv(tint_, 1, draw.tint.data());
        glUniform2fv(offset_, 1, draw.uv_offset.data());
        glBindSampler(0, draw.nearest ? nearest_sampler_ : linear_sampler_);
        glBindTexture(GL_TEXTURE_2D, draw.texture->id());
        glBindVertexArray(draw.mesh->vao_);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(draw.mesh->count));
    }
    glBindVertexArray(0);
    check_gl("GLES scene draw");
}
} // namespace g1
