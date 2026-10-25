#include "theme-config.hpp"
#include "ini-file.hpp"

#include <algorithm>
#include <cctype>

namespace aurorae
{
namespace
{
std::string lowercase(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [] (unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

ThemeConfig ThemeConfig::from_ini(const IniFile& ini)
{
    ThemeConfig c;

    const auto general = "General";
    c.active_text_color   = ini.get_color(general, "ActiveTextColor", c.active_text_color);
    c.inactive_text_color = ini.get_color(general, "InactiveTextColor", c.inactive_text_color);

    // Same fall-through as KWin: anything unrecognised means Right / Bottom.
    const auto halign = lowercase(ini.get_string(general, "TitleAlignment", "Left"));
    c.title_alignment = halign == "left" ? HAlign::Left :
        halign == "center" ? HAlign::Center : HAlign::Right;

    const auto valign = lowercase(ini.get_string(general, "TitleVerticalAlignment", "Center"));
    c.title_vertical_alignment = valign == "top" ? VAlign::Top :
        valign == "center" ? VAlign::Center : VAlign::Bottom;

    c.animation_ms = ini.get_int(general, "Animation", 0);
    c.decoration_position = ini.get_int(general, "DecorationPosition", 0);
    c.button_group_hover  = ini.get_bool(general, "ButtonGroupHover", false);

    c.use_text_shadow = ini.get_bool(general, "UseTextShadow", false);
    c.active_text_shadow_color =
        ini.get_color(general, "ActiveTextShadowColor", c.active_text_shadow_color);
    c.inactive_text_shadow_color =
        ini.get_color(general, "InactiveTextShadowColor", c.inactive_text_shadow_color);
    c.text_shadow_offset_x = ini.get_int(general, "TextShadowOffsetX", 0);
    c.text_shadow_offset_y = ini.get_int(general, "TextShadowOffsetY", 0);

    const auto layout = "Layout";
    auto len = [&] (const char *key, double fallback)
    {
        return double(ini.get_int(layout, key, int(fallback)));
    };

    c.border_left   = len("BorderLeft", 5);
    c.border_right  = len("BorderRight", 5);
    c.border_bottom = len("BorderBottom", 5);
    c.border_top    = len("BorderTop", 0);

    c.title_edges = {
        len("TitleEdgeLeft", 5), len("TitleEdgeTop", 5),
        len("TitleEdgeRight", 5), len("TitleEdgeBottom", 5),
    };
    c.title_edges_maximized = {
        len("TitleEdgeLeftMaximized", 0), len("TitleEdgeTopMaximized", 0),
        len("TitleEdgeRightMaximized", 0), len("TitleEdgeBottomMaximized", 0),
    };
    c.title_border_left  = len("TitleBorderLeft", 5);
    c.title_border_right = len("TitleBorderRight", 5);
    c.title_height = len("TitleHeight", 20);

    // Per-button widths fall back to ButtonWidth, the app menu to the menu.
    c.button_width = len("ButtonWidth", 20);
    c.button_width_minimize = len("ButtonWidthMinimize", c.button_width);
    c.button_width_maximize_restore = len("ButtonWidthMaximizeRestore", c.button_width);
    c.button_width_close = len("ButtonWidthClose", c.button_width);
    c.button_width_all_desktops = len("ButtonWidthAlldesktops", c.button_width);
    c.button_width_keep_above   = len("ButtonWidthKeepabove", c.button_width);
    c.button_width_keep_below   = len("ButtonWidthKeepbelow", c.button_width);
    c.button_width_shade = len("ButtonWidthShade", c.button_width);
    c.button_width_help  = len("ButtonWidthHelp", c.button_width);
    c.button_width_menu  = len("ButtonWidthMenu", c.button_width);
    c.button_width_app_menu = len("ButtonWidthAppMenu", c.button_width_menu);
    c.button_height  = len("ButtonHeight", 20);
    c.button_spacing = len("ButtonSpacing", 5);
    c.button_margin_top = len("ButtonMarginTop", 0);
    c.button_margin_top_maximized = len("ButtonMarginTopMaximized", 0);
    c.explicit_button_spacer = len("ExplicitButtonSpacer", 10);

    c.padding = {
        len("PaddingLeft", 0), len("PaddingTop", 0),
        len("PaddingRight", 0), len("PaddingBottom", 0),
    };

    return c;
}
} // namespace aurorae
