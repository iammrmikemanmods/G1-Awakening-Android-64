#include "g1/OriginalPresentation.hpp"

#include <cassert>
#include <cmath>
#include <utility>

int main() {
    using namespace g1::original_presentation;
    using g1::original_ui::fit;
    static_assert(in_source(dialogue) && in_source(tactical_hud));
    static_assert(in_source(top_left_action) && in_source(end_turn));
    static_assert(in_source(dialogue_portrait) && in_source(dialogue_heading));
    static_assert(in_source(dialogue_text));
    static_assert(dialogue.bottom()==tactical_hud.bottom());
    static_assert(dialogue.height==93 && tactical_hud.height==62);
    static_assert(disjoint(top_left_action,tactical_hud));
    static_assert(disjoint(end_turn,tactical_hud));
    for(const Rect rect:source_left_actions) {
        assert(in_source(rect));
        assert(disjoint(rect,tactical_hud));
    }
    for(const Rect rect:source_right_actions) {
        assert(in_source(rect));
        assert(disjoint(rect,tactical_hud));
    }
    for(const auto [w,h]:{std::pair{480.0,320.0},{896.0,414.0},
                            {812.0,375.0},{1024.0,768.0}}) {
        const auto result=fit(w,h);
        assert(result.scale>0);
        const auto dlg=result.project(dialogue);
        const auto hud=result.project(tactical_hud);
        assert(std::abs(dlg.width/dlg.height-480.0/93.0)<1e-12);
        assert(std::abs(hud.width/hud.height-480.0/62.0)<1e-12);
        assert(std::abs(dlg.bottom()-hud.bottom())<1e-9);
        assert(g1::original_ui::inside({0,0,w,h},dlg));
        assert(g1::original_ui::inside({0,0,w,h},hud));
        assert(result.project(top_left_action).right() < result.project(end_turn).x);
        assert(result.project(source_left_actions[1]).right() <=
               result.project(source_right_actions[1]).x);
    }
    // On-screen controls must never be taller than the source buttons.
    assert(source_right_actions[3].bottom()<=tactical_hud.y);
    assert(source_left_actions[3].bottom()<=tactical_hud.y);
}
