#include "test.hpp"

#include "core/decoration-layout.hpp"
#include "core/theme.hpp"

using namespace aurorae;

namespace
{
std::shared_ptr<Theme> theme()
{
    static auto t = Theme::load(test::data("TestTheme"));
    return t;
}

Settings settings(const char *left = "", const char *right = "IX")
{
    Settings s;
    s.buttons_left  = parse_button_layout(left);
    s.buttons_right = parse_button_layout(right);
    return s;
}

WindowState window(bool maximized = false)
{
    WindowState w;
    w.client    = {400, 300};
    w.maximized = maximized;
    w.resizable = !maximized;
    return w;
}
} // namespace

TEST_CASE(theme_loads_by_path)
{
    std::string error;
    auto t = Theme::load(test::data("TestTheme"), &error);
    CHECK(t != nullptr);
    CHECK(t && t->name() == "TestTheme");
    CHECK(t && t->button(ButtonType::Close) && t->button(ButtonType::Minimize));
    CHECK(t && t->button(ButtonType::Maximize) && t->button(ButtonType::Maximize)->restore);
    CHECK(t && !t->button(ButtonType::Shade)); // no shade.svg: no such button
    CHECK(!Theme::load(test::data("NoSuchTheme"), &error));
}

TEST_CASE(borders_follow_kde_border_sizes)
{
    // top = max(TitleHeight 18, ButtonHeight 16 + ButtonMarginTop 1) + edges 2 + 3
    CHECK(theme()->borders(BorderSize::Normal, 1.0) == (Margins{4, 23, 6, 5})); // clamped to 4..6
    CHECK(theme()->borders(BorderSize::Theme, 1.0) == (Margins{3, 23, 10, 5}));
    CHECK(theme()->borders(BorderSize::None, 1.0) == (Margins{0, 23, 0, 0}));
    CHECK(theme()->borders(BorderSize::NoSides, 1.0) == (Margins{0, 23, 0, 4}));
    CHECK(theme()->borders(BorderSize::Huge, 1.0) == (Margins{12, 23, 12, 12}));
    // Larger buttons grow the title bar: 16 * 1.6 + 1 = 26.6 > 18
    CHECK_NEAR(theme()->borders(BorderSize::Normal, 1.6).top, 26.6 + 5);
    CHECK(theme()->maximized_borders(1.0) == (Margins{0, 18, 0, 0}));
}

TEST_CASE(geometry_normal_window)
{
    const auto g = compute_geometry(*theme(), settings("", "IX"), window());
    CHECK(g.size == (SizeF{410, 328}));
    CHECK(g.client == (RectF{4, 23, 400, 300}));
    CHECK(g.frame == (RectF{-7, -8, 426, 346}));
    CHECK(g.frame_borders == AllBorders);
    CHECK(g.title_bar == (RectF{4, 0, 400, 23}));

    CHECK(g.buttons.size() == 2);
    CHECK(g.buttons[0].type == ButtonType::Minimize && g.buttons[0].rect == (RectF{352, 3, 20, 16}));
    CHECK(g.buttons[1].type == ButtonType::Close && g.buttons[1].rect == (RectF{374, 3, 30, 16}));
    CHECK(g.caption == (RectF{9, 2, 336, 18}));
}

TEST_CASE(geometry_maximized_window)
{
    const auto g = compute_geometry(*theme(), settings(), window(true));
    CHECK(g.borders == (Margins{0, 18, 0, 0}));
    CHECK(g.frame == (RectF{0, 0, 400, 318}));
    CHECK(g.frame_borders == NoBorder);
    CHECK(g.inner_border.empty());
    CHECK(g.buttons[1].rect == (RectF{370, 0, 30, 16}));
}

TEST_CASE(geometry_hidden_buttons_and_spacers)
{
    auto w = window();
    w.buttons[size_t(ButtonType::Minimize)].visible = false;
    auto g = compute_geometry(*theme(), settings("_", "IX"), w);
    CHECK(g.buttons.size() == 2);
    CHECK(g.buttons[0].type == ButtonType::Spacer && g.buttons[0].rect.width == 10);
    CHECK(g.buttons[1].type == ButtonType::Close && g.buttons[1].rect.x == 374);
    // Caption starts after the spacer: 4 + 10 + TitleBorderLeft 5.
    CHECK(g.caption.x == 19);
}

TEST_CASE(hit_testing)
{
    const Settings s = settings();
    const auto g = compute_geometry(*theme(), s, window());
    auto hit = [&] (double x, double y) { return hit_test(g, s, true, {x, y}); };
    using K = HitResult::Kind;

    CHECK(hit(380, 10).kind == K::Button && hit(380, 10).button == 1);
    CHECK(hit(100, 10).kind == K::TitleBar);
    CHECK(hit(100, 100).kind == K::None);                      // client area
    CHECK(hit(1, 200).kind == K::Resize && hit(1, 200).edges == EdgeLeft);
    CHECK(hit(408, 326).edges == (EdgeRight | EdgeBottom));
    CHECK(hit(200, -4).kind == K::Resize && hit(200, -4).edges == EdgeTop); // above the title bar
    CHECK(hit(2, -3).edges == (EdgeTop | EdgeLeft));
    CHECK(hit(200, -20).kind == K::None);                      // shadow: click-through
    CHECK(hit(-6, 100).kind == K::None);

    // Not resizable (e.g. maximized): borders still belong to the frame.
    CHECK(hit_test(g, s, false, {1, 200}).kind == K::Frame);
    CHECK(hit_test(g, s, false, {200, -4}).kind == K::None);
}
