#include "AndroidGame.hpp"
#include "AndroidUI.hpp"
#include "g1/Image.hpp"
#include "g1/MissionExecution.hpp"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {
// Host-test linker wrapping is the only failure injection. The Android APK
// neither compiles this probe nor exposes any runtime failure switch.
bool reject_next_scene_framebuffer = false;
}
extern "C" GLenum __real_glCheckFramebufferStatus(GLenum target);
extern "C" GLenum __wrap_glCheckFramebufferStatus(GLenum target) {
    if (reject_next_scene_framebuffer && target == GL_FRAMEBUFFER) {
        reject_next_scene_framebuffer = false;
        return GL_FRAMEBUFFER_UNSUPPORTED;
    }
    return __real_glCheckFramebufferStatus(target);
}

namespace {
constexpr int width = 2340, height = 1080;
void require(bool condition, const std::string &message) {
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
std::string field(const std::string &document, const std::string &key) {
    const auto at = document.find('"' + key + "\":");
    if (at == std::string::npos)
        return {};
    const auto start = at + key.size() + 3;
    if (document[start] == '"') {
        auto end = start + 1;
        while (end < document.size()) {
            if (document[end] == '\\') {
                end += 2;
                continue;
            }
            if (document[end] == '"')
                return document.substr(start + 1, end - start - 1);
            ++end;
        }
        throw std::runtime_error("Unterminated diagnostics string: " + key);
    }
    const auto end = document.find_first_of(",}", start);
    return document.substr(start, end - start);
}
std::uint64_t number(const std::string &document, const std::string &key) {
    const auto value = field(document, key);
    require(!value.empty() && value != "null", "Missing numeric diagnostics field: " + key);
    return std::stoull(value);
}
bool boolean(const std::string &document, const std::string &key) {
    const auto value = field(document, key);
    require(value == "true" || value == "false", "Missing boolean diagnostics field: " + key);
    return value == "true";
}
std::string accessible_label(const std::string &document, std::uint64_t id) {
    static const std::regex object(R"re(\{[^{}]+\})re");
    for (std::sregex_iterator it(document.begin(), document.end(), object), end; it != end; ++it) {
        const auto node = it->str();
        if (number(node, "id") == id)
            return field(node, "label");
    }
    throw std::runtime_error("Missing presented accessibility node " + std::to_string(id));
}
struct Target {
    int id{}, value{};
    float x{}, y{};
};
g1::Bytes portrait_pixels(const Target &target) {
    // Read the center of the visible portrait through its ordinary Tech Specs
    // touch target, avoiding any private UI state or asset-loader assumption.
    constexpr int edge = 12;
    g1::Bytes pixels(edge * edge * 4);
    glReadPixels(std::clamp(int(target.x) - edge / 2, 0, width - edge),
                 std::clamp(height - int(target.y) - edge / 2, 0, height - edge),
                 edge, edge, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    require(glGetError() == GL_NO_ERROR, "Portrait readback failed");
    return pixels;
}
struct UnitTarget {
    std::uint64_t index{}, form{};
    int tile_x{}, tile_y{};
    float x{}, y{};
};
std::optional<UnitTarget> unit_for_faction(const std::string &document, std::uint64_t faction) {
    const auto start = document.find("\"unit_targets\":[");
    if (start == std::string::npos)
        return {};
    const auto units = document.substr(start, document.find(']', start) - start);
    static const std::regex object(R"re(\{[^{}]+\})re");
    for (std::sregex_iterator it(units.begin(), units.end(), object), end; it != end; ++it) {
        const auto value = it->str();
        if (boolean(value, "active") && number(value, "faction") == faction)
            return UnitTarget{number(value, "index"),
                              number(value, "form"),
                              std::stoi(field(value, "tile_x")),
                              std::stoi(field(value, "tile_y")),
                              std::stof(field(value, "x")),
                              std::stof(field(value, "y"))};
    }
    return {};
}
std::optional<UnitTarget> friendly(const std::string &document) {
    return unit_for_faction(document, 0);
}
struct TileTarget {
    int tile_x{}, tile_y{};
    float x{}, y{};
};
float tile_width(const std::string &document) {
    const auto start = document.find("\"tile_targets\":[");
    require(start != std::string::npos, "No projected ground tiles");
    const auto tiles = document.substr(start, document.find(']', start) - start);
    static const std::regex object(R"re(\{[^{}]+\})re");
    std::optional<TileTarget> first;
    for (std::sregex_iterator it(tiles.begin(), tiles.end(), object), end; it != end; ++it) {
        const auto value = it->str();
        TileTarget tile{std::stoi(field(value, "tile_x")), std::stoi(field(value, "tile_y")),
                        std::stof(field(value, "x")), std::stof(field(value, "y"))};
        if (!first)
            first = tile;
        else if (first->tile_x != tile.tile_x)
            return std::abs((first->x - tile.x) / float(first->tile_x - tile.tile_x));
    }
    throw std::runtime_error("Ground tile width could not be observed");
}
std::optional<TileTarget> adjacent(const std::string &document, const UnitTarget &unit) {
    const auto start = document.find("\"tile_targets\":[");
    if (start == std::string::npos)
        return {};
    const auto tiles = document.substr(start, document.find(']', start) - start);
    static const std::regex object(R"re(\{[^{}]+\})re");
    for (std::sregex_iterator it(tiles.begin(), tiles.end(), object), end; it != end; ++it) {
        const auto value = it->str();
        TileTarget tile{std::stoi(field(value, "tile_x")), std::stoi(field(value, "tile_y")),
                        std::stof(field(value, "x")), std::stof(field(value, "y"))};
        if (std::abs(tile.tile_x - unit.tile_x) + std::abs(tile.tile_y - unit.tile_y) == 1)
            return tile;
    }
    return {};
}
std::optional<TileTarget> towards(const std::string &document, const UnitTarget &actor,
                                  const UnitTarget &enemy, int minimum_distance = 1) {
    const auto start = document.find("\"tile_targets\":[");
    if (start == std::string::npos)
        return {};
    const auto tiles = document.substr(start, document.find(']', start) - start);
    static const std::regex object(R"re(\{[^{}]+\})re");
    std::optional<TileTarget> best;
    auto distance = std::abs(actor.tile_x - enemy.tile_x) + std::abs(actor.tile_y - enemy.tile_y);
    for (std::sregex_iterator it(tiles.begin(), tiles.end(), object), end; it != end; ++it) {
        const auto value = it->str();
        TileTarget tile{std::stoi(field(value, "tile_x")), std::stoi(field(value, "tile_y")),
                        std::stof(field(value, "x")), std::stof(field(value, "y"))};
        const auto next =
            std::abs(tile.tile_x - enemy.tile_x) + std::abs(tile.tile_y - enemy.tile_y);
        if (next >= minimum_distance && next < distance) {
            distance = next;
            best = tile;
        }
    }
    return best;
}
std::optional<Target> target(const std::string &document, int id, int value = 0) {
    static const std::regex pattern(
        R"re(\{"id":([0-9]+),"value":(-?[0-9]+),"x":([0-9.]+),"y":([0-9.]+)\})re");
    const auto start = document.find("\"hit_targets\":[");
    if (start == std::string::npos)
        return {};
    const auto hits = document.substr(start);
    for (std::sregex_iterator it(hits.begin(), hits.end(), pattern), end; it != end; ++it) {
        Target t{std::stoi((*it)[1]), std::stoi((*it)[2]), std::stof((*it)[3]),
                 std::stof((*it)[4])};
        if (t.id == id && t.value == value)
            return t;
    }
    return {};
}

// Production uses Android Canvas; host validation uses a genuine installed
// monospace font only for that platform adapter. Every game asset stays original.
class FontWorker {
    int input_{-1}, output_{-1};
    pid_t child_{-1};
    static void readAll(int fd, void *data, std::size_t size) {
        auto *out = static_cast<unsigned char *>(data);
        while (size) {
            const auto n = read(fd, out, size);
            if (n < 0 && errno == EINTR)
                continue;
            require(n > 0, "System font worker ended unexpectedly");
            out += n;
            size -= std::size_t(n);
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
    g1::RgbaImage operator()(const std::string &text, float size) {
        const auto request =
            "{\"text\":" + json(text) + ",\"size\":" + std::to_string(size) + "}\n";
        std::size_t written = 0;
        while (written < request.size()) {
            const auto n = write(input_, request.data() + written, request.size() - written);
            if (n < 0 && errno == EINTR)
                continue;
            require(n > 0, "Font worker request failed");
            written += std::size_t(n);
        }
        std::uint32_t header[3];
        readAll(output_, header, sizeof(header));
        require(header[0] > 0 && header[1] > 0 &&
                    std::uint64_t(header[0]) * header[1] * 4 == header[2] &&
                    header[2] <= 64U * 1024U * 1024U,
                "Invalid font worker response");
        g1::RgbaImage image{header[0], header[1], g1::Bytes(header[2])};
        readAll(output_, image.pixels.data(), image.pixels.size());
        return image;
    }
};

class Offscreen {
    EGLDisplay display_{EGL_NO_DISPLAY};
    EGLConfig config_{};
    EGLSurface surface_{EGL_NO_SURFACE};
    EGLContext context_{EGL_NO_CONTEXT};

  public:
    Offscreen() {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        require(display_ != EGL_NO_DISPLAY, "No EGL display");
        require(eglInitialize(display_, nullptr, nullptr), "EGL initialization failed");
        require(eglBindAPI(EGL_OPENGL_ES_API), "EGL GLES API unavailable");
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
        EGLint count{};
        require(eglChooseConfig(display_, attributes, &config_, 1, &count) && count == 1,
                "No GLES3 pbuffer configuration");
        const EGLint buffer[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
        surface_ = eglCreatePbufferSurface(display_, config_, buffer);
        require(surface_ != EGL_NO_SURFACE, "Pbuffer creation failed");
        createContext();
    }
    ~Offscreen() {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT)
            eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE)
            eglDestroySurface(display_, surface_);
        if (display_ != EGL_NO_DISPLAY)
            eglTerminate(display_);
    }
    void createContext() {
        const EGLint attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, attributes);
        require(context_ != EGL_NO_CONTEXT, "GLES3 context creation failed");
        require(eglMakeCurrent(display_, surface_, surface_, context_), "EGL make current failed");
    }
    void replaceContext() {
        require(eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT),
                "EGL unbind failed");
        require(eglDestroyContext(display_, context_), "EGL context destruction failed");
        context_ = EGL_NO_CONTEXT;
        createContext();
    }
    void swap() {
        glFinish();
        require(glGetError() == GL_NO_ERROR, "GLES frame failed");
        require(eglSwapBuffers(display_, surface_), "EGL pbuffer swap failed");
    }
};
void screenshot(const std::string &path) {
    g1::RgbaImage result{width, height, g1::Bytes(std::size_t(width) * height * 4)};
    g1::Bytes pixels(result.pixels.size());
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    require(glGetError() == GL_NO_ERROR, "Screenshot readback failed");
    std::set<std::uint32_t> colors;
    for (int y = 0; y < height; ++y) {
        const auto begin = pixels.begin() + std::size_t(height - y - 1) * width * 4;
        std::copy_n(begin, width * 4, result.pixels.begin() + std::size_t(y) * width * 4);
        if (y % 8 == 0)
            for (int x = 0; x < width; x += 8)
                colors.insert((std::uint32_t(begin[x * 4]) << 16) |
                              (std::uint32_t(begin[x * 4 + 1]) << 8) | begin[x * 4 + 2]);
    }
    require(colors.size() > 100, "Game framebuffer is blank or lacks original assets");
    const auto png = g1::encode_png(result);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
    require(bool(out), "Screenshot write failed");
}

class Probe {
    Offscreen &screen_;
    std::ofstream events_;
    std::string output_, previousKey_;

  public:
    std::unique_ptr<g1::android::AndroidGame> game;
    std::string state;
    std::size_t frames{}, touches{};
    std::set<std::uint64_t> gates, dialogues;
    Probe(Offscreen &screen, const std::string &output)
        : screen_(screen), events_(output + "/game-events.jsonl"), output_(output) {}
    void record(const std::string &reason) {
        state = game->diagnostics();
        require(!boolean(state, "failed"), "Android host reported failure: " + state);
        require(field(state, "error").empty(), "Android host error: " + state);
        events_ << "{\"event\":" << json(reason) << ",\"frame\":" << frames
                << ",\"diagnostics\":" << state << "}\n";
        events_.flush();
        const auto key = field(state, "panel") + ":" + field(state, "mission_gate") + ":" +
                         field(state, "command_index") + ":" + field(state, "battle_token");
        if (key != previousKey_) {
            std::cout << reason << " frame=" << frames << " state=" << key << '\n' << std::flush;
            previousKey_ = key;
        }
        if (!field(state, "mission_gate").empty()) {
            gates.insert(number(state, "mission_gate"));
            if (number(state, "mission_gate") == 0)
                dialogues.insert(number(state, "command_index"));
        }
    }
    void frame(std::uint64_t nanos = 30000000ULL, bool presented = true) {
        game->draw(nanos);
        screen_.swap();
        game->presented(presented);
        ++frames;
        record(presented ? "presented" : "presentation_rejected");
        // Campaign handoff is the source engine's expected stop after a real
        // victory. Unsupported gameplay stops must still fail the probe.
        require(field(state, "mission_stop").empty() || number(state, "mission_stop") == 0 ||
                    number(state, "mission_stop") ==
                        static_cast<std::uint64_t>(g1::MissionStop::campaign_handoff_required),
                "Original engine reached an unsupported stop: " + state);
    }
    bool has(int id, int value = 0) const {
        return target(state, id, value).has_value();
    }
    void click(int id, int value = 0) {
        const auto hit = target(state, id, value);
        require(bool(hit), "No enabled on-screen target " + std::to_string(id) + ":" +
                               std::to_string(value) + " in " + state);
        game->touch(0, hit->x, hit->y, 0, 0, 1);
        ++touches;
        record("touch_" + std::to_string(id));
        frame(0);
    }
    void capture(const std::string &name) {
        screenshot(output_ + "/" + name + ".png");
    }
    void touch_at(float x, float y) {
        game->touch(0, x, y, 0, 0, 1);
        ++touches;
        record("public_touch");
        frame(0);
    }
};

struct CampaignTransitionEvidence {
    std::string results, mission2, restored_mission2;
    std::uint64_t capture_actions{}, upgrade_actions{}, battle_completions{};
    unsigned decisions{}, end_turns{};
};

CampaignTransitionEvidence complete_first_mission(Probe &probe) {
    using namespace g1::android;
    // These are observed source menu action IDs, exercised only through the
    // enabled target returned by the rendered Android UI.
    constexpr int dismiss_message = 125, results_continue = 126;
    CampaignTransitionEvidence evidence;
    const auto captures_before = number(probe.state, "capture_actions");
    const auto upgrades_before = number(probe.state, "upgrade_actions");
    const auto battles_before = number(probe.state, "battle_completed");

    const auto settle = [&] {
        for (unsigned i = 0; i < 1800; ++i) {
            const auto panel = number(probe.state, "panel");
            // The first draw creates a receipt; the next update observes it
            // and enables Continue. Do not mistake that required gate for a
            // missing control immediately after the first results frame.
            if (panel == 3 && probe.has(results_continue))
                return;
            if (panel == 14 && probe.has(dismiss_message)) {
                probe.click(dismiss_message);
                continue;
            }
            if (panel == 0 && number(probe.state, "mission_gate") == 0 &&
                i % 3 == 0) {
                probe.touch_at(width * .5F, height * .6F);
            }
            if (number(probe.state, "panel") == 0 &&
                number(probe.state, "interaction_mode") == 0 &&
                number(probe.state, "turn") == 0 &&
                number(probe.state, "battle_token") == 0 &&
                !boolean(probe.state, "moving") &&
                !boolean(probe.state, "world_pending") && probe.has(ui_end_turn))
                return;
            probe.frame();
        }
        throw std::runtime_error("Campaign pilot did not reach player input/results: " +
                                 probe.state);
    };

    settle();
    for (; evidence.decisions < 40 && number(probe.state, "panel") != 3;
         ++evidence.decisions) {
        require(number(probe.state, "mission_index") == 0,
                "Mission changed before the rendered results received Continue");
        auto actor = friendly(probe.state);
        require(bool(actor), "Mission 1 lost its visible friendly unit before completion");
        probe.touch_at(actor->x, actor->y);
        require(field(probe.state, "selected") == std::to_string(actor->index),
                "Campaign pilot could not select the rendered friendly unit");

        // Fight and capture in the source robot form. No stats, action flags,
        // checkpoint contents, unlocks or shared random state are injected.
        if (actor->form != 0 && probe.has(ui_transform)) {
            probe.click(ui_transform);
            settle();
            actor = friendly(probe.state);
            require(actor && actor->form == 0, "Robot transformation did not complete");
            continue;
        }
        if (probe.has(ui_capture)) {
            const auto before = number(probe.state, "capture_actions");
            probe.click(ui_capture);
            require(number(probe.state, "capture_actions") == before + 1,
                    "Enabled source Capture did not capture the mission objective");
            probe.capture("game-mission1-captured-a36");
            settle();
            continue;
        }
        const auto enemy = unit_for_faction(probe.state, 1);
        if (enemy && probe.has(ui_attack)) {
            const auto before = number(probe.state, "attack_actions");
            probe.click(ui_attack);
            probe.touch_at(enemy->x, enemy->y);
            require(number(probe.state, "attack_actions") == before + 1 &&
                        number(probe.state, "battle_token") > 0,
                    "Campaign pilot attack did not start its source battle");
            settle();
            continue;
        }

        if (probe.has(ui_move)) {
            probe.click(ui_move);
            // Mission 1's source captured-site goal is (5,7), corroborated by
            // the preserved iOS story trace. The coordinate merely chooses a
            // reachable on-screen tile; movement/capture use public touches.
            const UnitTarget site{0, 0, 5, 7, 0, 0};
            const auto destination = towards(probe.state, *actor, enemy ? *enemy : site,
                                               enemy ? 1 : 0);
            if (destination) {
                const auto before = number(probe.state, "move_actions");
                probe.touch_at(destination->x, destination->y);
                probe.touch_at(destination->x, destination->y);
                require(number(probe.state, "move_actions") == before + 1 &&
                            boolean(probe.state, "moving"),
                        "Campaign pilot movement target did not accept two public touches");
                settle();
                const auto arrived = friendly(probe.state);
                require(arrived && arrived->tile_x == destination->tile_x &&
                            arrived->tile_y == destination->tile_y,
                        "Campaign pilot movement arrived on a different source tile");
                continue;
            }
        }
        probe.click(ui_end_turn);
        ++evidence.end_turns;
        settle();
    }
    require(number(probe.state, "panel") == 3 &&
                number(probe.state, "mission_index") == 0 && probe.has(results_continue),
            "Mission 1 did not produce its real enabled results panel: " + probe.state);
    evidence.capture_actions = number(probe.state, "capture_actions") - captures_before;
    evidence.upgrade_actions = number(probe.state, "upgrade_actions") - upgrades_before;
    evidence.battle_completions = number(probe.state, "battle_completed") - battles_before;
    require(evidence.capture_actions > 0 && evidence.battle_completions > 0,
            "Mission completion bypassed capture or rendered combat");
    probe.capture("game-mission1-results-a36");
    // Waiting at Results must not silently install the next mission.
    for (unsigned i = 0; i < 4; ++i)
        probe.frame();
    require(number(probe.state, "panel") == 3 && number(probe.state, "mission_index") == 0,
            "Campaign advanced without a real Results Continue touch");
    evidence.results = probe.state;
    probe.click(results_continue);
    for (unsigned i = 0; i < 120 && number(probe.state, "mission_index") == 0; ++i)
        probe.frame();
    require(number(probe.state, "mission_index") == 1 &&
                number(probe.state, "session_installs") == 2,
            "Results Continue did not install the original Mission 2 session");
    settle();
    require(number(probe.state, "mission_index") == 1 && number(probe.state, "panel") == 0 &&
                probe.has(ui_end_turn),
            "Mission 2 did not reach rendered player control after campaign handoff");
    probe.capture("game-mission2-player-control-a36");
    evidence.mission2 = probe.state;

    // Check that the real CampaignController write unlocked Mission 2 and no
    // further mission. Navigate out and back through the actual menu controls.
    require(probe.game->back(), "Mission 2 Back did not open the pause menu");
    probe.frame(0);
    probe.click(134); // Save and main menu.
    probe.click(101); // Play.
    probe.click(102); // Story.
    require(probe.has(105, 0) && probe.has(105, 1) && !probe.has(105, 2),
            "Mission 1 results did not persist exactly the next mission unlock");
    probe.capture("game-mission2-unlocked-a36");
    require(probe.game->back(), "Mission picker Back was not handled");
    probe.frame(0);
    require(probe.game->back(), "Mode picker Back was not handled");
    probe.frame(0);
    probe.click(112); // Continue the checkpoint created in Mission 2.
    settle();
    require(number(probe.state, "mission_index") == 1 &&
                number(probe.state, "checkpoint_loads") == 2 && probe.has(ui_end_turn),
            "Mission 2 checkpoint did not restore through the real Continue control");
    evidence.restored_mission2 = probe.state;
    return evidence;
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 3, "usage: game_probe GAME_RESOURCE_ROOT OUTPUT_DIRECTORY");
        const std::string resources = argv[1], output = argv[2];
        const bool ui_name_smoke = std::getenv("G1_UI_NAME_SMOKE") != nullptr;
        std::filesystem::create_directories(output);
        std::ofstream(output + "/game-probe-report.json") << "{\"status\":\"running\"}\n";
        // Unique, initially absent save directory: never edits a user's profile.
        auto save_template = output + "/game-probe-saves-XXXXXX";
        std::vector<char> save_pattern(save_template.begin(), save_template.end());
        save_pattern.push_back('\0');
        const auto directory = mkdtemp(save_pattern.data());
        require(directory != nullptr, "Fresh probe save directory creation failed");
        const std::string saves = directory;
        FontWorker font;
        Offscreen screen;
        Probe probe(screen, output);
        probe.game = std::make_unique<g1::android::AndroidGame>(resources, saves, false);
        probe.game->setTextRasterizer(
            [&](const std::string &text, float size) { return font(text, size); });
        probe.game->resize(width, height, 72, 0, 0, 0);
        probe.game->attachGraphics();
        probe.game->resume();
        probe.frame(0);
        require(number(probe.state, "panel") == 1 && !boolean(probe.state, "loaded"),
                "Fresh profile did not show the source startup menu");
        require(!boolean(probe.state, "modern"), "Fresh source profile must default to Classic");
        probe.capture("game-menu-a36");
        probe.click(101);
        require(number(probe.state, "panel") == 6, "Play did not open mode selection");
        require(!probe.has(103) && !probe.has(104),
                "Locked Showdown/Arena accepted a fresh-profile hit");
        probe.click(102);
        require(number(probe.state, "panel") == 7, "Story did not open the mission list");
        require(probe.has(105, 0) && !probe.has(105, 1),
                "Fresh profile mission unlock limit changed");
        probe.click(105, 0);
        require(boolean(probe.state, "loaded") && number(probe.state, "session_installs") == 1,
                "Mission 1 was not installed through its real menu");
        require(number(probe.state, "mission_index") == 0, "Wrong source mission loaded");
        const auto presented = number(probe.state, "presented_frames");
        probe.frame(0, false);
        require(number(probe.state, "presented_frames") == presented,
                "Rejected buffer presentation acknowledged a frame");
        bool dialogue_capture = false;
        unsigned ready = 0;
        for (unsigned i = 0; i < 1800 && ready < 3; ++i) {
            bool dialogue_tapped = false;
            if (number(probe.state, "mission_gate") == 0 && i % 3 == 0) {
                probe.game->touch(0, width * .5F, height * .6F, 0, 0, 1);
                ++probe.touches;
                dialogue_tapped = true;
            }
            probe.frame();
            if (!dialogue_capture && dialogue_tapped && number(probe.state, "mission_gate") == 0) {
                probe.capture("game-mission1-dialogue-a36");
                dialogue_capture = true;
            }
            if (number(probe.state, "panel") == 0 && number(probe.state, "interaction_mode") == 0 &&
                number(probe.state, "mission_gate") != 0 &&
                !boolean(probe.state, "world_pending") && probe.has(g1::android::ui_end_turn))
                ++ready;
            else
                ready = 0;
        }
        require(ready == 3,
                "Mission 1 failed to reach source player control through rendered dialogue");
        require(dialogue_capture && !probe.dialogues.empty(),
                "The real mission dialogue was not exercised");
        require(number(probe.state, "simulation_ticks") > 0 &&
                    number(probe.state, "command_index") > 0,
                "Source mission did not advance");
        probe.capture("game-mission1-classic-a36");
        const auto command = number(probe.state, "command_index");
        probe.game->draw(0);
        const auto before_pause = number(probe.state, "presented_frames");
        probe.game->pause();
        probe.game->presented(true);
        probe.record("pause_invalidated_pending_frame");
        require(number(probe.state, "presented_frames") == before_pause,
                "Paused frame received a stale completion receipt");
        const auto ticks = number(probe.state, "simulation_ticks");
        probe.game->draw(1000000000ULL);
        probe.record("background_draw");
        require(number(probe.state, "simulation_ticks") == ticks,
                "Background draw advanced gameplay");
        probe.game->resume();
        probe.frame(1000000000ULL);
        require(number(probe.state, "simulation_ticks") == ticks,
                "Resume caught up the paused interval");
        // Ninety 120Hz frame intervals exceed 750ms by only60ns. Carrying
        // fractional milliseconds yields25 original30ms steps; truncating
        // each display interval independently would incorrectly yield24.
        const auto rate_ticks = number(probe.state, "simulation_ticks");
        for (unsigned i = 0; i < 90; ++i)
            probe.frame(8333334ULL);
        require(number(probe.state, "simulation_ticks") == rate_ticks + 25,
                "120Hz display intervals changed the original30ms simulation cadence");
        require(number(probe.state, "checkpoint_writes") > 0,
                "Source gameplay never created its atomic checkpoint");
        probe.game->back();
        probe.frame(0);
        require(number(probe.state, "panel") == 2, "Android Back did not open Pause");
        probe.click(114);
        require(number(probe.state, "panel") == 8, "Pause settings did not open");
        probe.click(116);
        require(boolean(probe.state, "modern"),
                "Graphics toggle did not activate source Modern mode");
        probe.game->back();
        probe.frame(0);
        probe.game->back();
        probe.frame(0);
        require(number(probe.state, "panel") == 0 &&
                    number(probe.state, "command_index") == command,
                "Settings changed the mission command");
        require(number(probe.state, "tactical_models") > 0 &&
                    number(probe.state, "tactical_fallbacks") == 0,
                "Modern tactical model path used an unexpected sprite fallback");
        probe.capture("game-mission1-modern-a36");
        const auto allocations_before = number(probe.state, "render_allocation_fallbacks");
        const auto presentations_before_recreation = number(probe.state, "presented_frames");
        probe.game->detachGraphics(true);
        screen.replaceContext();
        reject_next_scene_framebuffer = true;
        probe.game->attachGraphics();
        probe.game->resize(width, height, 72, 0, 0, 0);
        probe.game->resume();
        probe.frame(0);
        require(!reject_next_scene_framebuffer &&
                    number(probe.state, "render_allocation_fallbacks") == allocations_before + 1 &&
                    number(probe.state, "render_width") == width &&
                    number(probe.state, "render_height") == height &&
                    number(probe.state, "presented_frames") == presentations_before_recreation + 1,
                "Rejected supersampling allocation did not present a healthy native-size frame");
        probe.capture("game-supersampling-allocation-fallback-a36");
        probe.frame(0);
        require(number(probe.state, "render_width") > width &&
                    number(probe.state, "render_height") > height &&
                    number(probe.state, "render_allocation_fallbacks") == allocations_before + 1,
                "Modern supersampling did not recover after the one-shot allocation failure");
        require(number(probe.state, "command_index") == command &&
                    number(probe.state, "tactical_models") > 0 &&
                    number(probe.state, "tactical_fallbacks") == 0,
                "Real GL context replacement lost gameplay or model resources");
        probe.capture("game-context-recreated-a36");
        const auto selectable = friendly(probe.state);
        require(bool(selectable), "No visible friendly unit target in the original mission");
        probe.game->touch(0, selectable->x, selectable->y, 0, 0, 1);
        ++probe.touches;
        probe.frame(0);
        require(number(probe.state, "selected") == selectable->index,
                "Visible friendly-unit touch failed to select the source unit");
        probe.click(g1::android::ui_tech_specs);
        require(number(probe.state, "panel") == 18,
                "Selected unit did not open its source Tech Specs");
        probe.capture("game-tech-specs-robot-a36");
        probe.click(g1::android::ui_specs_vehicle);
        probe.capture("game-tech-specs-vehicle-a36");
        probe.click(g1::android::ui_specs_back);
        require(number(probe.state, "panel") == 0, "Tech Specs Back did not restore tactical play");
        const auto robot_hud =
            accessible_label(probe.game->accessibilitySnapshot(true).value_or(""), 4);
        const auto name_end = robot_hud.find(". Health ");
        require(name_end != std::string::npos && name_end > 0,
                "Selected robot HUD did not expose its name and health");
        const auto selected_name = robot_hud.substr(0, name_end);
        const auto robot_portrait_target = target(probe.state, g1::android::ui_tech_specs);
        require(bool(robot_portrait_target), "Selected robot portrait has no Tech Specs target");
        const auto robot_portrait = portrait_pixels(*robot_portrait_target);
        const auto transforms = number(probe.state, "transform_actions");
        probe.click(g1::android::ui_transform);
        require(number(probe.state, "transform_actions") == transforms + 1,
                "Transform HUD touch did not execute the original action");
        for (unsigned i = 0; i < 60; ++i)
            probe.frame();
        const auto transformed = friendly(probe.state);
        require(transformed && transformed->form != selectable->form,
                "Source unit form did not finish changing");
        const auto vehicle_hud =
            accessible_label(probe.game->accessibilitySnapshot(true).value_or(""), 4);
        require(vehicle_hud.rfind(selected_name + ". Health ", 0) == 0,
                "Transformation replaced the Transformer name with a quote or form-dependent text");
        const auto vehicle_portrait_target = target(probe.state, g1::android::ui_tech_specs);
        require(bool(vehicle_portrait_target) &&
                    portrait_pixels(*vehicle_portrait_target) == robot_portrait,
                "Transformation changed or removed the selected Transformer portrait");
        probe.capture("game-transform-completed-a36");
        if (ui_name_smoke) {
            std::ofstream report(output + "/game-probe-report.json");
            report << "{\n  \"status\":\"passed\",\n"
                   << "  \"kind\":\"Focused public-touch name/portrait regression on offscreen "
                      "AndroidGame GLES; not an Android device test or full campaign pass\",\n"
                   << "  \"frames\":" << probe.frames << ",\n"
                   << "  \"public_touch_events\":" << probe.touches << ",\n"
                   << "  \"selected_unit_name\":" << json(selected_name) << ",\n"
                   << "  \"unit_name_preserved_across_transform\":true,\n"
                   << "  \"selected_portrait_preserved_across_transform\":true,\n"
                   << "  \"robot_hud_label\":" << json(robot_hud) << ",\n"
                   << "  \"vehicle_hud_label\":" << json(vehicle_hud) << "\n}\n";
            require(bool(report), "Focused UI regression report write failed");
            probe.game->pause();
            probe.game->detachGraphics();
            probe.game.reset();
            std::cout << "Name/portrait regression passed: " << selected_name << ", "
                      << probe.frames << " rendered frames.\n";
            return 0;
        }
        const auto moves = number(probe.state, "move_actions");
        probe.click(g1::android::ui_move);
        const auto destination = adjacent(probe.state, *transformed);
        require(bool(destination), "Original movement range has no visible adjacent destination");
        probe.capture("game-movement-range-a36");
        probe.game->touch(0, destination->x, destination->y, 0, 0, 1);
        ++probe.touches;
        probe.frame(0);
        require(number(probe.state, "move_actions") == moves && !boolean(probe.state, "moving"),
                "First movement touch bypassed source confirmation");
        probe.game->touch(0, destination->x, destination->y, 0, 0, 1);
        ++probe.touches;
        probe.frame(0);
        require(number(probe.state, "move_actions") == moves + 1 && boolean(probe.state, "moving"),
                "Second movement touch did not start the original movement path");
        for (unsigned i = 0; i < 120 && boolean(probe.state, "moving"); ++i)
            probe.frame();
        require(!boolean(probe.state, "moving"), "Original adjacent movement did not complete");
        auto moved = friendly(probe.state);
        require(moved && moved->tile_x == destination->tile_x &&
                    moved->tile_y == destination->tile_y,
                "Source movement finished on the wrong tile");
        const auto undos = number(probe.state, "undo_actions");
        probe.click(g1::android::ui_undo);
        require(number(probe.state, "undo_actions") == undos + 1,
                "Undo HUD did not execute its source action");
        const auto undone = friendly(probe.state);
        require(undone && undone->tile_x == transformed->tile_x &&
                    undone->tile_y == transformed->tile_y,
                "Original undo did not restore the starting tile");
        const auto battles = number(probe.state, "battle_completed");
        bool saw_battle = false, battle_capture = false, rejected_final = false;
        unsigned battle_frames = 0;
        const auto battle_frame = [&] {
            // Inspect only the public diagnostics after a real GPU draw. The
            // final complete battle frame is deliberately denied one receipt.
            probe.game->draw(30000000ULL);
            screen.swap();
            probe.record("battle_draw_before_present");
            bool accept = true;
            if (number(probe.state, "battle_token") > 0) {
                saw_battle = true;
                ++battle_frames;
                if (!battle_capture && battle_frames >= 12) {
                    probe.capture("game-source-battle-a36");
                    battle_capture = true;
                }
                if (!rejected_final && field(probe.state, "battle_visual_complete") == "true") {
                    accept = false;
                    rejected_final = true;
                }
            }
            const auto token = number(probe.state, "battle_token");
            probe.game->presented(accept);
            ++probe.frames;
            probe.record(accept ? "battle_presented" : "battle_final_rejected");
            if (!accept)
                require(number(probe.state, "battle_token") == token &&
                            number(probe.state, "battle_completed") == battles,
                        "Failed final battle presentation mutated the original combat session");
        };
        const auto attack_if_available = [&] {
            if (!probe.has(g1::android::ui_attack))
                return;
            probe.click(g1::android::ui_attack);
            const auto enemy = unit_for_faction(probe.state, 1);
            require(bool(enemy), "Enabled Attack has no visible original enemy");
            probe.game->touch(0, enemy->x, enemy->y, 0, 0, 1);
            ++probe.touches;
            probe.frame(0);
            require(number(probe.state, "battle_token") > 0,
                    "Original enemy tap did not start an eligible attack");
        };
        unsigned human_turns = 0;
        for (unsigned turn = 0; turn < 12 && number(probe.state, "battle_completed") == battles;
             ++turn) {
            const auto actor = friendly(probe.state);
            require(bool(actor), "No visible friendly unit during legitimate battle approach");
            probe.game->touch(0, actor->x, actor->y, 0, 0, 1);
            ++probe.touches;
            probe.frame(0);
            attack_if_available();
            if (number(probe.state, "battle_token") == 0 && probe.has(g1::android::ui_move)) {
                const auto enemy = unit_for_faction(probe.state, 1);
                require(bool(enemy), "Original Mission1 enemy is not visible");
                probe.click(g1::android::ui_move);
                const auto approach = towards(probe.state, *actor, *enemy);
                if (approach) {
                    const auto span = tile_width(probe.state);
                    // A tall enemy model may cover the ground tile's center.
                    // Try visible interior side points using the same normal
                    // touch path; no selection or movement state is injected.
                    for (const float side : {0.F, .36F, -.36F}) {
                        probe.game->touch(0, approach->x + span * side, approach->y, 0, 0, 1);
                        ++probe.touches;
                        probe.frame(0);
                        if (!boolean(probe.state, "moving")) {
                            probe.game->touch(0, approach->x + span * side, approach->y, 0, 0, 1);
                            ++probe.touches;
                            probe.frame(0);
                        }
                        if (boolean(probe.state, "moving"))
                            break;
                    }
                    require(boolean(probe.state, "moving"),
                            "Original approach movement was not accepted");
                    for (unsigned i = 0; i < 360 && boolean(probe.state, "moving"); ++i)
                        probe.frame();
                    require(!boolean(probe.state, "moving"),
                            "Original approach movement did not finish");
                    const auto arrived = friendly(probe.state);
                    require(arrived && arrived->tile_x == approach->tile_x &&
                                arrived->tile_y == approach->tile_y,
                            "Approach touch resolved to a different original ground tile");
                    probe.capture("game-enemy-approach-a36");
                    attack_if_available();
                }
            }
            if (number(probe.state, "battle_token") == 0) {
                probe.click(g1::android::ui_end_turn);
                ++human_turns;
            }
            for (unsigned i = 0; i < 900 && number(probe.state, "battle_completed") == battles;
                 ++i) {
                battle_frame();
                if (number(probe.state, "battle_token") == 0 && number(probe.state, "turn") == 0 &&
                    probe.has(g1::android::ui_end_turn))
                    break;
            }
        }
        require(saw_battle && battle_capture, "Original AI never presented a real combat scene");
        require(rejected_final,
                "Real final battle frame was not subjected to a failed-presentation receipt");
        require(number(probe.state, "battle_completed") == battles + 1 &&
                    number(probe.state, "battle_token") == 0,
                "Matching final battle frame did not return to the source tactical session");
        for (unsigned i = 0;
             i < 360 && (!probe.has(g1::android::ui_end_turn) || number(probe.state, "turn") != 0);
             ++i)
            probe.frame();
        require(probe.has(g1::android::ui_end_turn) && number(probe.state, "turn") == 0,
                "Original AI turn failed to hand control back after combat");
        probe.frame();
        probe.capture("game-battle-return-tactical-a36");
        const auto checkpoint_unit = friendly(probe.state);
        require(bool(checkpoint_unit), "No surviving source unit to checkpoint");
        const auto before_restart = probe.state;
        probe.game->pause();
        probe.game->detachGraphics();
        probe.game.reset();
        probe.game = std::make_unique<g1::android::AndroidGame>(resources, saves, false);
        probe.game->setTextRasterizer(
            [&](const std::string &text, float size) { return font(text, size); });
        probe.game->resize(width, height, 72, 0, 0, 0);
        probe.game->attachGraphics();
        probe.game->resume();
        probe.frame(0);
        require(probe.has(112), "Checkpoint did not expose Continue after process recreation");
        probe.click(112);
        probe.frame(0);
        require(number(probe.state, "checkpoint_loads") == 1 &&
                    number(probe.state, "mission_index") == 0 &&
                    number(probe.state, "command_index") == command,
                "Process recreation did not restore the source checkpoint");
        require(boolean(probe.state, "modern"), "Graphics setting was not persisted");
        const auto restored = friendly(probe.state);
        require(restored && restored->form == checkpoint_unit->form &&
                    restored->tile_x == checkpoint_unit->tile_x &&
                    restored->tile_y == checkpoint_unit->tile_y,
                "Post-combat source unit did not survive checkpoint reload");
        probe.capture("game-checkpoint-restored-a36");
        const auto restored_mission1 = probe.state;
        const auto campaign = complete_first_mission(probe);
        std::ofstream report(output + "/game-probe-report.json");
        report << "{\n  \"status\": \"passed\",\n  \"kind\": \"Real public AndroidGame integration "
                  "on offscreen GLES; not an Android device test\",\n"
               << "  \"renderer\": "
               << json(reinterpret_cast<const char *>(glGetString(GL_RENDERER))) << ",\n"
               << "  \"width\": " << width << ", \"height\": " << height
               << ", \"left_inset\": 72,\n"
               << "  \"frames\": " << probe.frames << ", \"public_touch_events\": " << probe.touches
               << ", \"source_dialogue_commands\": " << probe.dialogues.size() << ",\n"
               << "  \"source_assets\": \"017R1 exact SHA-256 verified by AndroidGame\",\n"
               << "  \"fresh_profile_unlocks\": true, \"mission1_player_control\": true,\n"
               << "  \"classic_and_modern\": true, \"modern_sprite_fallbacks\": 0,\n"
               << "  \"unit_selection\": true, \"tech_specs_robot_and_vehicle_models\": true, "
                  "\"source_transform_action\": true,\n"
               << "  \"unit_name_preserved_across_transform\": true, "
                  "\"selected_portrait_preserved_across_transform\": true,\n"
               << "  \"two_tap_movement_confirmation\": true, \"source_movement_completed\": true, "
                  "\"source_movement_undo\": true,\n"
               << "  \"display120hz_preserves30ms_simulation\": true, \"original_battle\": true, "
                  "\"ordinary_end_turn_actions\": "
               << human_turns
               << ", \"failed_final_battle_receipt_rejected\": true, \"battle_return_tactical\": "
                  "true,\n"
               << "  \"failed_present_rejected\": true, \"pause_receipt_invalidated\": true, "
                  "\"resume_clock_reset\": true,\n"
               << "  \"actual_gl_context_replaced\": true, \"process_checkpoint_restored\": true,\n"
               << "  \"supersampling_allocation_failure_native_fallback\": true, "
                  "\"supersampling_recovers_after_allocation_failure\": true,\n"
               << "  \"mission1_completed_via_public_touch\": true, "
                  "\"mission1_capture_actions\": " << campaign.capture_actions << ",\n"
               << "  \"campaign_pilot_decisions\": " << campaign.decisions << ", "
                  "\"campaign_pilot_end_turns\": " << campaign.end_turns << ",\n"
               << "  \"campaign_pilot_battle_completions\": " << campaign.battle_completions
               << ", \"campaign_pilot_upgrade_actions\": " << campaign.upgrade_actions << ",\n"
               << "  \"results_wait_for_actual_continue\": true, "
                  "\"mission2_installed_by_campaign_controller\": true,\n"
               << "  \"mission2_player_control\": true, \"mission2_unlock_persisted\": true, "
                  "\"mission3_remains_locked\": true, \"mission2_checkpoint_restored\": true,\n"
               << "  \"limitations\": \"Host font adapter is DejaVuSansMono; AAudio/MediaPlayer, "
                  "SurfaceView/EGL recovery, physical Samsung driver, touch latency, thermal "
                  "performance, and a complete campaign require Android device validation\",\n"
               << "  \"pre_restart_diagnostics\": " << before_restart << ",\n"
               << "  \"mission1_checkpoint_restored_diagnostics\": " << restored_mission1 << ",\n"
               << "  \"mission1_results_diagnostics\": " << campaign.results << ",\n"
               << "  \"mission2_player_control_diagnostics\": " << campaign.mission2 << ",\n"
               << "  \"final_diagnostics\": " << probe.state << "\n}\n";
        require(bool(report), "Game report write failed");
        probe.game->pause();
        probe.game->detachGraphics();
        probe.game.reset();
        std::cout
            << "Game integration passed: " << probe.frames << " rendered frames, "
            << probe.dialogues.size()
            << " original dialogue commands, real context recreation, Mission 1 capture/results, "
               "Mission 2 handoff, persisted unlock and checkpoint reload.\n";
        return 0;
    } catch (const std::exception &e) {
        if (argc == 3)
            std::ofstream(std::string(argv[2]) + "/game-probe-report.json")
                << "{\"status\":\"failed\",\"error\":" << json(e.what()) << "}\n";
        std::cerr << "Game integration probe failed: " << e.what() << '\n';
        return 1;
    }
}
