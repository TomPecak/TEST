#include "test.hpp"

#include "core/ini-file.hpp"
#include "core/kde-settings.hpp"
#include "core/settings.hpp"
#include "core/theme-config.hpp"

using namespace aurorae;

TEST_CASE(ini_parsing)
{
    const auto ini = IniFile::parse(
        "# comment\n"
        "[General]\n"
        "  Key = value with spaces  \n"
        "Dup=1\n"
        "Dup=2\n"
        "Name[pl]=Nazwa\n"
        "Escaped=a\\sb\n"
        "[Other]\n"
        "Key=other\n");

    CHECK(ini.get_string("General", "Key", "") == "value with spaces");
    CHECK(ini.get_int("General", "Dup", 0) == 2);
    CHECK(ini.get_string("General", "Name[pl]", "") == "Nazwa");
    CHECK(ini.get_string("General", "Escaped", "") == "a b");
    CHECK(ini.get_string("Other", "Key", "") == "other");
    CHECK(!ini.get("General", "Missing"));
    CHECK(!ini.get("Missing", "Key"));
}

TEST_CASE(ini_typed_values)
{
    CHECK(parse_int(" 42 ") == 42);
    CHECK(parse_int("+7") == 7);
    CHECK(!parse_int("20.5"));
    CHECK(!parse_int(""));

    CHECK(parse_bool("True") == true);
    CHECK(parse_bool("on") == true);
    CHECK(parse_bool("0") == false);
    CHECK(!parse_bool("maybe"));

    CHECK(parse_color("255,0,0") == (Color{1, 0, 0, 1}));
    CHECK(parse_color("0,0,255,0") == (Color{0, 0, 1, 0}));
    CHECK(parse_color("#00ff00") == (Color{0, 1, 0, 1}));
    CHECK(!parse_color("256,0,0"));
    CHECK(!parse_color("1,2"));
}

TEST_CASE(theme_config_defaults_and_fallbacks)
{
    const auto c = ThemeConfig::from_ini(*IniFile::load(test::data("TestTheme/TestThemerc")));
    CHECK(c.title_alignment == HAlign::Center);
    CHECK(c.title_vertical_alignment == VAlign::Center); // default
    CHECK(c.animation_ms == 200);                        // last duplicate wins
    CHECK(c.active_text_color == (Color{1, 1, 1, 1}));
    CHECK(c.inactive_text_color == (Color{0, 0, 0, 1})); // default
    CHECK(c.button_width_close == 30);
    CHECK(c.button_width_minimize == 20);   // falls back to ButtonWidth
    CHECK(c.button_width_app_menu == 20);   // falls back to the menu width
    CHECK(c.explicit_button_spacer == 10);  // default
    CHECK(c.title_edges_maximized == (Margins{0, 0, 0, 0}));
    CHECK(c.padding == (Margins{7, 8, 9, 10}));
}

TEST_CASE(theme_config_alignment_fallthrough)
{
    // Like KWin, anything unknown means right / bottom.
    const auto c = ThemeConfig::from_ini(IniFile::parse(
        "[General]\nTitleAlignment=middle\nTitleVerticalAlignment=TOP\n"));
    CHECK(c.title_alignment == HAlign::Right);
    CHECK(c.title_vertical_alignment == VAlign::Top);
}

TEST_CASE(button_layout_letters)
{
    const auto b = parse_button_layout("MS_HIAXFBLN?");
    const std::vector<ButtonType> expected{
        ButtonType::Menu, ButtonType::OnAllDesktops, ButtonType::Spacer, ButtonType::ContextHelp,
        ButtonType::Minimize, ButtonType::Maximize, ButtonType::Close, ButtonType::KeepAbove,
        ButtonType::KeepBelow, ButtonType::Shade, ButtonType::ApplicationMenu,
    };
    CHECK(b == expected);
}

TEST_CASE(settings_names)
{
    CHECK(parse_border_size("normal") == BorderSize::Normal);
    CHECK(parse_border_size("no_sides") == BorderSize::NoSides);
    CHECK(parse_border_size("theme") == BorderSize::Theme);
    CHECK(!parse_border_size("Normal"));

    CHECK_NEAR(*parse_button_size("tiny"), 0.8);
    CHECK_NEAR(*parse_button_size("normal"), 1.0);
    CHECK_NEAR(*parse_button_size("huge"), 1.6);
    CHECK(!parse_button_size("big"));
}

TEST_CASE(qt_font_strings)
{
    // Qt 6: 16 fields, CSS weights.
    CHECK(kde::qt_font_to_pango("Noto Sans,10,-1,5,700,0,0,0,0,0,0,0,0,0,0,1") == "Noto Sans Bold 10");
    CHECK(kde::qt_font_to_pango("Noto Sans,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1") == "Noto Sans 10");
    // Qt 5: 10 fields, weights 0..99.
    CHECK(kde::qt_font_to_pango("Noto Sans,9,-1,5,75,0,0,0,0,0") == "Noto Sans Bold 9");
    CHECK(kde::qt_font_to_pango("DejaVu Serif,11,-1,5,50,1,0,0,0,0") == "DejaVu Serif Italic 11");
    CHECK(!kde::qt_font_to_pango(""));
}
