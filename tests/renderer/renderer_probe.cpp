#include "GLESRenderer.hpp"
#include "GLESModelRenderer.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct EGLSession {
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLContext context{EGL_NO_CONTEXT};
    EGLSurface surface{EGL_NO_SURFACE};
    EGLSession() {
        const auto get_platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        display = get_platform
                      ? get_platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                      : eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major = 0, minor = 0;
        if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor))
            throw std::runtime_error("EGL initialization failed");
        if (!eglBindAPI(EGL_OPENGL_ES_API))
            throw std::runtime_error("EGL GLES bind failed");
        const EGLint attributes[] = {EGL_SURFACE_TYPE,
                                     EGL_PBUFFER_BIT,
                                     EGL_RENDERABLE_TYPE,
                                     EGL_OPENGL_ES3_BIT_KHR,
                                     EGL_RED_SIZE,
                                     8,
                                     EGL_GREEN_SIZE,
                                     8,
                                     EGL_BLUE_SIZE,
                                     8,
                                     EGL_ALPHA_SIZE,
                                     8,
                                     EGL_DEPTH_SIZE,
                                     24,
                                     EGL_NONE};
        EGLConfig config{};
        EGLint count = 0;
        if (!eglChooseConfig(display, attributes, &config, 1, &count) || count != 1)
            throw std::runtime_error("EGL ES3 config missing");
        const EGLint pbuffer[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, pbuffer);
        const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
        if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
            !eglMakeCurrent(display, surface, surface, context))
            throw std::runtime_error("EGL ES3 context creation failed");
        glViewport(0, 0, 64, 64);
        glDisable(GL_DITHER);
    }
    ~EGLSession() {
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT)
                eglDestroyContext(display, context);
            if (surface != EGL_NO_SURFACE)
                eglDestroySurface(display, surface);
            eglTerminate(display);
        }
    }
};
int checks = 0;
void expect(bool condition, const std::string &name) {
    if (!condition)
        throw std::runtime_error(name);
    ++checks;
}
void clear(std::array<float, 4> color = {0, 0, 0, 0}) {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(color[0], color[1], color[2], color[3]);
    glClearDepthf(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
std::array<unsigned char, 4> pixel(int x = 31, int y = 31) {
    std::array<unsigned char, 4> result{};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
}
void expect_pixel(std::array<float, 4> expected, const std::string &name, int tolerance = 2) {
    auto actual = pixel();
    for (std::size_t i = 0; i < 4; ++i) {
        const int target = int(std::lround(std::clamp(expected[i], 0.0F, 1.0F) * 255));
        if (std::abs(int(actual[i]) - target) > tolerance)
            throw std::runtime_error(name + " channel " + std::to_string(i) + ": " +
                                     std::to_string(actual[i]) + " expected " +
                                     std::to_string(target));
    }
    ++checks;
}
const auto identity = g1::identity_matrix();
std::vector<g1::M3GVertex> vertices(std::array<float, 4> color = {1, 0, 0, 1}, float z = .5F) {
    std::vector<g1::M3GVertex> result;
    for (const auto p : std::array<std::array<float, 2>, 6>{
             {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}}})
        result.push_back({{p[0], p[1], z, 1}, {0, 0, 1, 0}, color, {.5F, .5F, 0, 1}});
    return result;
}
g1::M3GStaticModel model(std::array<float, 4> color = {1, 0, 0, 1}) {
    g1::M3GStaticModel m;
    m.has_geometry = true;
    g1::M3GSurface surface;
    surface.mesh_object = 1;
    surface.model = identity;
    surface.vertices = vertices(color);
    m.surfaces.push_back(surface);
    return m;
}
void render(g1::GLESModelRenderer &renderer, const g1::M3GStaticModel &source) {
    auto gpu = renderer.upload(source);
    renderer.encode(*gpu, identity);
}

// CPU reference in source texel coordinates. The iOS resolve integrates four
// bilinear samples at the quarter/three-quarter points of each output pixel.
// No GLSL helpers, UV interpolation, or renderer state is reused by this oracle.
std::array<double, 4> bilinear_sample(const g1::RgbaImage &source, double x, double y) {
    const int left = static_cast<int>(std::floor(x));
    const int bottom = static_cast<int>(std::floor(y));
    const double fx = x - left, fy = y - bottom;
    std::array<double, 4> result{};
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            const int sx = std::clamp(left + dx, 0, static_cast<int>(source.width) - 1);
            const int sy = std::clamp(bottom + dy, 0, static_cast<int>(source.height) - 1);
            const double weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            for (std::size_t channel = 0; channel < result.size(); ++channel)
                result[channel] += weight * source.pixels[(sy * source.width + sx) * 4 + channel];
        }
    return result;
}
std::array<double, 4> supersampling_reference(const g1::RgbaImage &source, int x, int y,
                                            int width, int height) {
    std::array<double, 4> result{};
    for (double vertical : {.25, .75})
        for (double horizontal : {.25, .75}) {
            const auto sample = bilinear_sample(source, (x + horizontal) * source.width / width - .5,
                                                (y + vertical) * source.height / height - .5);
            for (std::size_t channel = 0; channel < result.size(); ++channel)
                result[channel] += sample[channel] / 4;
        }
    return result;
}
void expect_resolve_pixel(const g1::RgbaImage &source, int x, int y, int width, int height,
                          const std::string &name) {
    const auto expected = supersampling_reference(source, x, y, width, height);
    const auto actual = pixel(x, y);
    for (std::size_t channel = 0; channel < actual.size(); ++channel) {
        const auto target = static_cast<int>(std::lround(expected[channel]));
        if (std::abs(static_cast<int>(actual[channel]) - target) > 2)
            throw std::runtime_error(name + " at " + std::to_string(x) + "," + std::to_string(y) +
                                     " channel " + std::to_string(channel) + ": " +
                                     std::to_string(actual[channel]) + " expected " + std::to_string(target));
    }
    ++checks;
}

void supersampling_contract(g1::GLESRenderer &scene) {
    constexpr int width = 8, height = 6;
    g1::RgbaImage source{12, 9, {}}; // Actual 1.5x scale, where one bilinear sample is insufficient.
    for (std::uint32_t y = 0; y < source.height; ++y)
        for (std::uint32_t x = 0; x < source.width; ++x) {
            // Asymmetric horizontal/vertical landmarks prove orientation. Blue
            // has one-texel alternation, and alpha must survive without blending.
            source.pixels.push_back(static_cast<unsigned char>(20 + (x / 4) * 105));
            source.pixels.push_back(static_cast<unsigned char>(10 + (y / 3) * 115));
            source.pixels.push_back((x + y) % 2 ? 255 : 0);
            source.pixels.push_back(static_cast<unsigned char>(48 + ((x * 19 + y * 29) % 192)));
        }
    auto color = scene.texture(source);
    GLuint source_fbo = 0;
    glGenFramebuffers(1, &source_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, source_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color->id(), 0);
    expect(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
           "supersampling source texture is a renderable framebuffer");

    // Execute the previous single-blit path to establish that this fixture can
    // catch the actual regression, rather than a theoretical filter difference.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    int distinguishable = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const auto old = pixel(x, y);
            const auto expected = supersampling_reference(source, x, y, width, height);
            if (std::abs(static_cast<double>(old[2]) - expected[2]) >= 12)
                ++distinguishable;
        }
    expect(distinguishable >= width * height / 2,
           "1.5x high-frequency fixture distinguishes four taps from the old bilinear blit");

    clear({1, 0, 1, 1});
    glBindFramebuffer(GL_FRAMEBUFFER, source_fbo);
    glViewport(2, 3, 1, 2);
    glEnable(GL_SCISSOR_TEST);
    glScissor(2, 3, 1, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_NEVER);
    glDepthMask(GL_TRUE);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_NEVER, 0, ~0U);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT_AND_BACK);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    GLuint inherited_sampler = 0;
    glGenSamplers(1, &inherited_sampler);
    glSamplerParameteri(inherited_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(inherited_sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(inherited_sampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glSamplerParameteri(inherited_sampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindSampler(0, inherited_sampler);
    glActiveTexture(GL_TEXTURE5);

    scene.resolve_supersampling(color->id(), width, height);
    GLint framebuffer = -1, viewport[4]{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    expect(framebuffer == 0 && viewport[0] == 0 && viewport[1] == 0 &&
               viewport[2] == width && viewport[3] == height,
           "resolve selects the native output framebuffer and viewport");
    expect(!glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_BLEND) && !glIsEnabled(GL_DEPTH_TEST) &&
               !glIsEnabled(GL_STENCIL_TEST) && !glIsEnabled(GL_CULL_FACE),
           "resolve ignores inherited scene rejection/blend state");
    GLboolean depth_write = GL_TRUE, color_write[4]{};
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write);
    glGetBooleanv(GL_COLOR_WRITEMASK, color_write);
    expect(!depth_write && std::all_of(std::begin(color_write), std::end(color_write),
                                     [](GLboolean value) { return value == GL_TRUE; }),
           "resolve writes all color channels and preserves native depth");

    expect_resolve_pixel(source, 0, 0, width, height, "bottom-left landmark and clamped quarter taps");
    expect_resolve_pixel(source, width - 1, 0, width, height, "bottom-right landmark");
    expect_resolve_pixel(source, 0, height - 1, width, height, "top-left landmark");
    expect_resolve_pixel(source, width - 1, height - 1, width, height, "top-right landmark");
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if ((x != 0 && x != width - 1) || (y != 0 && y != height - 1))
                expect_resolve_pixel(source, x, y, width, height, "four-tap full output against CPU oracle");

    // Reuse the cached shader at a different aspect/scale. The footprint must
    // depend on the current output size, not the source or the previous resolve.
    constexpr int second_width = 6, second_height = 3;
    scene.resolve_supersampling(color->id(), second_width, second_height);
    for (int y = 0; y < second_height; ++y)
        for (int x = 0; x < second_width; ++x)
            expect_resolve_pixel(source, x, y, second_width, second_height,
                                  "cached resolve updates its output-pixel footprint");
    bool rejected = false;
    try {
        scene.resolve_supersampling(0, width, height);
    } catch (const g1::FormatError &) {
        rejected = true;
    }
    expect(rejected, "resolve rejects a missing source texture");
    rejected = false;
    try {
        scene.resolve_supersampling(color->id(), 0, height);
    } catch (const g1::FormatError &) {
        rejected = true;
    }
    expect(rejected, "resolve rejects a zero-sized output");
    glDeleteSamplers(1, &inherited_sampler);
    glDeleteFramebuffers(1, &source_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 64, 64);
    expect(glGetError() == GL_NO_ERROR, "four-tap resolve leaves no GLES errors");
}
} // namespace
int main() {
    try {
        EGLSession egl;
        const std::string renderer_name = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        const std::string version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        g1::GLESRenderer scene;
        g1::GLESModelRenderer renderer;
        std::vector<g1::SceneVertex> quad;
        for (const auto &vertex : vertices())
            quad.push_back({vertex.position, {.5F, .5F}, {}});
        auto mesh = scene.mesh(quad);
        auto red = scene.texture({1, 1, {255, 0, 0, 128}});
        clear({0, 0, 1, 1});
        scene.encode({{mesh, red, identity, {1, 1, 1, 1}, true, {0, 0}, false, false}});
        expect_pixel({128.0F / 255, 0, 127.0F / 255, 1}, "scene alpha blending");
        clear();
        render(renderer, model());
        expect_pixel({1, 0, 0, 1}, "rigid shader and clip conversion");
        for (std::uint8_t mode = 64; mode <= 68; ++mode) {
            auto m = model({.2F, .4F, .6F, .5F});
            m.surfaces[0].appearance.blending = mode;
            const std::array<float, 4> source{.2F, .4F, .6F, .5F}, dest{.4F, .2F, .1F, .8F};
            std::array<float, 4> expected{};
            for (int c = 0; c < 4; ++c) {
                if (mode == 64)
                    expected[c] = source[c] * source[3] + dest[c] * (1 - source[3]);
                if (mode == 65)
                    expected[c] = source[c] * source[3] + dest[c];
                if (mode == 66)
                    expected[c] = source[c] * dest[c];
                if (mode == 67)
                    expected[c] = 2 * source[c] * dest[c];
                if (mode == 68)
                    expected[c] = source[c];
            }
            clear(dest);
            render(renderer, m);
            expect_pixel(expected, "compositing " + std::to_string(mode));
        }
        const std::array<float, 4> tex{64.0F / 255, 128.0F / 255, 192.0F / 255, 128.0F / 255};
        for (std::uint8_t format = 96; format <= 100; ++format)
            for (std::uint8_t fn = 224; fn <= 228; ++fn) {
                if (fn == 225 && format != 99 && format != 100)
                    continue;
                auto m = model({.2F, .4F, .6F, .8F});
                auto &s = m.surfaces[0];
                s.effective_alpha = .5F;
                g1::M3GTextureState t;
                t.image_object = 7;
                t.image_format = format;
                t.function = fn;
                t.blend_color = {.8F, .7F, .1F};
                s.appearance.texture = t;
                m.images.emplace(7, g1::RgbaImage{1, 1, {64, 128, 192, 128}});
                std::array<float, 4> result{.2F, .4F, .6F, .4F};
                bool rgb = format != 96, alpha = format == 96 || format == 98 || format == 100;
                for (int c = 0; c < 3; ++c) {
                    if (fn == 224 && rgb)
                        result[c] = result[c] * (1 - tex[c]) + t.blend_color[c] * tex[c];
                    if (fn == 225)
                        result[c] =
                            format == 99 ? tex[c] : result[c] * (1 - tex[3]) + tex[c] * tex[3];
                    if (fn == 226 && rgb)
                        result[c] += tex[c];
                    if (fn == 227 && rgb)
                        result[c] *= tex[c];
                    if (fn == 228 && rgb)
                        result[c] = tex[c];
                }
                if (alpha && fn != 225)
                    result[3] = fn == 228 ? tex[3] : result[3] * tex[3];
                clear();
                render(renderer, m);
                expect_pixel(result,
                             "texture env " + std::to_string(format) + "/" + std::to_string(fn));
            }
        auto flat = model();
        flat.surfaces[0].appearance.shading = 164;
        for (std::size_t i = 0; i < 6; ++i)
            flat.surfaces[0].vertices[i].color =
                i % 3 == 2 ? std::array<float, 4>{0, 0, 1, 1} : std::array<float, 4>{1, 0, 0, 1};
        clear();
        render(renderer, flat);
        expect_pixel({0, 0, 1, 1}, "third-vertex flat color");
        auto threshold = model({1, 0, 0, .5F});
        threshold.surfaces[0].appearance.alpha_threshold = .5F;
        clear();
        render(renderer, threshold);
        expect_pixel({1, 0, 0, .5F}, "alpha threshold equality retained");
        threshold.surfaces[0].appearance.alpha_threshold = .5001F;
        clear();
        render(renderer, threshold);
        expect_pixel({0, 0, 0, 0}, "alpha threshold below discarded");
        auto front = model();
        front.surfaces[0].appearance.culling = 161;
        clear();
        render(renderer, front);
        expect_pixel({0, 0, 0, 0}, "front face culling");
        front.surfaces[0].appearance.winding = 169;
        clear();
        render(renderer, front);
        expect_pixel({1, 0, 0, 1}, "clockwise front face");
        auto near = model();
        near.surfaces[0].appearance.depth_test = false;
        for (auto &v : near.surfaces[0].vertices)
            v.position[2] = .25F;
        auto far = model({0, 1, 0, 1});
        for (auto &v : far.surfaces[0].vertices)
            v.position[2] = .75F;
        clear();
        render(renderer, near);
        render(renderer, far);
        expect_pixel({1, 0, 0, 1}, "ALWAYS comparison still writes depth");
        near.surfaces[0].appearance.depth_write = false;
        clear();
        render(renderer, near);
        render(renderer, far);
        expect_pixel({0, 1, 0, 1}, "independent depth write disabled");
        auto mask = model({1, 0, 0, .25F});
        mask.surfaces[0].appearance.color_write = false;
        clear({0, 1, 0, 1});
        render(renderer, mask);
        expect_pixel({0, 1, 0, .25F}, "independent color/alpha mask");
        auto uv = model();
        g1::M3GTextureState sampling;
        sampling.image_object = 7;
        sampling.image_format = 100;
        sampling.function = 228;
        sampling.wrap_s = 240;
        sampling.wrap_t = 240;
        uv.surfaces[0].appearance.texture = sampling;
        uv.images.emplace(7, g1::RgbaImage{2, 1, {255, 0, 0, 255, 0, 255, 0, 255}});
        for (auto &v : uv.surfaces[0].vertices)
            v.texcoord = {1.5F, 1, 0, 2};
        clear();
        render(renderer, uv);
        expect_pixel({0, 1, 0, 1}, "homogeneous texture coordinates");
        for (auto &v : uv.surfaces[0].vertices)
            v.texcoord = {1.25F, .5F, 0, 1};
        uv.surfaces[0].appearance.texture->wrap_s = 241;
        clear();
        render(renderer, uv);
        expect_pixel({1, 0, 0, 1}, "repeat sampler");
        uv.surfaces[0].appearance.texture->wrap_s = 240;
        clear();
        render(renderer, uv);
        expect_pixel({0, 1, 0, 1}, "clamp sampler");
        auto gpu = renderer.upload(model());
        std::map<std::size_t, g1::M3GMatrix> empty_pose;
        clear({0, 1, 0, 1});
        bool rejected = false;
        try {
            renderer.encode(*gpu, identity, &empty_pose);
        } catch (const g1::FormatError &) {
            rejected = true;
        }
        expect(rejected, "missing drawn pose rejected");
        expect_pixel({0, 1, 0, 1}, "invalid pose draws nothing");
        g1::M3GRenderAsset asset;
        asset.geometry = model();
        asset.traversal.push_back({g1::M3GDrawableKind::mesh, 0});
        auto stateful = renderer.upload(asset);
        g1::M3GPreparedFrame frame;
        frame.assets.push_back(&stateful->asset());
        g1::M3GPreparedDraw draw;
        draw.instance = 0;
        draw.node_object = 1;
        draw.geometry_index = 0;
        draw.metal_matrix = identity;
        frame.draws.push_back(draw);
        clear();
        renderer.encode_frame({stateful}, frame);
        expect_pixel({1, 0, 0, 1}, "prepared frame draw");
        auto mismatch = frame;
        mismatch.assets[0] = &asset;
        rejected = false;
        try {
            renderer.encode_frame({stateful}, mismatch);
        } catch (const g1::FormatError &) {
            rejected = true;
        }
        expect(rejected, "immutable frame/asset identity enforced");
        frame.draws[0].assembly_role = 1;
        frame.draws[0].assembly_mix = .5F;
        clear();
        renderer.encode_frame({stateful}, frame);
        std::array<unsigned char, 8 * 8 * 4> pixels{};
        glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        int red_pixels = 0;
        for (int p = 0; p < 64; ++p)
            if (pixels[p * 4] == 255)
                ++red_pixels;
        expect(red_pixels == 32, "robot assembly coverage is half at mix0.5");
        frame.draws[0].assembly_role = 2;
        renderer.encode_frame({stateful}, frame);
        glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        red_pixels = 0;
        for (int p = 0; p < 64; ++p)
            if (pixels[p * 4] == 255)
                ++red_pixels;
        expect(red_pixels == 64, "robot and vehicle coverage is complementary");
        g1::M3GRenderAsset sprite_asset;
        sprite_asset.geometry.images.emplace(7, g1::RgbaImage{1, 1, {0, 255, 0, 255}});
        g1::M3GSpriteAsset sprite;
        sprite.node_object = 9;
        sprite.image_object = 7;
        sprite.image_format = 100;
        sprite.model = identity;
        sprite.crop_pixels = {0, 0, 1, 1};
        sprite.appearance.culling = 162;
        sprite_asset.sprites.push_back(sprite);
        sprite_asset.traversal.push_back({g1::M3GDrawableKind::sprite, 0});
        auto sprite_gpu = renderer.upload(sprite_asset);
        g1::M3GPreparedFrame sprite_frame;
        sprite_frame.assets.push_back(&sprite_gpu->asset());
        g1::M3GPreparedDraw sd;
        sd.kind = g1::M3GDrawableKind::sprite;
        sd.instance = 0;
        sd.node_object = 9;
        sd.geometry_index = 0;
        sd.metal_matrix = identity;
        sd.sprite_projection.result = g1::M3GSprite3DResult::drawn;
        const auto sv = vertices({1, 1, 1, 1});
        std::copy(sv.begin(), sv.end(), sd.sprite_vertices.begin());
        sprite_frame.draws.push_back(sd);
        clear();
        renderer.encode_frame({sprite_gpu}, sprite_frame);
        expect_pixel({0, 1, 0, 1}, "Sprite3D actual projected vertex draw");
        rejected = false;
        auto invalid = model();
        invalid.surfaces[0].appearance.material = g1::M3GMaterial{};
        try {
            renderer.upload(invalid);
        } catch (const g1::FormatError &) {
            rejected = true;
        }
        expect(rejected, "unsupported lighting fails before upload");
        supersampling_contract(scene);
        clear({0, .25F, .5F, 1});
        const auto before_context_loss = pixel();
        auto resources = mesh;
        scene.abandon_context();
        expect(!red->live(), "context invalidation reaches retained texture");
        rejected = false;
        try {
            scene.encode({{resources, red, identity}});
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        expect(rejected, "lost-context resource draw rejected");
        rejected = false;
        try {
            scene.resolve_supersampling(red->id(), 64, 64);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        expect(rejected, "lost-context supersampling resolve rejected");
        expect(pixel() == before_context_loss, "lost-context resolve draws no pixels");
        expect(glGetError() == GL_NO_ERROR, "no unhandled GLES errors");
        std::cout << "{\"status\":\"passed\",\"checks\":" << checks << ",\"renderer\":\""
                  << renderer_name << "\",\"version\":\"" << version
                  << "\",\"scope\":\"offscreen GLES shader/state/pixel contracts; not physical A36 "
                     "validation\"}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "renderer probe failed: " << e.what() << '\n';
        return 1;
    }
}
