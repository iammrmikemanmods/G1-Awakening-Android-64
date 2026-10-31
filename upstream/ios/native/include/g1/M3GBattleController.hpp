#pragma once

#include "g1/M3GBattleTimeline.hpp"

#include <array>
#include <vector>

namespace g1 {

struct M3GBattleControllerConfig {
    bool a_first{true},counter{},intro{};
    std::array<std::uint8_t,2> forms{},factions{{0,1}},adjustments{};
    std::array<bool,2> immune{},dead{};
    std::array<std::int32_t,2> damage{},final_health{};
    // Actual selected-form camera presence for each recovered camera ID.
    // ID8 bypasses model lookup and must remain false.
    std::array<bool,13> embedded_camera{};
    std::int32_t logical_width{480},logical_height{320};
};

struct M3GBattleControllerState {
    std::uint8_t phase{};
    bool complete{},camera_side_a{true},embedded_current{};
    std::int32_t elapsed_ms{},slide_fixed16{};
    std::array<std::int32_t,2> clocks{},display_health{};
};

enum class M3GBattleTarget : std::uint8_t {
    model_a,model_b,effect_0,effect_1,effect_2,effect_3,death_effect
};
enum class M3GBattleEventKind : std::uint8_t {
    animate,start_clip,tick_clip,select_camera,propagate_camera,
    place_effect,clear_text,set_integer_text,audio
};
struct M3GBattleEvent {
    M3GBattleEventKind kind{M3GBattleEventKind::animate};
    M3GBattleTarget target{M3GBattleTarget::model_a};
    // animate:time; start/tick:clip,clock; tick:delta/restart;
    // select:id; propagate:time/adjust; place:side_a/hit;
    // text:field/value; audio:operation/argument.
    std::int32_t time{},clip{},clock{},delta{},id{},adjust{},field{},value{},operation{},argument{};
    bool restart{},side_a{},hit{};
};
struct M3GBattleControllerStep {
    M3GBattleControllerState state;
    // start_clip/tick_clip record arithmetic already completed here. The
    // following animate event is the actual pose operation for the host.
    std::vector<M3GBattleEvent> events;
};

M3GBattleControllerStep advance_m3g_battle_controller(
    const M3GBattleControllerConfig&,const M3GBattleControllerState&,std::int32_t delta_ms);

struct M3GBattleDrawPlan {
    bool render_3d{};
    std::array<bool,7> root_enabled{};
    bool damage_text{};
    std::int32_t damage_x{},damage_y{},slide_fixed16{};
};
M3GBattleDrawPlan m3g_battle_draw_plan(const M3GBattleControllerConfig&,
    const M3GBattleControllerState&,bool refresh=true);

} // namespace g1
