#include "draw-list.hpp"
#include "raster.hpp"
#include "svg-document.hpp"

#include <cairo.h>

namespace aurorae
{
namespace
{
// Strips are at least this long, so a tiled edge needs few quads.
constexpr int MIN_STRIP_LENGTH = 512;

int repeats_for(int tile)
{
    return std::max(1, (MIN_STRIP_LENGTH + tile - 1) / tile);
}

const Raster *plain_raster(RasterCache& cache, const SvgDocument& doc, const std::string& id, Size size)
{
    return cache.get({&doc, id, size, {}, RasterKind::Plain},
        [&] { return doc.render_element(id, size); });
}

const Raster *strip_raster(RasterCache& cache, const SvgDocument& doc, const std::string& id,
    Size tile, Size strip)
{
    return cache.get({&doc, id, strip, tile, RasterKind::TileStrip}, [&] () -> SurfacePtr
    {
        auto tile_image = doc.render_element(id, tile);
        if (!tile_image)
        {
            return nullptr;
        }

        auto surface = make_surface(strip);
        cairo_t *cr  = cairo_create(surface.get());
        cairo_pattern_t *pattern = cairo_pattern_create_for_surface(tile_image.get());
        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
        cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
        cairo_set_source(cr, pattern);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_pattern_destroy(pattern);
        cairo_destroy(cr);
        return surface;
    });
}

std::optional<RectF> intersect(const RectF& a, const RectF& b)
{
    const double l = std::max(a.x, b.x), t = std::max(a.y, b.y);
    const double r = std::min(a.right(), b.right()), bt = std::min(a.bottom(), b.bottom());
    if ((r <= l) || (bt <= t))
    {
        return std::nullopt;
    }

    return RectF{l, t, r - l, bt - t};
}

// Appends src -> dst, cropped to clip (same coordinates as dst).
void append_clipped(std::vector<DrawItem>& out, const Raster *raster, RectF src, RectF dst,
    const std::optional<RectF>& clip)
{
    if (!clip)
    {
        out.push_back({raster, src, dst});
        return;
    }

    const auto visible = intersect(dst, *clip);
    if (!visible)
    {
        return;
    }

    const double sx = src.width / dst.width, sy = src.height / dst.height;
    const RectF cropped{
        src.x + (visible->x - dst.x) * sx, src.y + (visible->y - dst.y) * sy,
        visible->width * sx, visible->height * sy,
    };
    out.push_back({raster, cropped, *visible});
}

// Covers dst with copies of a strip whose content repeats every `tile`
// pixels, starting at dst's top-left corner like QPainter::drawTiledPixmap().
void append_tiled(std::vector<DrawItem>& out, const Raster *strip, RectF dst, const std::optional<RectF>& clip)
{
    const Size s = strip->size();
    for (double y = 0; y < dst.height; y += s.height)
    {
        const double h = std::min<double>(s.height, dst.height - y);
        for (double x = 0; x < dst.width; x += s.width)
        {
            const double w = std::min<double>(s.width, dst.width - x);
            append_clipped(out, strip, {0, 0, w, h}, {dst.x + x, dst.y + y, w, h}, clip);
        }
    }
}
} // namespace

void append_frame(std::vector<DrawItem>& out, RasterCache& cache, const FrameSvg& frame,
    Size frame_size, Borders borders, double dpr, PointF origin_px)
{
    static thread_local std::vector<FramePiece> pieces;
    pieces.clear();
    frame.layout(frame_size, borders, dpr, pieces);

    const SvgDocument& doc = frame.document();
    for (const auto& piece : pieces)
    {
        if (piece.raster.empty())
        {
            continue;
        }

        const std::string& id = frame.element(piece.piece);
        const RectF dst = piece.dst.translated(origin_px.x, origin_px.y);
        std::optional<RectF> clip;
        if (piece.clip)
        {
            clip = piece.clip->translated(origin_px.x, origin_px.y);
        }

        if (!piece.tiled)
        {
            if (auto *r = plain_raster(cache, doc, id, piece.raster))
            {
                append_clipped(out, r, {0, 0, double(r->size().width), double(r->size().height)}, dst, clip);
            }

            continue;
        }

        // Pieces repeat along their edge; the center and overlays both ways.
        const bool along_x = piece.piece != Piece::Left && piece.piece != Piece::Right;
        const bool along_y = piece.piece != Piece::Top && piece.piece != Piece::Bottom;
        const Size strip{
            piece.raster.width * (along_x ? repeats_for(piece.raster.width) : 1),
            piece.raster.height * (along_y ? repeats_for(piece.raster.height) : 1),
        };
        if (auto *r = strip_raster(cache, doc, id, piece.raster, strip))
        {
            append_tiled(out, r, dst, clip);
        }
    }
}
void clip_items(std::vector<DrawItem>& items, size_t first, const RectF& clip)
{
    std::vector<DrawItem> clipped;
    for (size_t i = first; i < items.size(); ++i)
    {
        append_clipped(clipped, items[i].raster, items[i].src, items[i].dst, clip);
    }

    items.resize(first);
    items.insert(items.end(), clipped.begin(), clipped.end());
}
} // namespace aurorae
