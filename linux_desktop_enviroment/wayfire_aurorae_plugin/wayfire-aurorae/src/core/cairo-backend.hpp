#pragma once

#include "draw-list.hpp"

#include <vector>

typedef struct _cairo cairo_t;

namespace aurorae
{
// Software backend: draws quads with cairo, in device pixels. Used by the
// preview tool and the tests; the compositor uses the GPU backend instead.
void cairo_draw(cairo_t *cr, const DrawItem& item);
void cairo_draw(cairo_t *cr, const std::vector<DrawItem>& items);
} // namespace aurorae
