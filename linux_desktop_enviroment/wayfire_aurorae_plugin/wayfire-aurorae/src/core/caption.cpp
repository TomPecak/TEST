#include "caption.hpp"

#include <cairo.h>
#include <pango/pangocairo.h>

namespace aurorae
{
namespace
{
void show_text(cairo_t *cr, PangoLayout *layout, const Color& color, double x, double y)
{
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
}


PangoLayout *make_layout(cairo_t *cr, std::string_view text, const CaptionStyle& style)
{
    // Grayscale antialiasing: subpixel (RGB) text is only correct when drawn
    // straight onto an opaque background, not into a translucent texture.
    cairo_font_options_t *options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_set_font_options(cr, options);
    cairo_font_options_destroy(options);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string(style.font.c_str());
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_text(layout, text.data(), int(text.size()));
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    return layout;
}

double logical_width(PangoLayout *layout)
{
    PangoRectangle logical;
    pango_layout_get_extents(layout, nullptr, &logical);
    return logical.width / double(PANGO_SCALE);
}
} // namespace

double caption_text_width(std::string_view text, const CaptionStyle& style)
{
    auto surface = make_surface({1, 1});
    cairo_t *cr  = cairo_create(surface.get());
    PangoLayout *layout = make_layout(cr, text, style);
    const double width  = logical_width(layout) + (style.shadow ? std::abs(style.shadow->offset_x) : 0);
    g_object_unref(layout);
    cairo_destroy(cr);
    return width;
}

CaptionImage render_caption(std::string_view text, const CaptionStyle& style, SizeF size, double dpr)
{
    if (text.empty() || size.empty())
    {
        return {};
    }

    // Lay the text out on a scratch context with the final scale, so that
    // measurements match the rendering.
    auto scratch = make_surface({1, 1});
    cairo_t *cr  = cairo_create(scratch.get());
    cairo_scale(cr, dpr, dpr);
    PangoLayout *layout = make_layout(cr, text, style);

    const double shadow_x = style.shadow ? style.shadow->offset_x : 0;
    const double natural  = logical_width(layout) + std::abs(shadow_x);
    double width = natural;
    double x     = 0;
    if (natural > size.width)
    {
        // Too long: elide to the full width.
        pango_layout_set_width(layout, int((size.width - std::abs(shadow_x)) * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
        width = size.width;
    } else if (style.halign != HAlign::Left)
    {
        const double free = size.width - natural;
        x = style.halign == HAlign::Center ? free / 2.0 : free;
        x = std::round(x * dpr) / dpr; // keep glyphs on the pixel grid
    }

    // Vertical placement like QPainter::drawText(): the line box is the
    // font's ascent + descent, the baseline sits ascent below its top, and
    // Qt's raster engine snaps glyph baselines down to whole pixels.
    PangoFontMetrics *metrics = pango_context_get_metrics(pango_layout_get_context(layout),
        pango_layout_get_font_description(layout), nullptr);
    const int ascent  = qround(pango_font_metrics_get_ascent(metrics) / double(PANGO_SCALE));
    const int descent = qround(pango_font_metrics_get_descent(metrics) / double(PANGO_SCALE));
    pango_font_metrics_unref(metrics);

    double top = 0;
    switch (style.valign)
    {
      case VAlign::Top:
        break;

      case VAlign::Center:
        top = (size.height - (ascent + descent)) / 2.0;
        break;

      case VAlign::Bottom:
        top = size.height - (ascent + descent);
        break;
    }

    const double baseline = std::floor(top + ascent);
    const double y = baseline - pango_layout_get_baseline(layout) / double(PANGO_SCALE);
    cairo_destroy(cr);

    CaptionImage image;
    image.x = x;
    image.surface = make_surface({int(std::ceil(width * dpr)), int(std::ceil(size.height * dpr))});
    cr = cairo_create(image.surface.get());
    cairo_scale(cr, dpr, dpr);
    cairo_font_options_t *options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_set_font_options(cr, options);
    cairo_font_options_destroy(options);
    pango_cairo_update_layout(cr, layout);

    const double text_x = shadow_x < 0 ? -shadow_x : 0;
    if (style.shadow)
    {
        show_text(cr, layout, style.shadow->color, text_x + shadow_x, y + style.shadow->offset_y);
    }

    show_text(cr, layout, style.color, text_x, y);

    g_object_unref(layout);
    cairo_destroy(cr);
    return image;
}
} // namespace aurorae
