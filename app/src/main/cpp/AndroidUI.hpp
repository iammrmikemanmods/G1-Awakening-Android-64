#pragma once

#include "GLESRenderer.hpp"
#include "g1/AdaptivePresentation.hpp"
#include "g1/Archive.hpp"
#include "g1/M3GBattlePresentation.hpp"
#include "g1/MissionExecution.hpp"
#include "g1/TechSpecs.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace g1::android {

// IDs below 100 are the fixed source HUD controls. A caller may use IDs >=100
// for menu choices, with value carrying a mission/type/topic index.
enum UiActionId : int {
    ui_none = 0,
    ui_menu = 1,
    ui_move,
    ui_capture,
    ui_transform,
    ui_attack,
    ui_heal,
    ui_upgrade,
    ui_end_turn,
    ui_build,
    ui_deploy,
    ui_undo,
    ui_tech_specs,
    ui_dialogue_next,
    ui_dialogue_skip,
    ui_specs_previous,
    ui_specs_next,
    ui_specs_robot,
    ui_specs_vehicle,
    ui_specs_back
};
struct UiAction {
    int id{}, value{};
    explicit operator bool() const noexcept {
        return id != 0;
    }
};
struct UiButton {
    std::string label;
    UiAction action;
    bool enabled{true};
};
struct UiPanel {
    std::string title, body;
    std::uint64_t token{};
    bool source_menu{true}, information{}, details{};
    std::int32_t background_offset{};
    std::vector<UiButton> buttons;
    // The source host uses a native, scrollable action sheet for production,
    // independent of the original artwork's nine menu slots. The final
    // button is the fixed Cancel action; all preceding choices may scroll.
    bool action_list{};
};
struct UiTactical {
    bool visible{true}, shows_unit{}, commands{}, tech_specs{}, vehicle_form{};
    std::string name, objective;
    std::int32_t health{}, attack_low{}, attack_high{}, defense{}, speed{}, funds{};
    bool move{}, capture{}, transform{}, attack{}, heal{}, upgrade{}, end_turn{}, build{}, deploy{},
        undo{};
    std::int32_t upgrade_cost{};
};
struct UiDialogue {
    std::uint64_t identity{};
    std::string heading, text;
    DialogueProgress progress{};
};
struct UiBattle {
    M3GBattleStatus status;
    std::string left_name, right_name;
    std::uint8_t left_faction{}, right_faction{};
    // Normalized projection of the actual damaged model, when available.
    std::optional<std::array<float, 2>> damage_anchor;
};
struct UiTechSpecs {
    TechSpecsData data{};
    std::string name, quote;
    std::vector<std::string> details;
    std::uint32_t portrait_resource{};
    bool previous{true}, next{true}, robot{true}, vehicle{true};
};
struct UiState {
    std::optional<UiPanel> panel;
    std::optional<UiDialogue> dialogue;
    std::optional<UiBattle> battle;
    std::optional<UiTechSpecs> tech_specs;
    UiTactical tactical;
    bool show_menu{true};
    std::string support_banner, notice, diagnostics;
};
struct UiReceipt {
    // Receipt says drawing commands were encoded, not that EGL presented them.
    // The host must only deliver these facts after a successful buffer swap.
    std::uint64_t panel_token{}, battle_token{}, battle_revision{}, dialogue_identity{};
    std::optional<DialogueLayout> dialogue_layout;
    bool support_banner_drawn{}, tactical_drawn{};
};
struct UiAccessibleNode {
    int id{};
    UiAction action; // Zero is read-only text; clicks still use normal hit testing.
    std::string label, hint, value;
    adaptive::Rect bounds; // Surface pixels, clipped to the actually drawn region.
    bool enabled{true}, scroll_forward{}, scroll_backward{};
};

class AndroidUI {
  public:
    explicit AndroidUI(GLESRenderer &);
    ~AndroidUI();
    AndroidUI(const AndroidUI &) = delete;
    AndroidUI &operator=(const AndroidUI &) = delete;
    void initialize(const Archive &, const std::string &resource_root);
    // The source iOS host used UIKit fallback for text not covered by its
    // bitmap fonts. Android supplies its platform monospace rasterizer here.
    void setSystemTextRasterizer(std::function<RgbaImage(const std::string &, float)>);
    void resize(int width, int height, adaptive::Insets insets = {});
    // Layout comes from the same wrap/clip routine used to draw dialogue.
    // fully_drawn remains false here; consume render()'s receipt after swap.
    DialogueLayout dialogueLayout(const UiDialogue &);
    UiReceipt render(const UiState &,
                     const std::function<void(adaptive::Rect)> &draw_specs_model = {});
    UiAction hit(float x, float y) const noexcept;
    // Snapshot for the last encoded UI; publish only after its successful swap.
    const std::vector<UiAccessibleNode> &accessibleNodes() const noexcept;
    bool scroll(float dy);
    adaptive::Rect specsModelViewport() const noexcept;
    const adaptive::Layout &layout() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace g1::android
