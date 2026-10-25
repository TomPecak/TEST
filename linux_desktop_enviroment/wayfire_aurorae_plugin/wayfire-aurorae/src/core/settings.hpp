#pragma once

#include "buttons.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurorae
{
/** KDecoration3::BorderSize, plus Theme for the theme's unclamped values. */
enum class BorderSize
{
    None, NoSides, Tiny, Normal, Large, VeryLarge, Huge, VeryHuge, Oversized, Theme,
};

std::optional<BorderSize> parse_border_size(std::string_view name);

// KDE's "ButtonSize" setting: tiny, normal, large, ... -> 0.8, 1.0, 1.2, ...
std::optional<double> parse_button_size(std::string_view name);

/** User preferences that are not part of the theme. */
struct Settings
{
    BorderSize border_size = BorderSize::Normal;
    double button_size_factor = 1.0;
    std::vector<ButtonType> buttons_left  = parse_button_layout("MS");
    std::vector<ButtonType> buttons_right = parse_button_layout("HIAX");

    // Pango font description of the caption.
    std::string font = "Noto Sans 10";

    // Draw the theme's UseTextShadow text shadow. KWin stopped doing so, so it
    // is off by default to look like current Plasma.
    bool legacy_text_shadow = false;

    // Thickness of the invisible resize area outside the frame
    // (KDecoration's "resize only borders", largeSpacing in KWin).
    double extended_resize_border = 8;
};
} // namespace aurorae
