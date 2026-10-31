#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

// The UI coordinate contract is defined by the verified, untouched 480x320
// resource 1002. This is an aspect-preserving native layout, not a claim that
// the original ARM32 UI's dispatcher or hit-box tables have been recovered.
namespace g1::original_ui {

struct Rect {
    double x{}, y{}, width{}, height{};
    constexpr double right() const noexcept { return x + width; }
    constexpr double bottom() const noexcept { return y + height; }
};

constexpr double source_width = 480.0;
constexpr double source_height = 320.0;
constexpr Rect heading_slot{132.0, 84.0, 216.0, 22.0};
constexpr Rect menu_red_panel{3.0, 113.0, 472.0, 103.0};
constexpr Rect information_text{54.0, 123.0, 372.0, 63.0};
constexpr Rect choices_region{26.0, 116.0, 428.0, 95.0};
constexpr double choices_center_y = 164.0;
constexpr double information_action_center_y = 201.0;
constexpr double information_action_max_height = 29.0;
constexpr int menu_columns = 3;
constexpr int mission_page_size = 6;

struct Layout {
    Rect artwork;
    double scale{};

    constexpr Rect project(Rect source) const noexcept {
        return {artwork.x + source.x * scale, artwork.y + source.y * scale,
                source.width * scale, source.height * scale};
    }

    constexpr double source_offset_from_center(double source_y) const noexcept {
        return (source_y - source_height / 2.0) * scale;
    }
};

inline Layout fit(double width, double height) noexcept {
    if (!(std::isfinite(width) && std::isfinite(height)) || width <= 0 || height <= 0)
        return {};
    const double scale = std::min(width / source_width, height / source_height);
    return {{(width - source_width * scale) / 2.0,
             (height - source_height * scale) / 2.0,
             source_width * scale, source_height * scale}, scale};
}

constexpr bool inside(Rect outer, Rect inner) noexcept {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}

// The native UI never overlays more than three rows of mission selections on
// the source red panel. Navigation buttons are included in that 9-slot budget.
constexpr int menu_row_count(int option_count) noexcept {
    return option_count <= 0 ? 0 : (option_count + menu_columns - 1) / menu_columns;
}

constexpr int mission_page_count(int available) noexcept {
    return available <= 0 ? 0 : (available + mission_page_size - 1) / mission_page_size;
}

} // namespace g1::original_ui
