#include "decoration-layout.hpp"
#include "theme.hpp"

namespace aurorae
{
namespace
{
Size to_size(SizeF s)
{
    return {qround(s.width), qround(s.height)};
}

bool button_exists_in_theme(const Theme& theme, ButtonType type)
{
    switch (type)
    {
      case ButtonType::Spacer:
      // Menu buttons fall back to the window icon when the theme has no SVG.
      case ButtonType::Menu:
      case ButtonType::ApplicationMenu:
        return true;

      default:
        return theme.button(type) != nullptr;
    }
}

// KDecoration3::DecorationButtonGroup layout: visible buttons side by side,
// separated by the theme's spacing.
RectF layout_group(const Theme& theme, const Settings& settings, const WindowState& state,
    const std::vector<ButtonType>& types, int group, std::vector<LaidOutButton>& out)
{
    const double spacing = theme.config().button_spacing;
    const size_t first   = out.size();
    double width  = 0;
    double height = 0;
    for (ButtonType type : types)
    {
        const auto& avail = state.buttons[size_t(type)];
        if (!avail.visible || !button_exists_in_theme(theme, type))
        {
            continue;
        }

        const SizeF base = theme.button_size(type);
        const SizeF size{base.width * settings.button_size_factor, base.height * settings.button_size_factor};
        if (out.size() > first)
        {
            width += spacing;
        }

        out.push_back({type, RectF{width, 0, size.width, size.height}, group, avail.enabled, avail.checked});
        width += size.width;
        height = std::max(height, size.height);
    }

    return {0, 0, width, height};
}

void move_group(std::vector<LaidOutButton>& buttons, int group, PointF pos, RectF& rect)
{
    rect = rect.translated(pos.x, pos.y);
    for (auto& b : buttons)
    {
        if (b.group == group)
        {
            b.rect = b.rect.translated(pos.x, pos.y);
        }
    }
}
} // namespace

DecorationGeometry compute_geometry(const Theme& theme, const Settings& settings, const WindowState& state)
{
    const auto& cfg = theme.config();
    const double factor = settings.button_size_factor;

    DecorationGeometry g;
    g.borders = state.maximized ? theme.maximized_borders(factor) :
        theme.borders(settings.border_size, factor);
    g.size   = grown(state.client, g.borders);
    g.client = {g.borders.left, g.borders.top, state.client.width, state.client.height};

    // The decoration frame covers the whole window plus the shadow padding.
    // Maximized windows only use the center element, without padding.
    if (state.maximized)
    {
        g.frame = {0, 0, g.size.width, g.size.height};
        g.frame_borders = NoBorder;
    } else
    {
        const Margins& p = cfg.padding;
        g.frame = {-p.left, -p.top, g.size.width + p.left + p.right, g.size.height + p.top + p.bottom};
        g.frame_borders = AllBorders;
    }

    g.frame_size = to_size({g.frame.width, g.frame.height});

    // Inner border, as KWin's Aurorae v2 places it: a frame the size of the
    // whole decoration grown by its margins, at the left border and one
    // decoration top margin above the client. Only its top and left edges
    // end up inside the decoration, which is what KWin shows, so we match it.
    if (const FrameSvg *inner = theme.inner_border(state.active); inner && !state.maximized)
    {
        const Margins m = inner->margins();
        const Margins deco = theme.decoration(state.active, false).margins();
        g.inner_border = {g.borders.left - m.left, g.borders.top - deco.top,
            g.size.width + m.left + m.right, g.size.height + m.top + m.bottom};
        g.inner_border_size = to_size({g.inner_border.width, g.inner_border.height});
    }

    g.title_bar = {g.borders.left, 0, state.client.width, g.borders.top};

    // Buttons (Decoration::positionButtons)
    g.groups[0] = layout_group(theme, settings, state, settings.buttons_left, 0, g.buttons);
    g.groups[1] = layout_group(theme, settings, state, settings.buttons_right, 1, g.buttons);

    const Margins& edges = state.maximized ? cfg.title_edges_maximized : cfg.title_edges;
    const double margin  = state.maximized ? cfg.button_margin_top_maximized : cfg.button_margin_top;
    const RectF inner_title{edges.left, edges.top,
        g.size.width - edges.left - edges.right, g.borders.top - edges.top - edges.bottom};
    move_group(g.buttons, 0, {inner_title.x, inner_title.y + margin}, g.groups[0]);
    move_group(g.buttons, 1, {inner_title.right() - g.groups[1].width, inner_title.y + margin}, g.groups[1]);

    // Caption (Decoration::updateCaption)
    const double caption_left  = g.groups[0].right() + cfg.title_border_left;
    const double caption_right = g.groups[1].left() - cfg.title_border_right;
    g.caption = {caption_left, edges.top, caption_right - caption_left,
        std::max(cfg.title_height, cfg.button_height)};

    // Area we draw on or accept input in.
    g.bounds = g.frame.united({0, 0, g.size.width, g.size.height});
    if (!state.maximized && state.resizable)
    {
        const double ext = settings.extended_resize_border;
        const bool sides = (settings.border_size == BorderSize::None) ||
            (settings.border_size == BorderSize::NoSides);
        g.bounds = g.bounds.united({sides ? -ext : 0, -ext,
            g.size.width + (sides ? 2 * ext : 0), g.size.height + ext + (sides ? ext : 0)});
    }

    return g;
}

HitResult hit_test(const DecorationGeometry& g, const Settings& settings, bool resizable, PointF p)
{
    if (g.client.contains(p))
    {
        return {};
    }

    for (size_t i = 0; i < g.buttons.size(); ++i)
    {
        if ((g.buttons[i].type != ButtonType::Spacer) && g.buttons[i].rect.contains(p))
        {
            return {HitResult::Kind::Button, int(i), 0};
        }
    }

    const RectF deco{0, 0, g.size.width, g.size.height};
    if (deco.contains(p) && g.title_bar.contains(p))
    {
        return {HitResult::Kind::TitleBar};
    }

    const bool in_deco = deco.contains(p);
    if (!resizable)
    {
        return in_deco ? HitResult{HitResult::Kind::Frame} : HitResult{};
    }

    // Edges under the point. Inside the decoration the borders count; outside
    // it the extended resize area does (KDecoration's resize-only borders).
    const double ext = settings.extended_resize_border;
    const bool sides_ext = (settings.border_size == BorderSize::None) ||
        (settings.border_size == BorderSize::NoSides);
    uint32_t edges = 0;
    if (in_deco)
    {
        if (p.x < g.borders.left)
        {
            edges |= EdgeLeft;
        } else if (g.size.width - p.x <= g.borders.right)
        {
            edges |= EdgeRight;
        }

        if (g.size.height - p.y <= g.borders.bottom)
        {
            edges |= EdgeBottom;
        }
    } else
    {
        const bool horizontal_ok = sides_ext ? (p.x >= -ext && p.x < g.size.width + ext) :
            (p.x >= 0 && p.x < g.size.width);
        if ((p.y < 0) && (p.y >= -ext) && horizontal_ok)
        {
            edges |= EdgeTop;
        }

        if (sides_ext)
        {
            if ((p.x < 0) && (p.x >= -ext))
            {
                edges |= EdgeLeft;
            } else if ((p.x >= g.size.width) && (p.x < g.size.width + ext))
            {
                edges |= EdgeRight;
            }

            if ((p.y >= g.size.height) && (p.y < g.size.height + ext))
            {
                edges |= EdgeBottom;
            }
        }
    }

    if (edges == 0)
    {
        return in_deco ? HitResult{HitResult::Kind::Frame} : HitResult{};
    }

    // Generous corners: near a corner, resize both dimensions.
    const double corner = 2 * settings.extended_resize_border;
    if (edges & (EdgeLeft | EdgeRight))
    {
        if (p.y >= g.size.height - corner)
        {
            edges |= EdgeBottom;
        } else if (p.y < 0)
        {
            edges |= EdgeTop;
        }
    }

    if (edges & (EdgeTop | EdgeBottom))
    {
        if (p.x < corner)
        {
            edges |= EdgeLeft;
        } else if (p.x >= g.size.width - corner)
        {
            edges |= EdgeRight;
        }
    }

    return {HitResult::Kind::Resize, -1, edges};
}
} // namespace aurorae
