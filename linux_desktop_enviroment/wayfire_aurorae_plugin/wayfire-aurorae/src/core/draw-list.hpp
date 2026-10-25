#pragma once

#include "frame-svg.hpp"
#include "geometry.hpp"

#include <vector>

namespace aurorae
{
class Raster;
class RasterCache;

/**
 * One textured quad: a region of a raster drawn into a rectangle, stretched
 * with bilinear filtering if the sizes differ. This is the only primitive a
 * rendering backend has to implement.
 */
struct DrawItem
{
    const Raster *raster = nullptr;
    RectF src; // in raster pixels
    RectF dst; // in device pixels
};

/**
 * Appends the quads of a laid-out frame. Rasters come from (and are added
 * to) the cache, so every distinct element image is rendered once and then
 * shared. Tiled pieces use pre-tiled strips, so a long edge costs a few quads
 * rather than one per tile.
 *
 * @param origin_px Device-pixel position of the frame's top-left corner.
 */
void append_frame(std::vector<DrawItem>& out, RasterCache& cache, const FrameSvg& frame,
    Size frame_size, Borders borders, double dpr, PointF origin_px);

// Crops items[first..] to clip (device pixels), dropping what falls outside.
void clip_items(std::vector<DrawItem>& items, size_t first, const RectF& clip);
} // namespace aurorae
