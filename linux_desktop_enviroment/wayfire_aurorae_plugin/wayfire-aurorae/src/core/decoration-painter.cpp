#include "decoration-painter.hpp"
#include "raster.hpp"
#include "theme.hpp"

namespace aurorae
{
namespace
{
PointF device_pos(PointF origin_px, double x, double y, double dpr)
{
    return {origin_px.x + x * dpr, origin_px.y + y * dpr};
}

const FrameSvg *button_frame(const DecorationScene& scene, const LaidOutButton& button,
    const ButtonPointerState& pointer)
{
    const ButtonAsset *asset = scene.theme.button(button.type);
    if (!asset)
    {
        return nullptr;
    }

    // Toggle buttons look pressed while checked; maximize uses the restore
    // SVG for that instead (DecorationButton::states()).
    ButtonStates states;
    states.window_active = scene.active;
    states.hovered  = pointer.hovered;
    states.pressed  = pointer.pressed || (button.checked && button.type != ButtonType::Maximize);
    states.disabled = !button.enabled;

    const ButtonFrameSet& set = (button.type == ButtonType::Maximize && scene.maximized && asset->restore) ?
        *asset->restore : asset->normal;
    return &set.select(states);
}
} // namespace

void append_frame_items(std::vector<DrawItem>& out, const DecorationScene& scene, double dpr,
    PointF origin_px)
{
    const auto& g = scene.geometry;
    const FrameSvg& frame = scene.theme.decoration(scene.active, scene.maximized);
    append_frame(out, scene.theme.rasters(), frame, g.frame_size, g.frame_borders, dpr,
        device_pos(origin_px, g.frame.x, g.frame.y, dpr));

    if (!g.inner_border.empty())
    {
        if (const FrameSvg *inner = scene.theme.inner_border(scene.active))
        {
            // KDecoration paints inside the decoration rectangle only.
            const size_t first = out.size();
            append_frame(out, scene.theme.rasters(), *inner, g.inner_border_size, AllBorders, dpr,
                device_pos(origin_px, g.inner_border.x, g.inner_border.y, dpr));
            const PointF o = device_pos(origin_px, 0, 0, dpr);
            clip_items(out, first, {o.x, o.y, g.size.width * dpr, g.size.height * dpr});
        }
    }
}

void append_button_item(std::vector<DrawItem>& out, const DecorationScene& scene, size_t index,
    double dpr, PointF origin_px)
{
    const auto& button = scene.geometry.buttons[index];
    const ButtonPointerState pointer = index < scene.pointer.size() ? scene.pointer[index] : ButtonPointerState{};
    const PointF pos = device_pos(origin_px, button.rect.x, button.rect.y, dpr);

    if (const FrameSvg *frame = button_frame(scene, button, pointer))
    {
        const Size size{qround(button.rect.width), qround(button.rect.height)};
        append_frame(out, scene.theme.rasters(), *frame, size, AllBorders, dpr, pos);
        return;
    }

    // Menu buttons without an SVG show the window icon (IconDecorationButton).
    const bool is_menu = button.type == ButtonType::Menu || button.type == ButtonType::ApplicationMenu;
    if (is_menu && scene.icon)
    {
        const Size s = scene.icon->size();
        const double side = std::min(button.rect.width, button.rect.height) * dpr;
        const RectF dst{
            pos.x + (button.rect.width * dpr - side) / 2.0,
            pos.y + (button.rect.height * dpr - side) / 2.0,
            side, side,
        };
        out.push_back({scene.icon, {0, 0, double(s.width), double(s.height)}, dst});
    }
}

void append_button_items(std::vector<DrawItem>& out, const DecorationScene& scene, double dpr,
    PointF origin_px)
{
    for (size_t i = 0; i < scene.geometry.buttons.size(); ++i)
    {
        append_button_item(out, scene, i, dpr, origin_px);
    }
}

CaptionStyle caption_style(const Theme& theme, const Settings& settings, bool active)
{
    const auto& cfg = theme.config();
    CaptionStyle style;
    style.font   = settings.font;
    style.color  = active ? cfg.active_text_color : cfg.inactive_text_color;
    style.halign = cfg.title_alignment;
    style.valign = cfg.title_vertical_alignment;
    if (settings.legacy_text_shadow && cfg.use_text_shadow)
    {
        style.shadow = TextShadow{
            active ? cfg.active_text_shadow_color : cfg.inactive_text_shadow_color,
            double(cfg.text_shadow_offset_x), double(cfg.text_shadow_offset_y),
        };
    }

    return style;
}
} // namespace aurorae
