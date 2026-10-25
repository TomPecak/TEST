#pragma once

#include "geometry.hpp"

#include <string>

namespace aurorae
{
class IniFile;

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Center, Bottom };

/**
 * Contents of a theme's "<name>rc" file with the defaults of KWin's Aurorae
 * (aurorae/v2/decorationtheme.cpp). All lengths are logical pixels.
 */
struct ThemeConfig
{
    // [General]
    Color active_text_color   = {0, 0, 0, 1};
    Color inactive_text_color = {0, 0, 0, 1};
    HAlign title_alignment    = HAlign::Left;
    VAlign title_vertical_alignment = VAlign::Center;
    int animation_ms = 0;
    int decoration_position = 0; // 0 top, 1 left, 2 right, 3 bottom
    bool button_group_hover = false;

    // Legacy text effects. Current KWin ignores them; we honour them only when
    // the user opts in (see Settings::legacy_text_shadow).
    bool use_text_shadow = false;
    Color active_text_shadow_color   = {1, 1, 1, 1};
    Color inactive_text_shadow_color = {1, 1, 1, 1};
    int text_shadow_offset_x = 0;
    int text_shadow_offset_y = 0;

    // [Layout]
    double border_left   = 5;
    double border_right  = 5;
    double border_bottom = 5;
    double border_top    = 0;

    Margins title_edges = {5, 5, 5, 5};
    Margins title_edges_maximized = {0, 0, 0, 0};
    double title_border_left  = 5;
    double title_border_right = 5;
    double title_height = 20;

    double button_width = 20;
    double button_width_minimize = 20;
    double button_width_maximize_restore = 20;
    double button_width_close = 20;
    double button_width_all_desktops = 20;
    double button_width_keep_above   = 20;
    double button_width_keep_below   = 20;
    double button_width_shade = 20;
    double button_width_help  = 20;
    double button_width_menu  = 20;
    double button_width_app_menu = 20;
    double button_height  = 20;
    double button_spacing = 5;
    double button_margin_top = 0;
    double button_margin_top_maximized = 0;
    double explicit_button_spacer = 10;

    Margins padding = {0, 0, 0, 0};

    static ThemeConfig from_ini(const IniFile& ini);
};
} // namespace aurorae
