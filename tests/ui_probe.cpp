#include "AndroidUI.hpp"
#include "GLESRenderer.hpp"
#include "g1/BitmapFont.hpp"
#include "g1/Image.hpp"
#include "g1/Text.hpp"
#include "g1/World.hpp"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string json(const std::string &text) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c < 32) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else
            out += char(c);
    }
    return out + '"';
}
class FontWorker {
    int input_{-1}, output_{-1};
    pid_t child_{-1};
    static void readAll(int fd, void *data, std::size_t size) {
        auto *out = static_cast<unsigned char *>(data);
        while (size) {
            const auto n = read(fd, out, size);
            require(n > 0, "System font worker ended unexpectedly");
            out += n;
            size -= n;
        }
    }

  public:
    FontWorker() {
        int input[2], output[2];
        require(pipe(input) == 0 && pipe(output) == 0, "Font worker pipes failed");
        child_ = fork();
        require(child_ >= 0, "Font worker fork failed");
        if (child_ == 0) {
            dup2(input[0], STDIN_FILENO);
            dup2(output[1], STDOUT_FILENO);
            close(input[0]);
            close(input[1]);
            close(output[0]);
            close(output[1]);
            execlp("python3", "python3", "tests/ui_system_font.py", "--server",
                   static_cast<char *>(nullptr));
            _exit(127);
        }
        close(input[0]);
        close(output[1]);
        input_ = input[1];
        output_ = output[0];
    }
    ~FontWorker() {
        if (input_ >= 0)
            close(input_);
        if (output_ >= 0)
            close(output_);
        if (child_ > 0)
            waitpid(child_, nullptr, 0);
    }
    g1::RgbaImage operator()(const std::string &text, float height) {
        const auto request =
            "{\"text\":" + json(text) + ",\"size\":" + std::to_string(height) + "}\n";
        std::size_t written = 0;
        while (written < request.size()) {
            const auto n = write(input_, request.data() + written, request.size() - written);
            require(n > 0, "Font worker request failed");
            written += n;
        }
        std::uint32_t header[3];
        readAll(output_, header, sizeof(header));
        require(header[0] > 0 && header[1] > 0 &&
                    std::uint64_t(header[0]) * header[1] * 4 == header[2] &&
                    header[2] <= 64U * 1024U * 1024U,
                "Font worker response is invalid");
        g1::RgbaImage image{header[0], header[1], g1::Bytes(header[2])};
        readAll(output_, image.pixels.data(), image.pixels.size());
        return image;
    }
};
void image(const std::string &destination, int width, int height) {
    g1::RgbaImage result{static_cast<unsigned>(width), static_cast<unsigned>(height),
                         g1::Bytes(std::size_t(width) * height * 4)};
    g1::Bytes pixels(result.pixels.size());
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    require(glGetError() == GL_NO_ERROR, "UI pixel readback failed");
    for (int y = 0; y < height; ++y)
        std::copy_n(pixels.begin() + std::size_t(height - 1 - y) * width * 4, width * 4,
                    result.pixels.begin() + std::size_t(y) * width * 4);
    const auto png = g1::encode_png(result);
    std::ofstream out(destination, std::ios::binary);
    out.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    require(bool(out), "UI PNG write failed");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("usage: ui_probe GAME_RESOURCE_ROOT OUTPUT_DIRECTORY");
        FontWorker font_worker;
        const std::string root = argv[1], output = argv[2];
        std::filesystem::create_directories(output);
        const g1::Archive archive(g1::read_file(root + "/GameData/iphone.big"));
        const auto text = g1::load_text_table(archive);
        std::vector<g1::BitmapFontAssets> fonts;
        for (std::size_t i = 0; i < 12; ++i)
            fonts.push_back(g1::load_bitmap_font_assets(archive, i));
        std::set<char32_t> missing;
        std::size_t entries = 0, single_face = 0;
        std::set<std::uint16_t> system_entries;
        for (const auto &entry : text.entries()) {
            const auto chars = g1::bitmap_font_utf8(entry.utf8);
            ++entries;
            bool any_face = false;
            for (const auto &font : fonts) {
                const bool covers = std::all_of(chars.begin(), chars.end(), [&](char32_t c) {
                    return c == U'\n' || c == U'\r' || font.descriptor.contains(c);
                });
                any_face = any_face || covers;
            }
            if (any_face)
                ++single_face;
            else
                system_entries.insert(entry.id);
            for (const auto c : chars)
                if (c != U'\n' && c != U'\r' &&
                    std::none_of(fonts.begin(), fonts.end(),
                                 [&](const auto &font) { return font.descriptor.contains(c); }))
                    missing.insert(c);
        }
        std::cout << "Font coverage: " << single_face << "/" << entries
                  << " entries fit one original face; unsupported scalars:";
        for (auto c : missing)
            std::cout << " U+" << std::hex << unsigned(c) << std::dec;
        std::cout << "\n";
        const auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        require(display != EGL_NO_DISPLAY, "No EGL display");
        EGLint major{}, minor{};
        require(eglInitialize(display, &major, &minor), "EGL initialization failed");
        require(eglBindAPI(EGL_OPENGL_ES_API), "EGL ES API unavailable");
        const EGLint attributes[] = {EGL_SURFACE_TYPE,
                                     EGL_PBUFFER_BIT,
                                     EGL_RENDERABLE_TYPE,
                                     0x0040,
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
        EGLint count{};
        require(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1,
                "No ES3 pbuffer configuration");
        constexpr int width = 2340, height = 1080;
        const EGLint buffer_attributes[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
        const auto surface = eglCreatePbufferSurface(display, config, buffer_attributes);
        const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        const auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
        require(context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE,
                "EGL pbuffer/context creation failed");
        require(eglMakeCurrent(display, surface, surface, context), "EGL make current failed");
        std::cout << "GL renderer: " << glGetString(GL_RENDERER) << "\n";
        {
            g1::GLESRenderer renderer;
            g1::android::AndroidUI ui(renderer);
            const auto system_font = [&](const std::string &value, float height) {
                // The production adapter uses Android Canvas. This host-only
                // contract uses a real DejaVu monospace rasterizer, keeping
                // the platform font difference explicit in the report.
                return font_worker(value, height);
            };
            ui.setSystemTextRasterizer(system_font);
            ui.initialize(archive, root);
            ui.resize(width, height, {0, 72, 0, 0});
            const auto clear = [&] {
                glViewport(0, 0, width, height);
                glClearColor(.025F, .04F, .065F, 1);
                glDepthMask(GL_TRUE);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            };
            const auto accessible = [&] {
                std::set<int> ids;
                for (const auto &node : ui.accessibleNodes()) {
                    require(node.id >= 0 && ids.insert(node.id).second,
                            "Accessibility IDs are missing or duplicated");
                    require(!node.label.empty(),
                            "Rendered accessibility node lacks its source label");
                    require(node.bounds.x >= 0 && node.bounds.y >= 0 && node.bounds.width > 0 &&
                                node.bounds.height > 0 && node.bounds.right() <= width + .001 &&
                                node.bounds.bottom() <= height + .001,
                            "Accessibility bounds exceed actual visible surface");
                    if (node.action) {
                        const auto hit = ui.hit(float(node.bounds.x + node.bounds.width / 2),
                                                float(node.bounds.y + node.bounds.height / 2));
                        require(
                            node.enabled
                                ? (hit.id == node.action.id && hit.value == node.action.value)
                                : !hit,
                            "Accessibility action does not match ordinary hit/disabled semantics");
                    }
                }
            };
            using namespace g1::android;
            UiState state{};
            state.tactical.visible = false;
            UiPanel menu{};
            menu.title = text.at(1169);
            menu.token = 1;
            menu.buttons = {{text.at(471), {100, 0}},
                            {text.at(468), {101, 0}},
                            {text.at(754), {102, 0}},
                            {"CREDITS", {103, 0}}};
            state.panel = menu;
            clear();
            auto receipt = ui.render(state);
            require(receipt.panel_token == 1, "Source menu token was not drawn");
            require(!receipt.dialogue_layout && !receipt.battle_token,
                    "Menu emitted a hidden content receipt");
            accessible();
            require(ui.accessibleNodes().size() == 5,
                    "Source menu accessibility labels are missing");
            image(output + "/ui-contract-menu-a36.png", width, height);
            state.panel->title = "CREDITS";
            state.panel->information = true;
            state.panel->token = 2;
            state.panel->body = "Transformers: G1 Awakening\nFaithful native Android port by "
                                "iammrmikeman.\nThe original game assets, campaign, characters, "
                                "and native iOS gameplay source are preserved.";
            state.panel->buttons = {{"BACK", {104, 0}}};
            clear();
            ui.render(state);
            image(output + "/ui-contract-information-a36.png", width, height);
            // Production is a platform action list, not a source nine-slot
            // menu. Stress it with every decoded source unit name/cost;
            // eligibility is owned and separately checked by GameSession.
            UiPanel production{};
            production.title = text.at(785);
            production.body = "Energy: 999";
            production.token = 3;
            production.source_menu = false;
            production.action_list = true;
            const auto world = g1::bootstrap_first_campaign(archive);
            for (const auto &unit : world.unit_definitions)
                production.buttons.push_back(
                    {text.at(g1::script_text_id(g1::unit_text_index(unit.raw_text_indices[0]))) +
                         " · " + std::to_string(g1::unit_spawn_cost(unit)),
                     {150, int(unit.type)}});
            require(production.buttons.size() > 9,
                    "Production stress list does not exceed original menu capacity");
            const auto last_production_type = production.buttons.back().action.value;
            production.buttons.push_back({"Cancel", {151, 0}});
            state.panel = production;
            clear();
            receipt = ui.render(state);
            require(receipt.panel_token == 3 && !receipt.tactical_drawn,
                    "Production panel receipt is incorrect");
            const auto list_layout = ui.layout();
            const float list_x = float(list_layout.safe.x + list_layout.safe.width / 2);
            const float first_y = float(list_layout.safe.y + 105 * list_layout.scale);
            require(ui.hit(list_x, first_y).id == 150 && ui.hit(list_x, first_y).value == 0,
                    "First production choice is unavailable");
            const float cancel_y = float(list_layout.safe.bottom() - 36 * list_layout.scale);
            require(ui.hit(list_x, cancel_y).id == 151,
                    "Production Cancel is unavailable before scrolling");
            require(ui.scroll(-float(height) * 100), "Overflowing production list did not scroll");
            clear();
            receipt = ui.render(state);
            bool last_choice_seen = false, first_choice_seen = false;
            for (int y = 0; y < height; ++y) {
                const auto hit = ui.hit(list_x, float(y));
                if (hit.id == 150 && hit.value == last_production_type)
                    last_choice_seen = true;
                if (hit.id == 150 && hit.value == 0)
                    first_choice_seen = true;
            }
            require(last_choice_seen && !first_choice_seen,
                    "Scrolled production list has missing or stale hit regions");
            require(ui.hit(list_x, cancel_y).id == 151, "Production Cancel scrolled out of reach");
            accessible();
            bool hidden_accessible = false, final_accessible = false, scroll_accessible = false;
            for (const auto &node : ui.accessibleNodes()) {
                if (node.action.id == 150 && node.action.value == 0)
                    hidden_accessible = true;
                if (node.action.id == 150 && node.action.value == last_production_type)
                    final_accessible = true;
                if (node.scroll_backward && !node.scroll_forward)
                    scroll_accessible = true;
            }
            require(!hidden_accessible && final_accessible && scroll_accessible,
                    "Production accessibility tree contains hidden rows or lacks scrolling");
            image(output + "/ui-contract-production-scroll-a36.png", width, height);
            UiPanel result{};
            result.title = text.at(1323);
            result.body = text.at(393);
            result.token = 4;
            result.source_menu = false;
            result.buttons = {{text.at(587), {152, 0}, false}};
            state.panel = result;
            clear();
            receipt = ui.render(state);
            require(receipt.panel_token == 4 && !receipt.dialogue_layout && !receipt.battle_token &&
                        !receipt.tactical_drawn,
                    "Generic results/message panel emitted an incorrect receipt");
            require(!ui.hit(list_x, cancel_y),
                    "Disabled results action accepted an input before host presentation");
            accessible();
            require(std::any_of(
                        ui.accessibleNodes().begin(), ui.accessibleNodes().end(),
                        [](const auto &node) { return node.action.id == 152 && !node.enabled; }),
                    "Disabled results control was omitted from accessibility");
            image(output + "/ui-contract-results-a36.png", width, height);
            state.panel.reset();
            state.tactical.visible = true;
            state.tactical.commands = true;
            state.tactical.shows_unit = true;
            state.tactical.name = "BUMBLEBEE";
            state.tactical.health = 100;
            state.tactical.attack_low = 15;
            state.tactical.attack_high = 25;
            state.tactical.defense = 8;
            state.tactical.speed = 5;
            state.tactical.funds = 40;
            state.tactical.move = true;
            state.tactical.transform = true;
            state.tactical.end_turn = true;
            state.tactical.tech_specs = true;
            state.tactical.upgrade_cost = 20;
            clear();
            ui.render(state);
            image(output + "/ui-contract-tactical-a36.png", width, height);
            accessible();
            int accessible_move_id = 0;
            for (const auto &node : ui.accessibleNodes())
                if (node.action.id == ui_move)
                    accessible_move_id = node.id;
            require(accessible_move_id > 0, "Source Move accessibility label missing");
            const auto &layout = ui.layout();
            const auto move = layout.place({428, 50, 52, 49}, g1::adaptive::Anchor::end);
            require(ui.hit(float(move.x + move.width / 2), float(move.y + move.height / 2)).id ==
                        ui_move,
                    "Move hit target differs from artwork");
            state.tactical.move = false;
            clear();
            ui.render(state);
            require(!ui.hit(float(move.x + move.width / 2), float(move.y + move.height / 2)),
                    "Disabled move icon accepted action");
            accessible();
            require(std::any_of(ui.accessibleNodes().begin(), ui.accessibleNodes().end(),
                                [&](const auto &node) {
                                    return node.id == accessible_move_id && node.label == "Move" &&
                                           !node.enabled;
                                }),
                    "Disabled Move lost its source label or stable identity");
            require(std::any_of(ui.accessibleNodes().begin(), ui.accessibleNodes().end(),
                                [](const auto &node) {
                                    return node.action.id == ui_upgrade &&
                                           node.value == "20 Energon";
                                }),
                    "Source upgrade accessibility value missing");
            UiDialogue dialogue{};
            dialogue.identity = 17;
            dialogue.heading = "BUMBLEBEE";
            auto entry =
                std::find_if(text.entries().begin(), text.entries().end(), [](const auto &e) {
                    return e.utf8.size() > 220 && e.utf8.size() < 450 &&
                           e.utf8.find("Autobot") != std::string::npos;
                });
            require(entry != text.entries().end(), "No original sample dialogue found");
            dialogue.text = entry->utf8;
            dialogue.progress.reveal_offset = 0;
            state.dialogue = dialogue;
            const auto before = ui.dialogueLayout(dialogue);
            require(!before.visible_text_fully_revealed, "Layout falsely reported completed draw");
            clear();
            receipt = ui.render(state);
            require(receipt.dialogue_identity == 17 && receipt.dialogue_layout.has_value(),
                    "Dialogue receipt identity mismatch");
            require(receipt.dialogue_layout->visible_text_fully_revealed,
                    "Full dialogue page did not acknowledge actual glyph rendering");
            require(receipt.dialogue_layout->line_count > 0 &&
                        receipt.dialogue_layout->line_height > 0,
                    "Dialogue metrics are empty");
            accessible();
            image(output + "/ui-contract-dialogue-a36.png", width, height);
            state.dialogue->progress.reveal_offset = -65536000;
            clear();
            receipt = ui.render(state);
            require(!receipt.dialogue_layout->visible_text_fully_revealed,
                    "Unrevealed dialogue falsely reported complete");
            accessible();
            require(std::any_of(ui.accessibleNodes().begin(), ui.accessibleNodes().end(),
                                [](const auto &node) {
                                    return node.action.id == ui_dialogue_next &&
                                           node.label == "BUMBLEBEE. . Tap to continue.";
                                }),
                    "Accessibility exposed dialogue before it was revealed");
            state.dialogue.reset();
            UiBattle battle{};
            battle.status.token = 42;
            battle.status.revision = 3;
            battle.status.health_visible = true;
            battle.status.display_health = {{73, 35}};
            battle.status.damage_text = 17;
            battle.status.phase_time_ms = 320;
            battle.left_name = "BUMBLEBEE";
            battle.right_name = "MEGATRON";
            battle.left_faction = 0;
            battle.right_faction = 1;
            state.battle = battle;
            clear();
            receipt = ui.render(state);
            require(receipt.battle_token == 42 && receipt.battle_revision == 3,
                    "Battle HUD receipt mismatch");
            image(output + "/ui-contract-battle-a36.png", width, height);
            state.battle.reset();
            UiTechSpecs specs{};
            specs.name = "BUMBLEBEE";
            specs.data.faction = 0;
            specs.data.forms = 2;
            specs.data.energon_price = 20;
            specs.data.form[0].combat.attack_low = 15;
            specs.data.form[0].combat.attack_high = 25;
            specs.data.form[0].combat.defense = 8;
            specs.data.form[0].speed = 5;
            specs.data.form[1].speed = 7;
            specs.portrait_resource = 731;
            specs.quote = "The least likely can be the most dangerous.";
            specs.details = {"ATTACK RANGE: 1", text.at(807), text.at(714), text.at(929)};
            state.tech_specs = specs;
            clear();
            ui.render(state);
            image(output + "/ui-contract-specs-a36.png", width, height);
            accessible();
            state.tech_specs.reset();
            state.dialogue = dialogue;
            // Independently exercise source-font geometry across every
            // covered source string; Android dialogue uses system monospace.
            ui.setSystemTextRasterizer({});
            std::size_t dialogue_layouts = 0;
            for (const auto &value : text.entries()) {
                if (system_entries.count(value.id))
                    continue;
                state.dialogue->text = value.utf8;
                state.dialogue->identity = 1000 + value.id;
                const auto metrics = ui.dialogueLayout(*state.dialogue);
                require(metrics.line_height > 0 && metrics.box_height > 0,
                        "Original text layout failed");
                ++dialogue_layouts;
            }
            ui.setSystemTextRasterizer(system_font);
            // Exact original legal text needing platform fallback uses the
            // scrollable body surface, matching the source information view.
            for (const auto id : system_entries) {
                UiPanel p{};
                p.title = "ORIGINAL LEGAL TEXT";
                p.body = text.at(id);
                p.token = 2000 + id;
                p.source_menu = false;
                state.dialogue.reset();
                state.panel = p;
                clear();
                receipt = ui.render(state);
                require(receipt.panel_token == p.token,
                        "Platform fallback did not emit a real draw receipt");
            }
            image(output + "/ui-contract-system-font-fallback.png", width, height);
            require(glGetError() == GL_NO_ERROR, "Unexpected GL error after UI probes");
            std::ofstream report(output + "/ui-contract-report.json");
            report
                << "{\n  \"kind\": \"offscreen UI contract, not Android device gameplay\",\n"
                << "  \"width\": " << width << ", \"height\": " << height << ",\n"
                << "  \"source_text_entries\": " << entries
                << ", \"single_font_coverage\": " << single_face << ",\n"
                << "  \"source_font_dialogue_layouts\": " << dialogue_layouts
                << ", \"platform_fallback_entries\": " << system_entries.size() << ",\n"
                << "  \"production_dialogue_font\": \"Android white monospace (iOS uses Menlo)\",\n"
                << "  \"platform_fallback\": \"U+00AE preserved; Android uses Canvas, this probe "
                   "uses DejaVuSansMono\",\n"
                << "  \"menu_and_battle_tokens\": true, \"dialogue_reveal_feedback\": true,\n"
                << "  \"production_choices_scroll_beyond_nine\": true, "
                   "\"production_cancel_fixed\": true,\n"
                << "  \"generic_panel_receipt_and_disabled_action\": true,\n"
                << "  \"accessibility_source_labels_visible_bounds_and_disabled_actions\": true,\n"
                << "  \"accessibility_dialogue_reveal_and_scroll_rows\": true,\n"
                << "  \"safe_area_hit_geometry\": true, \"disabled_controls_rejected\": true\n}\n";
            std::cout << "UI contracts passed; original text layouts=" << dialogue_layouts << "\n";
        }
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "UI probe failed: " << e.what() << "\n";
        return 1;
    }
}
