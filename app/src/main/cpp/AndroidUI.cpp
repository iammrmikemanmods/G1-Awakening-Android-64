#include "AndroidUI.hpp"
#include "AssetStore.hpp"
#include "g1/BitmapFont.hpp"
#include "g1/OriginalPresentation.hpp"
#include "g1/OriginalUI.hpp"
#include "g1/TextureEnhancement.hpp"
#include "g1/UIBitmap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <limits>
#include <map>
#include <utility>

namespace g1::android {
namespace {
using Rect = adaptive::Rect;
using Color = std::array<float, 4>;
constexpr Color white{{1, 1, 1, 1}};
constexpr Color red{{1, .36F, .37F, 1}};
constexpr Color purple{{.8F, .36F, 1, 1}};

bool inside(Rect r, float x, float y) noexcept {
    return x >= r.x && y >= r.y && x < r.right() && y < r.bottom();
}
Rect intersection(Rect a, Rect b) noexcept {
    const double x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::max(0.0, std::min(a.right(), b.right()) - x),
            std::max(0.0, std::min(a.bottom(), b.bottom()) - y)};
}
std::string uppercase(std::string s) {
    for (auto &c : s)
        if (static_cast<unsigned char>(c) < 128)
            c = static_cast<char>(std::toupper(c));
    return s;
}
std::string utf8(std::u32string_view text) {
    std::string out;
    for (const auto c : text) {
        if (c < 0x80)
            out.push_back(char(c));
        else if (c < 0x800) {
            out.push_back(char(0xc0 | (c >> 6)));
            out.push_back(char(0x80 | (c & 63)));
        } else if (c < 0x10000) {
            out.push_back(char(0xe0 | (c >> 12)));
            out.push_back(char(0x80 | ((c >> 6) & 63)));
            out.push_back(char(0x80 | (c & 63)));
        } else {
            out.push_back(char(0xf0 | (c >> 18)));
            out.push_back(char(0x80 | ((c >> 12) & 63)));
            out.push_back(char(0x80 | ((c >> 6) & 63)));
            out.push_back(char(0x80 | (c & 63)));
        }
    }
    return out;
}
std::uint32_t portrait(const std::string &value, bool dialogue) {
    const auto name = uppercase(value);
    if (name.find("BUMBLEBEE") != std::string::npos)
        return dialogue ? 1295 : 731;
    if (name.find("RATCHET") != std::string::npos)
        return 396;
    if (name.find("OPTIMUS") != std::string::npos)
        return 878;
    if (name.find("MEGATRON") != std::string::npos)
        return dialogue ? 1156 : 670;
    return 0;
}
RgbaImage crop(const RgbaImage &source, const BitmapGlyph &glyph) {
    if (std::uint32_t(glyph.source_x) + glyph.width > source.width ||
        std::uint32_t(glyph.source_y) + glyph.height > source.height)
        throw FormatError("Original UI glyph exceeds its source sheet");
    RgbaImage result{glyph.width, glyph.height, Bytes(std::size_t(glyph.width) * glyph.height * 4)};
    for (std::uint32_t row = 0; row < glyph.height; ++row)
        std::copy_n(source.pixels.begin() +
                        (std::size_t(glyph.source_y + row) * source.width + glyph.source_x) * 4,
                    glyph.width * 4, result.pixels.begin() + std::size_t(row) * glyph.width * 4);
    return result;
}
struct WrappedLine {
    std::size_t start{}, end{}, consumed{};
};
} // namespace

struct AndroidUI::Impl {
    struct Picture {
        GLTexturePtr texture;
        double width{}, height{};
    };
    struct Font {
        BitmapFontAssets assets;
        std::map<char32_t, GLTexturePtr> glyphs;
        explicit Font(BitmapFontAssets a) : assets(std::move(a)) {}
    };
    struct Target {
        Rect rect;
        UiAction action;
        bool enabled;
    };
    GLESRenderer &renderer;
    const Archive *archive{};
    std::string resource_root;
    int width{}, height{};
    adaptive::Layout screen{};
    GLTexturePtr white_texture;
    std::map<std::uint32_t, Picture> pictures;
    std::map<std::size_t, std::unique_ptr<Font>> fonts;
    std::function<RgbaImage(const std::string &, float)> system_rasterizer;
    std::map<std::pair<std::string, int>, Picture> system_text;
    std::map<std::array<float, 4>, std::shared_ptr<GLESMesh>> quads;
    std::vector<GLESDraw> draws;
    std::vector<Target> targets;
    std::vector<UiAccessibleNode> accessible_nodes;
    std::map<std::pair<int, int>, int> accessible_action_ids;
    int next_accessible_id{100};
    std::optional<Rect> clip;
    std::uint64_t scroll_token{};
    double scroll_position{}, scroll_maximum{};
    Rect scroll_region{};
    std::u32string dialogue_text;
    std::vector<WrappedLine> dialogue_lines;
    std::uint64_t dialogue_identity{};
    double dialogue_width{}, dialogue_height{}, dialogue_font_height{}, dialogue_line_height{},
        dialogue_ink_height{};
    std::size_t dialogue_slot{2};
    bool dialogue_ready{};

    explicit Impl(GLESRenderer &r) : renderer(r) {}
    Font &font(std::size_t slot) {
        const auto found = fonts.find(slot);
        if (found != fonts.end())
            return *found->second;
        if (!archive)
            throw FormatError("Android UI archive is not initialized");
        auto result = std::make_unique<Font>(load_bitmap_font_assets(*archive, slot));
        return *fonts.emplace(slot, std::move(result)).first->second;
    }
    static bool supports(const Font &f, std::u32string_view text) {
        return std::all_of(text.begin(), text.end(), [&](char32_t c) {
            return c == U'\n' || c == U'\r' || f.assets.descriptor.contains(c);
        });
    }
    std::size_t chooseFont(std::size_t preferred, std::u32string_view text) {
        if (supports(font(preferred), text))
            return preferred;
        // Match source host role fallbacks. A red lowercase font must never
        // replace white body text merely because it covers more characters.
        if (preferred == 0 && supports(font(2), text))
            return 2;
        if (preferred == 6) {
            if (supports(font(0), text))
                return 0;
            if (supports(font(2), text))
                return 2;
        }
        if (system_rasterizer)
            return 12;
        // Headless/source-only users can still measure every recovered face;
        // production installs the Android fallback before drawing UI.
        for (const auto slot : {2U, 0U, 1U, 11U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U})
            if (supports(font(slot), text))
                return slot;
        // Never acknowledge unreadable dialogue or silently drop source text.
        throw FormatError("Text contains a character outside all original G1 bitmap fonts");
    }
    Picture &systemText(std::u32string_view text, double pixel_size) {
        const auto key = std::make_pair(utf8(text), static_cast<int>(std::ceil(pixel_size)));
        const auto found = system_text.find(key);
        if (found != system_text.end())
            return found->second;
        if (!system_rasterizer)
            throw FormatError("Android system text rasterizer is unavailable");
        auto pixels = system_rasterizer(key.first, float(key.second));
        if (!pixels.width || !pixels.height || pixels.width > 8192 || pixels.height > 8192 ||
            pixels.pixels.size() != std::size_t(pixels.width) * pixels.height * 4)
            throw FormatError("Android system text rasterizer returned invalid pixels");
        if (system_text.size() > 512)
            system_text.clear();
        return system_text
            .emplace(key,
                     Picture{renderer.texture(pixels), double(pixels.width), double(pixels.height)})
            .first->second;
    }
    GLTexturePtr glyphTexture(Font &f, char32_t c) {
        const auto existing = f.glyphs.find(c);
        if (existing != f.glyphs.end())
            return existing->second;
        const auto *g = f.assets.descriptor.glyph(c);
        if (!g || !g->width || !g->height)
            return {};
        auto image = scale4x_rgba(crop(f.assets.image, *g));
        return f.glyphs.emplace(c, renderer.texture(image)).first->second;
    }
    Picture &picture(std::uint32_t resource) {
        const auto existing = pictures.find(resource);
        if (existing != pictures.end())
            return existing->second;
        if (!archive)
            throw FormatError("Android UI archive is not initialized");
        const auto png = archive->find(resource, 0xb7178678U),
                   bmp = archive->find(resource, 0xb717945fU);
        if (png.size() + bmp.size() != 1)
            throw FormatError("Original UI picture is missing or ambiguous");
        const auto source = !png.empty() ? decode_png(archive->read(png.front()))
                                         : decode_ui_bmp(archive->read(bmp.front()), 0x00ff00ffU);
        auto clean = cleanTexture(source, "ui", 4, resource_root);
        auto pixels = clean ? std::move(*clean) : scale4x_rgba(source);
        return pictures
            .emplace(resource,
                     Picture{renderer.texture(pixels), double(source.width), double(source.height)})
            .first->second;
    }
    void verifyPicture(std::uint32_t id, unsigned w, unsigned h) {
        const auto &p = picture(id);
        if (p.width != w || p.height != h)
            throw FormatError("Original UI picture dimensions changed");
    }
    std::shared_ptr<GLESMesh> mesh(std::array<float, 4> uv) {
        const auto found = quads.find(uv);
        if (found != quads.end())
            return found->second;
        const auto v = [](float x, float y, float u, float t) {
            return SceneVertex{{{x, y, 0, 1}}, {{u, t}}, {{0, 0}}};
        };
        auto value =
            renderer.mesh({v(0, 0, uv[0], uv[1]), v(1, 0, uv[2], uv[1]), v(0, 1, uv[0], uv[3]),
                           v(1, 0, uv[2], uv[1]), v(1, 1, uv[2], uv[3]), v(0, 1, uv[0], uv[3])});
        // Clip edges move during scrolling; keep this cache bounded.
        if (quads.size() > 4096)
            quads.clear();
        return quads.emplace(uv, std::move(value)).first->second;
    }
    void quad(Rect destination, const GLTexturePtr &texture,
              std::array<float, 4> uv = {{0, 0, 1, 1}}, Color tint = white, bool nearest = false) {
        if (!texture || width <= 0 || height <= 0 || destination.width <= 0 ||
            destination.height <= 0)
            return;
        const Rect original = destination;
        destination =
            intersection(destination, clip ? *clip : Rect{0, 0, double(width), double(height)});
        if (destination.width <= 0 || destination.height <= 0)
            return;
        const float du = uv[2] - uv[0], dv = uv[3] - uv[1];
        uv = {float(uv[0] + du * (destination.x - original.x) / original.width),
              float(uv[1] + dv * (destination.y - original.y) / original.height),
              float(uv[0] + du * (destination.right() - original.x) / original.width),
              float(uv[1] + dv * (destination.bottom() - original.y) / original.height)};
        Matrix4 matrix{};
        matrix[0] = float(2 * destination.width / width);
        matrix[5] = float(-2 * destination.height / height);
        matrix[10] = 1;
        matrix[12] = float(2 * destination.x / width - 1);
        matrix[13] = float(1 - 2 * destination.y / height);
        matrix[15] = 1;
        draws.push_back({mesh(uv), texture, matrix, tint, false, {}, true, nearest});
    }
    void solid(Rect destination, Color color) {
        quad(destination, white_texture, {{0, 0, 1, 1}}, color);
    }
    void image(std::uint32_t id, Rect destination, std::optional<Rect> source = {},
               Color tint = white) {
        auto &p = picture(id);
        const Rect s = source ? *source : Rect{0, 0, p.width, p.height};
        quad(destination, p.texture,
             {{float(s.x / p.width), float(s.y / p.height), float(s.right() / p.width),
               float(s.bottom() / p.height)}},
             tint);
    }
    void fitImage(std::uint32_t id, Rect destination) {
        auto &p = picture(id);
        const auto factor = std::min(destination.width / p.width, destination.height / p.height);
        image(id, {destination.x + (destination.width - p.width * factor) / 2,
                   destination.y + (destination.height - p.height * factor) / 2, p.width * factor,
                   p.height * factor});
    }
    void tile(std::uint32_t id, Rect source, Rect destination, double scale) {
        if (scale <= 0 || source.width <= 0 || source.height <= 0)
            return;
        const double tw = source.width * scale, th = source.height * scale;
        const auto prior = clip;
        clip = intersection(destination, prior ? *prior : screen.full);
        for (double y = destination.y; y < destination.bottom(); y += th)
            for (double x = destination.x; x < destination.right(); x += tw)
                image(id, {x, y, tw, th}, source);
        clip = prior;
    }
    void nine(std::uint32_t id, Rect source, Rect dest, double top, double left, double bottom,
              double right, bool tiled = false, double scale = 1) {
        if (source.width < left + right || source.height < top + bottom)
            return;
        const double dl = std::min(left * scale, dest.width / 2),
                     dr = std::min(right * scale, dest.width / 2);
        const double dt = std::min(top * scale, dest.height / 2),
                     db = std::min(bottom * scale, dest.height / 2);
        const std::array<double, 4> sx{
            {source.x, source.x + left, source.right() - right, source.right()}};
        const std::array<double, 4> sy{
            {source.y, source.y + top, source.bottom() - bottom, source.bottom()}};
        const std::array<double, 4> dx{{dest.x, dest.x + dl, dest.right() - dr, dest.right()}};
        const std::array<double, 4> dy{{dest.y, dest.y + dt, dest.bottom() - db, dest.bottom()}};
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) {
                Rect s{sx[x], sy[y], sx[x + 1] - sx[x], sy[y + 1] - sy[y]};
                Rect d{dx[x], dy[y], dx[x + 1] - dx[x], dy[y + 1] - dy[y]};
                if (s.width <= 0 || s.height <= 0 || d.width <= 0 || d.height <= 0)
                    continue;
                if (tiled && (x == 1 || y == 1))
                    tile(id, s, d, scale);
                else
                    image(id, d, s);
            }
    }
    double textWidth(std::size_t slot, std::u32string_view text, double height) {
        if (text.empty())
            return 0;
        if (slot == 12)
            return systemText(text, height).width;
        const auto &descriptor = font(slot).assets.descriptor;
        return std::max(0, descriptor.measure(text).width) * height / descriptor.metrics().height;
    }
    void line(const std::u32string &value, Rect area, double glyph_height, std::size_t slot = 2,
              int align = 0, Color tint = white, bool fit = false) {
        if (value.empty() || area.width <= 0 || area.height <= 0)
            return;
        const auto requested_slot = slot;
        if (slot != 12)
            slot = chooseFont(slot, value);
        if (slot == 12) {
            if (requested_slot == 4 || requested_slot == 6 || requested_slot == 9) {
                tint[0] *= red[0];
                tint[1] *= red[1];
                tint[2] *= red[2];
            } else if (requested_slot == 5 || requested_slot == 7 || requested_slot == 10) {
                tint[0] *= purple[0];
                tint[1] *= purple[1];
                tint[2] *= purple[2];
            }
            auto &text = systemText(value, glyph_height);
            const double scale = fit ? std::min(1.0, area.width / text.width) : 1.0;
            const double dw = text.width * scale, dh = text.height * scale;
            double x = area.x;
            if (align == 1)
                x += std::max(0.0, (area.width - dw) / 2);
            else if (align == 2)
                x += std::max(0.0, area.width - dw);
            const double y = area.y + std::max(0.0, (area.height - dh) / 2);
            const auto prior = clip;
            clip = intersection(area, prior ? *prior : screen.full);
            quad({x, y, dw, dh}, text.texture, {{0, 0, 1, 1}}, tint);
            clip = prior;
            return;
        }
        auto &f = font(slot);
        const auto &descriptor = f.assets.descriptor;
        const auto metrics = descriptor.metrics();
        double measured = textWidth(slot, value, glyph_height);
        if (fit && measured > area.width && measured > 0) {
            glyph_height *= area.width / measured;
            measured = area.width;
        }
        const double scale = glyph_height / metrics.height;
        double x = area.x;
        if (align == 1)
            x += std::max(0.0, (area.width - std::ceil(measured)) / 2);
        else if (align == 2)
            x += std::max(0.0, area.width - std::ceil(measured));
        const double y =
            area.y + std::max(0.0, (area.height - std::ceil(metrics.line_height * scale)) / 2);
        const auto prior = clip;
        clip = intersection(area, prior ? *prior : screen.full);
        for (const auto &glyph : descriptor.layout(value)) {
            auto texture = glyphTexture(f, value.at(glyph.character_index));
            if (texture)
                quad({x + glyph.x * scale, y + glyph.y * scale, glyph.width * scale,
                      glyph.height * scale},
                     texture, {{0, 0, 1, 1}}, tint);
        }
        clip = prior;
    }
    void line(const std::string &value, Rect area, double glyph_height, std::size_t slot = 2,
              int align = 0, Color tint = white, bool fit = false) {
        if (!value.empty())
            line(bitmap_font_utf8(value), area, glyph_height, slot, align, tint, fit);
    }
    std::vector<WrappedLine> wrap(const std::u32string &text, double available, double glyph_height,
                                  std::size_t slot) {
        std::vector<WrappedLine> lines;
        if (text.empty() || available <= 0)
            return lines;
        std::size_t start = 0;
        while (start < text.size()) {
            if (text[start] == U'\n') {
                lines.push_back({start, start, start + 1});
                ++start;
                continue;
            }
            std::size_t end = start, last_space = std::u32string::npos;
            while (end < text.size() && text[end] != U'\n') {
                if (text[end] == U' ' || text[end] == U'\t')
                    last_space = end;
                if (end > start &&
                    textWidth(slot, std::u32string_view(text).substr(start, end - start + 1),
                              glyph_height) > available)
                    break;
                ++end;
            }
            std::size_t consumed = end;
            if (end < text.size() && text[end] != U'\n' && last_space != std::u32string::npos &&
                last_space > start) {
                end = last_space;
                consumed = last_space + 1;
            } else if (end < text.size() && text[end] == U'\n')
                ++consumed;
            if (end == start && consumed == start) {
                ++end;
                ++consumed;
            }
            // TextKit wraps whitespace with its preceding line; preserve it
            // in the reveal budget while omitting trailing blank ink.
            while (consumed < text.size() && (text[consumed] == U' ' || text[consumed] == U'\t'))
                ++consumed;
            std::size_t ink_end = end;
            while (ink_end > start && (text[ink_end - 1] == U' ' || text[ink_end - 1] == U'\t' ||
                                       text[ink_end - 1] == U'\r'))
                --ink_end;
            lines.push_back({start, ink_end, consumed});
            start = consumed;
        }
        return lines;
    }
    double paragraph(const std::string &value, Rect area, double glyph_height, std::size_t slot = 2,
                     Color tint = white, double spacing = 0,
                     std::size_t maximum = std::numeric_limits<std::size_t>::max(), int align = 0,
                     double offset = 0) {
        if (value.empty())
            return 0;
        const auto text = bitmap_font_utf8(value);
        if (slot != 12)
            slot = chooseFont(slot, text);
        const auto wrapped = wrap(text, area.width, glyph_height, slot);
        if (spacing <= 0)
            spacing = glyph_height + 2 * screen.scale;
        const auto prior = clip;
        clip = intersection(area, prior ? *prior : screen.full);
        for (std::size_t i = 0; i < wrapped.size() && i < maximum; ++i) {
            const auto &row = wrapped[i];
            line(text.substr(row.start, row.end - row.start),
                 {area.x, area.y + i * spacing - offset, area.width, spacing}, glyph_height, slot,
                 align, tint);
        }
        clip = prior;
        return wrapped.size() * spacing;
    }
    Rect source(Rect rect) const noexcept {
        return {screen.safe.x + rect.x * screen.scale, screen.source.y + rect.y * screen.scale,
                rect.width * screen.scale, rect.height * screen.scale};
    }
    void describe(int id, Rect rect, const std::string &label, const std::string &hint = {},
                  const std::string &value = {}, UiAction action = {}, bool enabled = true,
                  bool forward = false, bool backward = false) {
        rect = intersection(rect, clip ? *clip : screen.full);
        if (label.empty() || rect.width <= 0 || rect.height <= 0)
            return;
        accessible_nodes.push_back(
            {id, action, label, hint, value, rect, enabled, forward, backward});
    }
    void target(Rect rect, UiAction action, bool enabled = true, std::string label = {},
                std::string hint = {}, std::string value = {}) {
        targets.push_back({rect, action, enabled});
        if (label.empty())
            switch (action.id) {
            case ui_menu:
                label = "Pause menu";
                break;
            case ui_move:
                label = "Move";
                break;
            case ui_capture:
                label = "Capture";
                break;
            case ui_transform:
                label = "Transform";
                break;
            case ui_attack:
                label = "Attack";
                break;
            case ui_heal:
                label = "Repair";
                break;
            case ui_upgrade:
                label = "Upgrade";
                break;
            case ui_end_turn:
                label = "End turn";
                break;
            case ui_build:
                label = "Activate unit";
                break;
            case ui_deploy:
                label = "Activate tape";
                break;
            case ui_undo:
                label = "Undo";
                break;
            case ui_dialogue_skip:
                label = "Skip dialogue";
                break;
            case ui_tech_specs:
                label = "Selected Transformer Tech Specs";
                hint = "Opens the original character statistics and 3D model.";
                break;
            case ui_specs_back:
                label = "Back to game";
                break;
            case ui_specs_previous:
                label = "Previous Transformer";
                break;
            case ui_specs_next:
                label = "Next Transformer";
                break;
            case ui_specs_robot:
                label = "View robot form";
                break;
            case ui_specs_vehicle:
                label = "View alternate form";
                value = enabled ? "Available" : "Unavailable";
                break;
            default:
                break;
            }
        if (action.id >= ui_menu && action.id <= ui_undo && hint.empty())
            hint = "Tap to perform this action when available.";
        if (action.id == ui_dialogue_skip && hint.empty())
            hint = "Tap to perform this action when available.";
        const auto key = std::make_pair(action.id, action.value);
        auto found = accessible_action_ids.find(key);
        if (found == accessible_action_ids.end())
            found = accessible_action_ids.emplace(key, next_accessible_id++).first;
        describe(found->second, rect, label, hint, value, action, enabled);
    }
    void flush() {
        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        renderer.encode(draws);
        draws.clear();
    }
    void menuBackground() {
        const auto s = screen.scale;
        const auto w = screen.virtual_width;
        tile(1002, {16, 8, 64, 64}, screen.full, s);
        image(1002, source({(w - 480) / 2, 0, 480, 84}), Rect{0, 0, 480, 84});
        nine(1002, {0, 84, 132, 29}, source({0, 84, (w - 216) / 2, 29}), 0, 20, 0, 20, false, s);
        image(1002, source({(w - 216) / 2, 84, 216, 29}), Rect{132, 84, 216, 29});
        nine(1002, {348, 84, 132, 29}, source({(w + 216) / 2, 84, (w - 216) / 2, 29}), 0, 20, 0, 20,
             false, s);
        nine(1002, {0, 113, 480, 103}, source({0, 113, w, 103}), 12, 10, 12, 10, true, s);
        nine(1002, {0, 216, 480, 34}, source({0, 216, w, 34}), 0, 90, 0, 90, false, s);
        image(1002, source({0, 250, 80, 70}), Rect{0, 250, 80, 70});
        image(1002, source({w - 80, 250, 80, 70}), Rect{400, 250, 80, 70});
    }
    void panel(const UiPanel &p) {
        const auto s = screen.scale;
        // Information views, result bodies and generic host buttons use
        // UIKit fonts in the iOS host, even when a source face covers them.
        // Keep them on Android's platform text path rather than substituting
        // the bitmap face used by the original game headings and menus.
        const std::size_t platform_slot = system_rasterizer ? 12 : 2;
        if (scroll_token != p.token) {
            scroll_token = p.token;
            scroll_position = 0;
        }
        scroll_maximum = 0;
        scroll_region = {};
        if (p.action_list) {
            // UIKit's production sheet is host UI, not original menu art.
            // Keep every source choice and a fixed Cancel action available
            // on Android without forcing them into the nine source slots.
            solid(screen.full, {{.025F, .045F, .075F, 1}});
            const double margin = 16 * s, gap = 6 * s, row_height = 40 * s;
            line(p.title,
                 {screen.safe.x + margin, screen.safe.y + 14 * s, screen.safe.width - 2 * margin,
                  34 * s},
                 22 * s, 0, 1, white, true);
            describe(1,
                     {screen.safe.x + margin, screen.safe.y + 14 * s,
                      screen.safe.width - 2 * margin, 34 * s},
                     p.title);
            const std::size_t slot = platform_slot;
            paragraph(p.body,
                      {screen.safe.x + margin, screen.safe.y + 51 * s,
                       screen.safe.width - 2 * margin, 28 * s},
                      13 * s, slot, white, 16 * s, 1, 1);
            describe(2,
                     {screen.safe.x + margin, screen.safe.y + 51 * s,
                      screen.safe.width - 2 * margin, 28 * s},
                     p.body);
            const double cancel_y = screen.safe.bottom() - margin - row_height;
            scroll_region = {screen.safe.x + margin, screen.safe.y + 85 * s,
                             screen.safe.width - 2 * margin,
                             std::max(0.0, cancel_y - gap - (screen.safe.y + 85 * s))};
            const std::size_t count = p.buttons.empty() ? 0 : p.buttons.size() - 1;
            const double content_height = count ? count * (row_height + gap) - gap : 0;
            scroll_maximum = std::max(0.0, content_height - scroll_region.height);
            scroll_position = std::clamp(scroll_position, 0.0, scroll_maximum);
            describe(3, scroll_region, p.title, {}, {}, {}, true, scroll_position < scroll_maximum,
                     scroll_position > 0);
            const auto draw_button = [&](const UiButton &button, Rect r) {
                solid(r, {{.68F, .22F, .26F, 1}});
                solid({r.x + s, r.y + s, r.width - 2 * s, r.height - 2 * s},
                      {{.18F, .024F, .045F, .96F}});
                line(button.label, {r.x + 8 * s, r.y + 2 * s, r.width - 16 * s, r.height - 4 * s},
                     15 * s, slot, 1, button.enabled ? white : Color{{.5F, .5F, .5F, 1}}, true);
                const auto visible = intersection(r, clip ? *clip : screen.full);
                if (visible.width > 0 && visible.height > 0)
                    target(visible, button.action, button.enabled, button.label);
            };
            const auto prior = clip;
            clip = intersection(scroll_region, prior ? *prior : screen.full);
            for (std::size_t n = 0; n < count; ++n) {
                const Rect r{scroll_region.x,
                             scroll_region.y + n * (row_height + gap) - scroll_position,
                             scroll_region.width, row_height};
                if (r.bottom() > scroll_region.y && r.y < scroll_region.bottom())
                    draw_button(p.buttons[n], r);
            }
            clip = prior;
            if (!p.buttons.empty())
                draw_button(p.buttons.back(), {screen.safe.x + margin, cancel_y,
                                               screen.safe.width - 2 * margin, row_height});
        } else if (p.source_menu) {
            menuBackground();
            line(p.title, screen.place(original_ui::heading_slot), 17 * s, 0, 1, white, true);
            describe(1, screen.place(original_ui::heading_slot), p.title);
            if (p.information) {
                scroll_region = screen.place(original_ui::information_text);
                const auto total =
                    paragraph(p.body, scroll_region, 12 * s, platform_slot, white, 14 * s,
                              std::numeric_limits<std::size_t>::max(), 0, scroll_position);
                scroll_maximum = std::max(0.0, total - scroll_region.height);
                describe(2, scroll_region, p.body, {}, {}, {}, true,
                         scroll_position < scroll_maximum, scroll_position > 0);
            } else if (p.details && !p.body.empty()) {
                const auto d = screen.source;
                paragraph(
                    p.body,
                    {d.x + d.width * .22, d.y + d.height * .833, d.width * .56, d.height * .125},
                    11 * s, platform_slot, white, 13 * s, 3, 1);
                describe(
                    2, {d.x + d.width * .22, d.y + d.height * .833, d.width * .56, d.height * .125},
                    p.body);
            }
            const int rows = original_ui::menu_row_count(static_cast<int>(p.buttons.size()));
            if (rows > 3)
                throw FormatError("Source menu exceeds the original nine-slot choices region");
            const double total_height = rows * 29.0 + (std::max(0, rows - 1)) * 4;
            const double center = p.information ? original_ui::information_action_center_y
                                                : original_ui::choices_center_y;
            for (std::size_t i = 0; i < p.buttons.size(); ++i) {
                const int row = static_cast<int>(i / 3), column = static_cast<int>(i % 3);
                const int row_count = std::min(3, static_cast<int>(p.buttons.size()) - row * 3);
                const double available = screen.virtual_width - 52;
                const double cell = (available - (row_count - 1) * 3) / row_count;
                const Rect r = source(
                    {26 + column * (cell + 3), center - total_height / 2 + row * 33, cell, 29});
                const auto &b = p.buttons[i];
                line(uppercase(b.label), {r.x + 4 * s, r.y + s, r.width - 8 * s, r.height - 2 * s},
                     15 * s, 6, 1, b.enabled ? white : Color{{1, 1, 1, .38F}}, true);
                target(r, b.action, b.enabled, b.label);
            }
        } else {
            solid(screen.full, {{.025F, .045F, .075F, 1}});
            const double phase = std::fmod(double(p.background_offset) / 65536.0, 48.0) * s;
            for (double y = phase - 48 * s; y < height; y += 48 * s)
                solid({0, y, double(width), s}, {{.5F, .5F, .5F, .06F}});
            line(p.title,
                 {screen.safe.x + 16 * s, screen.safe.y + 14 * s, screen.safe.width - 32 * s,
                  34 * s},
                 22 * s, 0, 0, white, true);
            describe(1,
                     {screen.safe.x + 16 * s, screen.safe.y + 14 * s, screen.safe.width - 32 * s,
                      34 * s},
                     p.title);
            const int rows = (static_cast<int>(p.buttons.size()) + 3) / 4;
            const double button_height = 40 * s, gap = 6 * s;
            scroll_region = {
                screen.safe.x + 16 * s, screen.safe.y + 62 * s, screen.safe.width - 32 * s,
                std::max(0.0, screen.safe.height - 74 * s - rows * (button_height + gap))};
            const double total =
                paragraph(p.body, scroll_region, 16 * s, platform_slot, white, 20 * s,
                          std::numeric_limits<std::size_t>::max(), 0, scroll_position);
            scroll_maximum = std::max(0.0, total - scroll_region.height);
            describe(2, scroll_region, p.body, {}, {}, {}, true, scroll_position < scroll_maximum,
                     scroll_position > 0);
            for (std::size_t i = 0; i < p.buttons.size(); ++i) {
                const int row = static_cast<int>(i / 4), column = static_cast<int>(i % 4);
                const int count = std::min(4, static_cast<int>(p.buttons.size()) - row * 4);
                const double cell = (screen.safe.width - 30 * s - (count - 1) * gap) / count;
                const Rect r{screen.safe.x + 15 * s + column * (cell + gap),
                             screen.safe.bottom() - 15 * s - (rows - row) * (button_height + gap),
                             cell, button_height};
                solid(r, {{.68F, .22F, .26F, 1}});
                solid({r.x + s, r.y + s, r.width - 2 * s, r.height - 2 * s},
                      {{.18F, .024F, .045F, .96F}});
                line(p.buttons[i].label,
                     {r.x + 4 * s, r.y + 2 * s, r.width - 8 * s, r.height - 4 * s}, 13 * s,
                     platform_slot, 1, p.buttons[i].enabled ? white : Color{{.5F, .5F, .5F, 1}},
                     true);
                target(r, p.buttons[i].action, p.buttons[i].enabled, p.buttons[i].label);
            }
        }
        scroll_position = std::clamp(scroll_position, 0.0, scroll_maximum);
    }
    void tactical(const UiTactical &t) {
        const auto s = screen.scale;
        const auto extra = screen.virtual_width - 480;
        if (t.visible) {
            const auto bar = screen.bottom(62);
            const auto p = [&](Rect r) {
                return Rect{bar.x + r.x * s, bar.y + r.y * s, r.width * s, r.height * s};
            };
            image(874, p({59, 11, 285 + extra, 50}));
            image(874, p({367 + extra, 10, 62, 52}));
            nine(514, {0, 0, 480, 62}, bar, 0, 72, 0, 142, false, s);
            fitImage(709, p({433 + extra, 11, 47, 50}));
            const auto id = t.shows_unit ? portrait(t.name, false) : 0;
            if (id)
                fitImage(id, p({1, 0, 60, 61}));
            if (t.shows_unit) {
                line(t.name, p({94, 21, 126 + extra, 18}), 13 * s, 4, 0, white, true);
                line(std::to_string(t.health), p({73, 39, 46, 18}), 12 * s, 4);
                line("ATK", p({217 + extra, 20, 49, 14}), 11 * s, 4);
                line("DEF", p({279 + extra, 20, 37, 14}), 11 * s, 4);
                line("SPD", p({316 + extra, 20, 37, 14}), 11 * s, 4);
                line(std::to_string(t.attack_low) + "-" + std::to_string(t.attack_high),
                     p({217 + extra, 39, 57, 17}), 12 * s, 4, 0, white, true);
                line(std::to_string(t.defense), p({282 + extra, 39, 34, 17}), 12 * s, 4);
                line(std::to_string(t.speed), p({318 + extra, 39, 32, 17}), 12 * s, 4);
            } else {
                line(t.name, p({85, 21, 243 + extra, 14}), 12 * s, 4, 0, white, true);
                line(t.objective, p({84, 37, 249 + extra, 17}), 9 * s, 2, 0, white, true);
            }
            line(std::to_string(t.funds), p({400 + extra, 39, 29, 16}), 11 * s, 4, 0, white, true);
            describe(
                4, bar,
                t.shows_unit
                    ? t.name + ". Health " + std::to_string(t.health) + ". Attack " +
                          std::to_string(t.attack_low) + " to " + std::to_string(t.attack_high) +
                          ". Defense " + std::to_string(t.defense) + ". Speed " +
                          std::to_string(t.speed) + ". Energon " + std::to_string(t.funds) + "."
                    : t.name + ". " + t.objective + ". Energon " + std::to_string(t.funds) + ".");
            if (t.tech_specs)
                target(p({0, 0, 60, 62}), {ui_tech_specs, 0});
        }
        if (!t.commands)
            return;
        const auto button = [&](Rect r, adaptive::Anchor anchor, int id, std::uint32_t on,
                                std::uint32_t off, bool enabled) {
            const auto rect = screen.place(r, anchor);
            fitImage(enabled ? on : off, rect);
            target(rect, {id, 0}, enabled, {}, {},
                   id == ui_upgrade && t.shows_unit ? std::to_string(t.upgrade_cost) + " Energon"
                                                    : std::string{});
        };
        using namespace original_presentation;
        button(source_left_actions[0], adaptive::Anchor::start, ui_heal, 669, 726, t.heal);
        button(source_left_actions[1], adaptive::Anchor::start, ui_attack, 660, 850, t.attack);
        button(source_left_actions[2], adaptive::Anchor::start, ui_undo, 793, 1076, t.undo);
        const bool deploying = t.deploy && !t.build;
        button(source_left_actions[3], adaptive::Anchor::start, deploying ? ui_deploy : ui_build,
               deploying ? 919 : 442, deploying ? 633 : 548, deploying ? t.deploy : t.build);
        button(source_right_actions[0], adaptive::Anchor::end, ui_move, 1118, 721, t.move);
        button(source_right_actions[1], adaptive::Anchor::end, ui_capture, 919, 633, t.capture);
        button(source_right_actions[2], adaptive::Anchor::end, ui_transform,
               t.vehicle_form ? 591 : 904, t.vehicle_form ? 1191 : 433, t.transform);
        button(source_right_actions[3], adaptive::Anchor::end, ui_upgrade, 1118, 721, t.upgrade);
        const auto end = screen.place(original_presentation::end_turn, adaptive::Anchor::center,
                                      adaptive::Anchor::start);
        fitImage(454, end);
        target(end, {ui_end_turn, 0}, t.end_turn);
    }
    Rect dialogueArea() const noexcept {
        const auto bar = screen.bottom(93);
        const auto s = screen.scale;
        return {bar.x + 174 * s, bar.y + 31 * s, (151 + screen.virtual_width - 480) * s, 54 * s};
    }
    DialogueLayout layoutDialogue(const UiDialogue &d) {
        const auto area = dialogueArea();
        const auto text = bitmap_font_utf8(d.text);
        const double glyph_height = std::max(8.0, 11 * screen.scale);
        if (!dialogue_ready || dialogue_identity != d.identity || dialogue_text != text ||
            dialogue_width != area.width || dialogue_height != area.height ||
            dialogue_font_height != glyph_height) {
            dialogue_identity = d.identity;
            dialogue_text = text;
            dialogue_width = area.width;
            dialogue_height = area.height;
            // iOS uses white Menlo/TextKit for dialogue. Android's explicit
            // white monospace adapter preserves case and readable color;
            // source bitmap faces remain authoritative for the game's HUD.
            dialogue_font_height = glyph_height;
            dialogue_slot = system_rasterizer ? 12 : chooseFont(2, text);
            if (dialogue_slot == 12) {
                dialogue_ink_height = systemText(U"Mg", glyph_height).height;
                dialogue_line_height = std::ceil(dialogue_ink_height) + 2;
            } else {
                const auto &descriptor = font(dialogue_slot).assets.descriptor;
                const auto metrics = descriptor.metrics();
                // Pixel geometry is measured from the original glyphs. iOS used
                // Menlo/TextKit here; its font rasterization is platform-specific.
                int low = std::numeric_limits<int>::max(), high = std::numeric_limits<int>::min();
                for (const auto &glyph : descriptor.glyphs())
                    if (glyph.height) {
                        low = std::min(low, metrics.draw_y_adjustment + glyph.offset_y);
                        high = std::max(high,
                                        metrics.draw_y_adjustment + glyph.offset_y + glyph.height);
                    }
                dialogue_ink_height =
                    std::ceil(std::max(1, high - low) * glyph_height / metrics.height);
                dialogue_line_height = std::ceil(std::max(glyph_height, dialogue_ink_height)) + 2;
            }
            dialogue_lines = wrap(text, area.width, glyph_height, dialogue_slot);
            dialogue_ready = true;
        }
        return {static_cast<std::int32_t>(dialogue_lines.size()),
                static_cast<std::int32_t>(dialogue_line_height),
                static_cast<std::int32_t>(dialogue_ink_height),
                0,
                static_cast<std::int32_t>(std::floor(area.height)),
                false};
    }
    DialogueLayout dialogue(const UiDialogue &d) {
        auto result = layoutDialogue(d);
        const auto bar = screen.bottom(93);
        const auto s = screen.scale;
        nine(1034, {0, 0, 480, 93}, bar, 12, 174, 8, 155, true, s);
        const auto id = portrait(d.heading, true);
        if (id)
            fitImage(id, {bar.x + 81 * s, bar.y + 17 * s, 78 * s, 69 * s});
        line(d.heading,
             {bar.x + 174 * s, bar.y + 17 * s, (157 + screen.virtual_width - 480) * s, 14 * s},
             10 * s, 0, 0, white, true);
        const auto area = dialogueArea();
        const auto first =
            std::min(std::size_t(std::max(0, d.progress.first_line)), dialogue_lines.size());
        std::size_t end = first;
        while (end < dialogue_lines.size() &&
               (end - first) * dialogue_line_height + dialogue_ink_height < area.height)
            ++end;
        if (end == first && end < dialogue_lines.size() && area.height >= dialogue_ink_height)
            ++end;
        bool full = true;
        std::string spoken;
        if (end > first) {
            const auto start = dialogue_lines[first].start;
            const auto finish = dialogue_lines[end - 1].consumed;
            const auto budget = std::size_t(std::max(0, dialogue_character_budget(d.progress)));
            const auto shown = std::min(budget, finish - start);
            full = shown >= finish - start;
            spoken = utf8(std::u32string_view(dialogue_text).substr(start, shown));
            for (std::size_t i = first; i < end; ++i) {
                const auto &row = dialogue_lines[i];
                const auto visible_end = std::min(row.end, start + shown);
                if (visible_end > row.start)
                    line(dialogue_text.substr(row.start, visible_end - row.start),
                         {area.x, area.y + (i - first) * dialogue_line_height, area.width,
                          dialogue_line_height},
                         dialogue_font_height, dialogue_slot);
            }
        } else if (first < dialogue_lines.size())
            full = false;
        result.visible_text_fully_revealed = full;
        target(bar, {ui_dialogue_next, 0}, true, d.heading + ". " + spoken + ". Tap to continue.");
        return result;
    }
    void combatant(const std::string &name, int health, double x, double y, std::uint8_t faction,
                   double scale) {
        const auto p = [&](Rect r) {
            return Rect{x + r.x * scale, y + r.y * scale, r.width * scale, r.height * scale};
        };
        image(faction == 0 ? 874 : 799, p({6, 7, 144, 50}));
        image(1038, p({0, 0, 156, 58}));
        line(std::to_string(health), p({61, 16, 79, 17}), 16 * scale, faction == 0 ? 9 : 10, 1,
             white, true);
        line(name, p({25, 36, 113, 18}), 12 * scale, faction == 0 ? 6 : 7, 1, white, true);
    }
    void battle(const UiBattle &b) {
        const auto s = screen.scale;
        const auto &status = b.status;
        if (status.health_visible) {
            combatant(b.left_name, status.display_health[0], screen.safe.x,
                      screen.safe.bottom() - 58 * s, b.left_faction, s);
            combatant(b.right_name, status.display_health[1], screen.safe.right() - 156 * s,
                      screen.safe.bottom() - 58 * s, b.right_faction, s);
            describe(5, screen.bottom(58),
                     b.left_name + " HP " + std::to_string(status.display_health[0]) + ". " +
                         b.right_name + " HP " + std::to_string(status.display_health[1]) + ".");
        }
        if (status.damage_text) {
            const double x =
                screen.safe.x +
                screen.safe.width * (b.damage_anchor
                                         ? std::clamp(double((*b.damage_anchor)[0]), .08, .92)
                                         : (status.damage_on_left ? .25 : .75));
            const double y =
                screen.safe.y +
                screen.safe.height *
                    (b.damage_anchor ? std::clamp(double((*b.damage_anchor)[1]), .12, .70) : .35);
            const double lift = std::clamp(double(status.phase_time_ms) / 40, 0.0, 14.0) * s;
            line(std::to_string(*status.damage_text), {x - 55 * s, y - lift, 110 * s, 45 * s},
                 34 * s, 3, 1, white, true);
        }
    }
    Rect modelViewport() const noexcept {
        return source({123, 36, 232 + screen.virtual_width - 480, 274});
    }
    void specs(const UiTechSpecs &t, const std::function<void(Rect)> &model) {
        const auto s = screen.scale, extra = screen.virtual_width - 480;
        const auto background = t.data.faction == 0 ? 874U : 799U;
        tile(background, {0, 0, 128, 63}, screen.full, 1);
        nine(t.data.faction == 0 ? 1224 : 417, {0, 0, 480, 320}, source({0, 0, 480 + extra, 320}),
             36, 123, 12, 125, true, s);
        // The iOS model was a sibling MTKView above the source background.
        // Flush the frame artwork before invoking the shared M3G renderer.
        if (model) {
            flush();
            model(modelViewport());
        }
        const auto accent = t.data.faction == 0 ? 4U : 5U, title = t.data.faction == 0 ? 6U : 7U;
        line(t.name, source({115, 4, 253 + extra, 25}), 18 * s, title, 1, white, true);
        const auto &robot = t.data.form[0];
        const auto &vehicle = t.data.form[1];
        describe(
            6, source({115, 4, 253 + extra, 25}),
            "Tech Specs: " + t.name + ". Robot attack " + std::to_string(robot.combat.attack_low) +
                " to " + std::to_string(robot.combat.attack_high) + ", defense " +
                std::to_string(robot.combat.defense) + ", speed " + std::to_string(robot.speed) +
                ". Vehicle attack " + std::to_string(vehicle.combat.attack_low) + " to " +
                std::to_string(vehicle.combat.attack_high) + ", defense " +
                std::to_string(vehicle.combat.defense) + ", speed " +
                std::to_string(vehicle.speed) + ". Energon cost " +
                std::to_string(t.data.energon_price) + ". " + t.quote);
        if (model)
            describe(7, modelViewport(), "Original rotating Transformer 3D model. Drag to rotate.");
        for (std::size_t form = 0; form < 2 && form < t.data.forms; ++form) {
            const auto &entry = t.data.form[form];
            const double x = form == 0 ? 78 : 363 + extra;
            line("ATK", source({x, 51, 39, 16}), 13 * s, accent, 1);
            const auto attack = entry.combat.attack_low <= 0 && entry.combat.attack_high <= 0
                                    ? std::string("N/A")
                                    : std::to_string(entry.combat.attack_low) + "-" +
                                          std::to_string(entry.combat.attack_high);
            line(attack, source({x - 3, 65, 45, 14}), 11 * s, 2, 1, white, true);
            line("DEF", source({x, 77, 39, 16}), 13 * s, accent, 1);
            line(std::to_string(entry.combat.defense), source({x - 3, 91, 45, 14}), 11 * s, 2, 1);
            line("SPD", source({x, 101, 39, 16}), 13 * s, accent, 1);
            line(std::to_string(entry.speed), source({x - 3, 115, 45, 14}), 11 * s, 2, 1);
        }
        image(t.data.faction == 0 ? 442 : 657, source({74, 139, 42, 39}));
        if (t.data.faction == 0)
            image(904, source({363 + extra, 139, 42, 39}));
        double y = 184;
        for (const auto &detail : t.details) {
            paragraph(detail, source({14, y, 139, 19}), 8 * s, 2, white, 9.5 * s, 2);
            y += 11;
            if (y > 225)
                break;
        }
        paragraph(t.quote, source({360 + extra, 183, 107, 40}), 8 * s, 2, white, 9.5 * s, 4);
        image(709, source({44, 247, 37, 38}));
        line(std::to_string(int(t.data.energon_price)), source({49, 287, 30, 20}), 16 * s, title,
             1);
        if (t.portrait_resource)
            fitImage(t.portrait_resource, source({387 + extra, 246, 62, 63}));
        target(source({0, 0, 62, 53}), {ui_specs_back, 0});
        target(source({0, 65, 73, 83}), {ui_specs_previous, 0}, t.previous);
        target(source({405 + extra, 65, 75, 83}), {ui_specs_next, 0}, t.next);
        target(source({75, 137, 48, 43}), {ui_specs_robot, 0}, t.robot);
        target(source({357 + extra, 137, 48, 43}), {ui_specs_vehicle, 0},
               t.vehicle && t.data.forms > 1);
    }
};

AndroidUI::AndroidUI(GLESRenderer &renderer) : impl_(std::make_unique<Impl>(renderer)) {}
AndroidUI::~AndroidUI() = default;
void AndroidUI::setSystemTextRasterizer(
    std::function<RgbaImage(const std::string &, float)> rasterizer) {
    impl_->system_rasterizer = std::move(rasterizer);
    impl_->system_text.clear();
    impl_->dialogue_ready = false;
}
void AndroidUI::initialize(const Archive &archive, const std::string &root) {
    auto &i = *impl_;
    i.archive = &archive;
    i.resource_root = root;
    i.pictures.clear();
    i.fonts.clear();
    i.quads.clear();
    i.dialogue_ready = false;
    i.white_texture = i.renderer.texture(RgbaImage{1, 1, Bytes{255, 255, 255, 255}});
    i.verifyPicture(1002, 480, 320);
    i.verifyPicture(514, 480, 62);
    i.verifyPicture(1034, 480, 93);
    i.verifyPicture(1038, 156, 58);
    i.verifyPicture(1224, 480, 320);
    i.verifyPicture(417, 480, 320);
    for (const auto id : {874U, 799U})
        i.verifyPicture(id, 128, 63);
    i.verifyPicture(709, 51, 54);
    for (const auto id : {1118U, 721U, 919U, 633U, 904U, 433U, 660U, 850U, 669U, 726U, 454U, 442U,
                          548U, 793U, 1076U, 591U, 1191U, 657U})
        i.verifyPicture(id, 52, 49);
    for (const auto id : {905U, 719U})
        i.verifyPicture(id, 60, 49);
    for (const auto slot : {0U, 2U, 3U, 4U, 5U, 6U, 7U, 9U, 10U})
        i.font(slot);
}
void AndroidUI::resize(int width, int height, adaptive::Insets insets) {
    if (width < 0 || height < 0)
        throw FormatError("Negative Android UI dimensions");
    auto &i = *impl_;
    i.width = width;
    i.height = height;
    i.screen = adaptive::layout(width, height, insets);
    i.dialogue_ready = false;
    i.targets.clear();
    i.accessible_nodes.clear();
}
DialogueLayout AndroidUI::dialogueLayout(const UiDialogue &dialogue) {
    if (impl_->screen.scale <= 0)
        return {};
    return impl_->layoutDialogue(dialogue);
}
UiReceipt AndroidUI::render(const UiState &state,
                            const std::function<void(adaptive::Rect)> &draw_specs_model) {
    auto &i = *impl_;
    UiReceipt receipt{};
    i.draws.clear();
    i.targets.clear();
    i.accessible_nodes.clear();
    i.clip.reset();
    if (i.screen.scale <= 0)
        return receipt;
    if (state.panel) {
        i.panel(*state.panel);
        receipt.panel_token = state.panel->token;
    } else if (state.tech_specs) {
        i.specs(*state.tech_specs, draw_specs_model);
    } else {
        i.scroll_region = {};
        i.scroll_maximum = 0;
        if (state.battle) {
            i.battle(*state.battle);
            receipt.battle_token = state.battle->status.token;
            receipt.battle_revision = state.battle->status.revision;
        } else if (state.dialogue) {
            receipt.dialogue_identity = state.dialogue->identity;
            receipt.dialogue_layout = i.dialogue(*state.dialogue);
        } else {
            i.tactical(state.tactical);
            receipt.tactical_drawn = state.tactical.visible && state.support_banner.empty();
        }
        if (state.show_menu && !state.battle) {
            const auto rect = i.screen.place(original_presentation::top_left_action,
                                             adaptive::Anchor::start, adaptive::Anchor::start);
            i.image(state.dialogue ? 719 : 905, rect);
            i.target(rect, {state.dialogue ? ui_dialogue_skip : ui_menu, 0});
        }
        if (!state.support_banner.empty()) {
            const auto s = i.screen.scale;
            const auto rect = Rect{i.screen.safe.x + 70 * s, i.screen.safe.y + 8 * s,
                                   i.screen.safe.width - 140 * s, 32 * s};
            i.line(state.support_banner, rect, 18 * s, 0, 1, white, true);
            receipt.support_banner_drawn = true;
            i.describe(8, rect, state.support_banner);
        }
        if (!state.notice.empty()) {
            const auto s = i.screen.scale;
            const auto rect = Rect{i.screen.safe.x + 65 * s, i.screen.safe.y + 51 * s,
                                   i.screen.safe.width - 130 * s, 27 * s};
            i.solid(rect, {{0, 0, 0, .65F}});
            i.line(state.notice, rect, 12 * s, 2, 1, white, true);
            i.describe(9, rect, state.notice);
        }
        if (!state.diagnostics.empty()) {
            const auto s = i.screen.scale;
            const auto rect = Rect{i.screen.safe.x + 65 * s, i.screen.safe.y + 1 * s,
                                   i.screen.safe.width - 130 * s, 21 * s};
            i.line(state.diagnostics, rect, 10 * s, 2, 1, white, true);
            i.describe(10, rect, state.diagnostics);
        }
    }
    i.flush();
    return receipt;
}
UiAction AndroidUI::hit(float x, float y) const noexcept {
    if (!std::isfinite(x) || !std::isfinite(y))
        return {};
    for (auto it = impl_->targets.rbegin(); it != impl_->targets.rend(); ++it)
        if (inside(it->rect, x, y))
            return it->enabled ? it->action : UiAction{};
    return {};
}
const std::vector<UiAccessibleNode> &AndroidUI::accessibleNodes() const noexcept {
    return impl_->accessible_nodes;
}
bool AndroidUI::scroll(float dy) {
    if (!std::isfinite(dy) || impl_->scroll_maximum <= 0)
        return false;
    impl_->scroll_position =
        std::clamp(impl_->scroll_position - double(dy), 0.0, impl_->scroll_maximum);
    return true;
}
adaptive::Rect AndroidUI::specsModelViewport() const noexcept {
    return impl_->modelViewport();
}
const adaptive::Layout &AndroidUI::layout() const noexcept {
    return impl_->screen;
}
} // namespace g1::android
