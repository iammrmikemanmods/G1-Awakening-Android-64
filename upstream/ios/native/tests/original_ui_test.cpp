#include "g1/OriginalUI.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <utility>

int main() {
    using namespace g1::original_ui;
    static_assert(inside(menu_red_panel, choices_region), "Buttons must remain inside original red panel");
    static_assert(inside(menu_red_panel, information_text), "Help text must remain in the original red panel");
    static_assert(heading_slot.bottom() < menu_red_panel.y, "Menu heading must never intersect controls");
    static_assert(mission_page_size + 3 == menu_columns * 3, "Six missions plus prev/next/back must fit three rows");
    static_assert(menu_row_count(9) == 3 && menu_row_count(10) == 4);
    static_assert(mission_page_count(20) == 4);

    for (const auto [width,height] : {std::pair{480.0,320.0}, {896.0,414.0}, {812.0,375.0}, {1024.0,768.0}}) {
        const auto l = fit(width,height);
        assert(l.scale > 0);
        assert(std::abs(l.artwork.width/l.artwork.height - 1.5) < 1e-12);
        assert(inside({0,0,width,height},l.artwork));
        assert(inside(l.project(menu_red_panel),l.project(information_text)));
        assert(l.project(heading_slot).bottom() < l.project(information_text).y);
        // Back is centred below the scrollable body, still inside the red frame.
        const auto back_top = information_action_center_y - information_action_max_height/2.0;
        const auto back_bottom = information_action_center_y + information_action_max_height/2.0;
        assert(back_top > information_text.bottom());
        assert(back_bottom <= menu_red_panel.bottom());
    }
    assert(fit(0,320).scale==0);
    assert(fit(480,std::numeric_limits<double>::quiet_NaN()).scale==0);
}
