#pragma once
#include "g1/OriginalUI.hpp"
#include <algorithm>
#include <cmath>

// Gameplay UI measurements (source virtual 480x320), reconstructed by
// inspecting the user-provided full first-mission reference recording at
// frames 33/39/42/51/54. Geometry is presentation-only; commands unchanged.
namespace g1::original_presentation {
using g1::original_ui::Rect;
constexpr Rect dialogue{0,227,480,93};
constexpr Rect dialogue_portrait{81,17,78,69};
constexpr Rect dialogue_heading{174,17,157,14};
constexpr Rect dialogue_text{174,31,151,54};
constexpr Rect tactical_hud{0,258,480,62};
constexpr Rect top_left_action{0,0,60,49};
constexpr Rect end_turn{208,0,60,49};
constexpr Rect source_left_actions[4]={{0,50,52,49},{0,99,52,49},{0,148,52,49},{0,197,52,49}};
constexpr Rect source_right_actions[4]={{428,50,52,49},{428,99,52,49},{428,148,52,49},{428,197,52,49}};
constexpr Rect tactical_portrait{0,0,60,62};
constexpr Rect tactical_name{91,19,131,17};
constexpr Rect tactical_hp{72,40,54,17};
constexpr Rect tactical_attack{222,19,53,37};
constexpr Rect tactical_defense{275,19,33,37};
constexpr Rect tactical_speed{308,19,31,37};
constexpr Rect tactical_energy{432,12,48,50};

constexpr bool in_source(Rect r) noexcept {
    return g1::original_ui::inside({0,0,480,320},r);
}
constexpr bool disjoint(Rect a,Rect b) noexcept {
    return a.right()<=b.x || b.right()<=a.x || a.bottom()<=b.y || b.bottom()<=a.y;
}
// Fit the legacy virtual viewport to the XR safe drawing area. Modern Metal
// scene is allowed to extend behind the side pillars; controls do not stretch.
inline g1::original_ui::Layout fit_source(double width,double height) noexcept {
    return g1::original_ui::fit(width,height);
}
}
