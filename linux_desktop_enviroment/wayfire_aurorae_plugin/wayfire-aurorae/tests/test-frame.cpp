#include "test.hpp"

#include "core/draw-list.hpp"
#include "core/frame-svg.hpp"
#include "core/raster.hpp"
#include "core/svg-document.hpp"

#include <cairo.h>

using namespace aurorae;

namespace
{
std::shared_ptr<SvgDocument> frame_doc()
{
    static auto doc = SvgDocument::load(test::data("frame.svg"));
    return doc;
}

uint32_t pixel(cairo_surface_t *s, int x, int y)
{
    cairo_surface_flush(s);
    auto *row = cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s);
    return reinterpret_cast<uint32_t*>(row)[x];
}

const FramePiece *find(const std::vector<FramePiece>& pieces, Piece p)
{
    for (const auto& piece : pieces)
    {
        if (piece.piece == p)
        {
            return &piece;
        }
    }

    return nullptr;
}
} // namespace

TEST_CASE(svg_geometry_like_qtsvg)
{
    auto doc = frame_doc();
    CHECK(doc != nullptr);
    // Ancestor transforms count.
    CHECK(doc->element_rect("decoration-topleft") == (RectF{5, 7, 10, 20}));
    // Strokes count, half on each side.
    CHECK(doc->element_rect("stroked") == (RectF{9, 79, 22, 12}));
    // Elements inside hidden groups and off the canvas still exist.
    CHECK(doc->has_element("decoration-hint-top-margin"));
    CHECK(doc->element_rect("outside") == (RectF{150, -40, 20, 10}));
    CHECK(!doc->has_element("missing"));
}

TEST_CASE(svg_render_fills_exact_pixels)
{
    auto doc = frame_doc();
    auto top = doc->render_element("decoration-top", {30, 20});
    CHECK(top != nullptr);
    CHECK(pixel(top.get(), 0, 0) == 0xff00ff00);
    CHECK(pixel(top.get(), 29, 19) == 0xff00ff00);

    // Off-canvas elements render too (librsvg would clip them).
    auto outside = doc->render_element("outside", {20, 10});
    CHECK(outside && pixel(outside.get(), 10, 5) == 0xff112233);
}

TEST_CASE(frame_margins_and_prefix)
{
    FrameSvg frame(frame_doc(), "decoration");
    CHECK(frame.drawable());
    CHECK(frame.element(Piece::Top) == "decoration-top");
    // hint-top-margin overrides the top element's height.
    CHECK(frame.margins() == (Margins{10, 15, 12, 8}));
    CHECK(frame.margins(LeftBorder | BottomBorder) == (Margins{10, 0, 0, 8}));

    // A prefix without a center element falls back to no prefix (KSvg).
    FrameSvg missing(frame_doc(), "decoration-inactive");
    CHECK(missing.element(Piece::Center) == "center");
    CHECK(!missing.drawable());
}

TEST_CASE(frame_layout_tiled)
{
    FrameSvg frame(frame_doc(), "decoration");
    std::vector<FramePiece> pieces;
    frame.layout({200, 100}, AllBorders, 1.0, pieces);
    CHECK(pieces.size() == 9);

    const auto *center = find(pieces, Piece::Center);
    CHECK(center && center->dst == (RectF{10, 20, 178, 72}) && !center->tiled);
    CHECK(center && center->raster == (Size{30, 30}));
    CHECK(find(pieces, Piece::TopLeft)->dst == (RectF{0, 0, 10, 20}));
    CHECK(find(pieces, Piece::TopRight)->dst == (RectF{188, 0, 12, 20}));
    CHECK(find(pieces, Piece::BottomRight)->dst == (RectF{188, 92, 12, 8}));

    // Without hint-stretch-borders the sides tile their natural-size image.
    const auto *top = find(pieces, Piece::Top);
    CHECK(top && top->tiled && top->dst == (RectF{10, 0, 178, 20}) && top->raster == (Size{30, 20}));
    const auto *right = find(pieces, Piece::Right);
    CHECK(right && right->tiled && right->dst == (RectF{188, 20, 12, 72}) && right->raster == (Size{12, 30}));
}

TEST_CASE(frame_layout_hidpi)
{
    FrameSvg frame(frame_doc(), "decoration");
    std::vector<FramePiece> pieces;
    frame.layout({200, 100}, AllBorders, 2.0, pieces);
    CHECK(find(pieces, Piece::Center)->dst == (RectF{20, 40, 356, 144}));
    CHECK(find(pieces, Piece::Center)->raster == (Size{60, 60}));
    CHECK(find(pieces, Piece::Top)->raster == (Size{60, 40}));
}

TEST_CASE(frame_layout_disabled_borders)
{
    // Maximized decorations use the center element only.
    FrameSvg frame(frame_doc(), "decoration");
    std::vector<FramePiece> pieces;
    frame.layout({200, 100}, NoBorder, 1.0, pieces);
    CHECK(pieces.size() == 1);
    CHECK(pieces[0].piece == Piece::Center && pieces[0].dst == (RectF{0, 0, 200, 100}));
}

TEST_CASE(frame_layout_stretched)
{
    auto doc = SvgDocument::load(test::data("frame-stretch.svg"));
    FrameSvg frame(doc, "decoration"); // no "decoration-center": unprefixed
    CHECK(frame.drawable());
    std::vector<FramePiece> pieces;
    frame.layout({50, 40}, AllBorders, 1.0, pieces);
    const auto *top = find(pieces, Piece::Top);
    CHECK(top && !top->tiled && top->dst == (RectF{4, 0, 42, 6}) && top->raster == (Size{3, 6}));
    CHECK(find(pieces, Piece::Center)->dst == (RectF{4, 6, 42, 30}));
}

TEST_CASE(draw_list_covers_frame_once)
{
    // The quads of a frame partition it: total area equals the frame area.
    FrameSvg frame(frame_doc(), "decoration");
    RasterCache cache;
    std::vector<DrawItem> items;
    append_frame(items, cache, frame, {700, 300}, AllBorders, 1.0, {0, 0});
    double area = 0;
    for (const auto& item : items)
    {
        CHECK(item.raster != nullptr);
        area += item.dst.width * item.dst.height;
    }

    CHECK_NEAR(area, 700.0 * 300.0);

    // Rasters are shared: a second frame of another size adds none.
    const size_t rasters = cache.size();
    items.clear();
    append_frame(items, cache, frame, {640, 480}, AllBorders, 1.0, {0, 0});
    CHECK(cache.size() == rasters);
}
