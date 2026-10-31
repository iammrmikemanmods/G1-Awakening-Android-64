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
