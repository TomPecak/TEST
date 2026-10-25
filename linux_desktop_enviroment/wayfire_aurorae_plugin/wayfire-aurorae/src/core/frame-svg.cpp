#include "frame-svg.hpp"
#include "svg-document.hpp"

#include <cmath>

namespace aurorae
{
namespace
{
constexpr std::array<std::string_view, PIECE_COUNT> PIECE_NAMES = {
    "center", "top", "bottom", "left", "right", "topleft", "topright", "bottomleft", "bottomright",
    "overlay",
};

// FrameSvgHelpers::sectionRect()
RectF section_rect(Piece piece, const RectF& c, SizeF full)
{
    switch (piece)
    {
      case Piece::Center:
      case Piece::Overlay:
        return c;

      case Piece::Top:
        return {c.left(), 0, c.width, c.top()};

      case Piece::Bottom:
        return {c.left(), c.bottom(), c.width, full.height - c.bottom()};

      case Piece::Left:
        return {0, c.top(), c.left(), c.height};

      case Piece::Right:
        return {c.right(), c.top(), full.width - c.right(), c.height};

      case Piece::TopLeft:
        return {0, 0, c.left(), c.top()};

      case Piece::TopRight:
        return {c.right(), 0, full.width - c.right(), c.top()};

      case Piece::BottomLeft:
        return {0, c.bottom(), c.left(), full.height - c.bottom()};

      case Piece::BottomRight:
        return {c.right(), c.bottom(), full.width - c.right(), full.height - c.bottom()};
    }

    return {};
}

// KSvg's pixel snapping for corners and tiled borders: QRectF::setTopLeft()
// with the rounded top-left (keeping the bottom-right), then ceil the size.
RectF snap(RectF r)
{
    const double right  = r.right();
    const double bottom = r.bottom();
    r.x = qround(r.x);
    r.y = qround(r.y);
    r.width  = std::ceil(right - r.x);
    r.height = std::ceil(bottom - r.y);
    return r;
}

Borders piece_borders(Piece p)
{
    switch (p)
    {
      case Piece::Top:
        return TopBorder;

      case Piece::Bottom:
        return BottomBorder;

      case Piece::Left:
        return LeftBorder;

      case Piece::Right:
        return RightBorder;

      case Piece::TopLeft:
        return TopBorder | LeftBorder;

      case Piece::TopRight:
        return TopBorder | RightBorder;

      case Piece::BottomLeft:
        return BottomBorder | LeftBorder;

      case Piece::BottomRight:
        return BottomBorder | RightBorder;

      case Piece::Center:
      case Piece::Overlay:
        return NoBorder;
    }

    return NoBorder;
}
} // namespace

bool FrameSvg::has_prefix(const SvgDocument& doc, std::string_view prefix)
{
    if (prefix.empty())
    {
        return doc.has_element("center");
    }

    std::string id(prefix);
    if (id.back() != '-')
    {
        id += '-';
    }

    return doc.has_element(id + "center");
}

FrameSvg::FrameSvg(std::shared_ptr<const SvgDocument> document, std::string_view requested_prefix) :
    doc(std::move(document))
{
    // Like KSvg, a prefix without a center element falls back to no prefix.
    std::string prefix;
    if (!requested_prefix.empty() && has_prefix(*doc, requested_prefix))
    {
        prefix = std::string(requested_prefix) + "-";
    }

    for (size_t i = 0; i < PIECE_COUNT; ++i)
    {
        ids[i]    = prefix + std::string(PIECE_NAMES[i]);
        exists[i] = doc->has_element(ids[i]);
        sizes[i]  = doc->element_size(ids[i]);
    }

    auto hint_rect = [&] (const char *name) { return doc->element_rect(prefix + name); };
    auto hint = [&] (const char *name)
    {
        // Unprefixed variants exist for backwards compatibility.
        return doc->has_element(name) || doc->has_element(prefix + name);
    };

    // Margins come from the hint elements if present, else from the borders.
    const auto top    = hint_rect("hint-top-margin");
    const auto bottom = hint_rect("hint-bottom-margin");
    const auto left   = hint_rect("hint-left-margin");
    const auto right  = hint_rect("hint-right-margin");
    fixed_margins = {
        left ? left->width : sizes[size_t(Piece::Left)].width,
        top ? top->height : sizes[size_t(Piece::Top)].height,
        right ? right->width : sizes[size_t(Piece::Right)].width,
        bottom ? bottom->height : sizes[size_t(Piece::Bottom)].height,
    };

    no_border_padding = hint("hint-no-border-padding");
    stretch_borders   = hint("hint-stretch-borders");
    tile_center = hint("hint-tile-center");

    // Overlay (FrameSvgPrivate::generateBackground). Masked overlays need a
    // "mask-" frame, which Aurorae themes do not have; those are skipped.
    if (exists[size_t(Piece::Overlay)] && !doc->has_element("mask-" + prefix + "center"))
    {
        auto has = [&] (const char *name) { return doc->has_element(prefix + name); };
        overlay_tile_h = has("hint-overlay-tile-horizontal");
        overlay_tile_v = has("hint-overlay-tile-vertical");
        overlay = has("hint-overlay-pos-right") ? OverlayMode::PosRight :
            has("hint-overlay-pos-bottom") ? OverlayMode::PosBottom :
            has("hint-overlay-stretch") ? OverlayMode::Stretch :
            (overlay_tile_h || overlay_tile_v) ? OverlayMode::Tile : OverlayMode::Place;
    }
}

Margins FrameSvg::margins(Borders enabled) const
{
    if (no_border_padding)
    {
        return {};
    }

    return {
        (enabled & LeftBorder) ? fixed_margins.left : 0,
        (enabled & TopBorder) ? fixed_margins.top : 0,
        (enabled & RightBorder) ? fixed_margins.right : 0,
        (enabled & BottomBorder) ? fixed_margins.bottom : 0,
    };
}

Size FrameSvg::raster_size(Piece p, double dpr) const
{
    const SizeF s = sizes[size_t(p)];
    return {qround(s.width * dpr), qround(s.height * dpr)};
}

void FrameSvg::layout_overlay(Size frame_size, double dpr, std::vector<FramePiece>& out) const
{
    // KSvg paints the overlay element into an image of overlaySize at
    // position pos, then copies that image's (pos, overlaySize) part to pos
    // on the frame. So the element covers (pos, overlaySize), but only the
    // part inside (0, 0, overlaySize) - and inside the frame - is visible.
    const SizeF natural = sizes[size_t(Piece::Overlay)];
    const SizeF frame{double(frame_size.width), double(frame_size.height)};
    SizeF size = natural;
    PointF pos{0, 0};
    switch (overlay)
    {
      case OverlayMode::None:
        return;

      case OverlayMode::Place:
        break;

      case OverlayMode::PosRight:
        pos.x = frame.width - size.width;
        break;

      case OverlayMode::PosBottom:
        pos.y = frame.height - size.height;
        break;

      case OverlayMode::Stretch:
        size = frame;
        break;

      case OverlayMode::Tile:
        size = {overlay_tile_h ? frame.width : size.width, overlay_tile_v ? frame.height : size.height};
        break;
    }

    if (size.empty())
    {
        return;
    }

    const RectF dst{pos.x * dpr, pos.y * dpr, size.width * dpr, size.height * dpr};
    RectF clip{0, 0, std::min(size.width, frame.width) * dpr, std::min(size.height, frame.height) * dpr};
    const bool tiled = overlay == OverlayMode::Tile;
    out.push_back({Piece::Overlay, dst, raster_size(Piece::Overlay, dpr), tiled, clip});
}

void FrameSvg::layout(Size frame_size, Borders enabled, double dpr, std::vector<FramePiece>& out) const
{
    if (!drawable() || frame_size.empty())
    {
        return;
    }

    auto size_of = [&] (Piece p) { return sizes[size_t(p)]; };
    const double top_height    = (enabled & TopBorder) ? size_of(Piece::Top).height : 0;
    const double bottom_height = (enabled & BottomBorder) ? size_of(Piece::Bottom).height : 0;
    const double left_width    = (enabled & LeftBorder) ? size_of(Piece::Left).width : 0;
    const double right_width   = (enabled & RightBorder) ? size_of(Piece::Right).width : 0;

    // FrameSvgPrivate::generateFrameBackground() / contentGeometry()
    const SizeF size{frame_size.width * dpr, frame_size.height * dpr};
    const SizeF full{double(qround(frame_size.width * dpr)), double(qround(frame_size.height * dpr))};
    RectF content{0, 0,
        size.width - (left_width + right_width) * dpr,
        size.height - (top_height + bottom_height) * dpr};
    if ((enabled & LeftBorder) && exists[size_t(Piece::Left)])
    {
        content.x += left_width * dpr;
    }

    if ((enabled & TopBorder) && exists[size_t(Piece::Top)])
    {
        content.y += top_height * dpr;
    }

    // Center
    if (!content.empty())
    {
        out.push_back({Piece::Center, content, raster_size(Piece::Center, dpr), tile_center, std::nullopt});
    }

    // Corners, drawn only if both adjacent borders are enabled.
    for (Piece corner : {Piece::TopLeft, Piece::TopRight, Piece::BottomLeft, Piece::BottomRight})
    {
        const Borders needed = piece_borders(corner);
        if (((enabled & needed) != needed) || !exists[size_t(corner)])
        {
            continue;
        }

        const RectF r = snap(section_rect(corner, content, full));
        if (!r.empty())
        {
            out.push_back({corner, r, raster_size(corner, dpr), false, std::nullopt});
        }
    }

    // Sides. KSvg checks a nominal size: the fixed border thickness by the
    // element's own length along the edge.
    const struct
    {
        Piece piece;
        SizeF nominal;
    } sides[] = {
        {Piece::Left, {left_width, size_of(Piece::Left).height}},
        {Piece::Right, {right_width, size_of(Piece::Right).height}},
        {Piece::Top, {size_of(Piece::Top).width, top_height}},
        {Piece::Bottom, {size_of(Piece::Bottom).width, bottom_height}},
    };

    for (const auto& side : sides)
    {
        const SizeF nominal{side.nominal.width * dpr, side.nominal.height * dpr};
        if (!(enabled & piece_borders(side.piece)) || !exists[size_t(side.piece)] || nominal.empty())
        {
            continue;
        }

        if (stretch_borders)
        {
            const RectF r = section_rect(side.piece, content, full);
            if (!r.empty())
            {
                out.push_back({side.piece, r, raster_size(side.piece, dpr), false, std::nullopt});
            }
        } else
        {
            const RectF r = snap(section_rect(side.piece, content, full));
            const Size tile{int(std::ceil(nominal.width)), int(std::ceil(nominal.height))};
            if (!r.empty())
            {
                out.push_back({side.piece, r, tile, true, std::nullopt});
            }
        }
    }

    layout_overlay(frame_size, dpr, out);
}
} // namespace aurorae
