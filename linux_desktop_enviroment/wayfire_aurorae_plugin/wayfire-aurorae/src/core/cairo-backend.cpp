#include "cairo-backend.hpp"
#include "raster.hpp"

#include <cairo.h>

namespace aurorae
{
void cairo_draw(cairo_t *cr, const DrawItem& item)
{
    if (!item.raster || !item.raster->surface() || item.dst.empty() || item.src.empty())
    {
        return;
    }

    cairo_save(cr);
    cairo_rectangle(cr, item.dst.x, item.dst.y, item.dst.width, item.dst.height);
    cairo_clip(cr);
    cairo_translate(cr, item.dst.x, item.dst.y);
    cairo_scale(cr, item.dst.width / item.src.width, item.dst.height / item.src.height);
    cairo_set_source_surface(cr, item.raster->surface(), -item.src.x, -item.src.y);
    // Bilinear with clamped edges, matching the GPU path.
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    cairo_paint(cr);
    cairo_restore(cr);
}

void cairo_draw(cairo_t *cr, const std::vector<DrawItem>& items)
{
    for (const auto& item : items)
    {
        cairo_draw(cr, item);
    }
}
} // namespace aurorae
