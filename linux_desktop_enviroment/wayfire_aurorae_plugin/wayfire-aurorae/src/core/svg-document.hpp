#pragma once

#include "geometry.hpp"
#include "raster.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

typedef struct _RsvgHandle RsvgHandle;

namespace aurorae
{
/**
 * A parsed SVG/SVGZ file with the element queries KSvg::Svg offers.
 *
 * Geometry follows QtSvg, which KSvg is built on: an element's rect is the
 * bounding box of its shapes (strokes included) and images, in document user
 * units with all ancestor transforms applied; filters and clipping do not
 * count. An element "exists" only if that rect is non-empty. Rendering maps
 * the rect onto the requested pixel size.
 *
 * librsvg measures differently, so the file is preprocessed (see
 * svg-document.cpp) into two documents: one for rendering and one, stripped
 * of filters, clips and masks and with images replaced by their outlines,
 * for measuring.
 *
 * Geometry results are memoized. Not thread-safe (librsvg handles are
 * single-threaded).
 */
class SvgDocument
{
  public:
    static std::shared_ptr<SvgDocument> load(const std::string& path, std::string *error = nullptr);
    ~SvgDocument();

    SvgDocument(const SvgDocument&) = delete;
    SvgDocument& operator =(const SvgDocument&) = delete;

    const std::string& path() const
    {
        return file_path;
    }

    bool has_element(std::string_view id) const;
    std::optional<RectF> element_rect(std::string_view id) const;

    // KSvg::Svg::elementSize(): the element rect's size, rounded.
    SizeF element_size(std::string_view id) const;

    // Rasterizes one element stretched to exactly size_px.
    SurfacePtr render_element(std::string_view id, Size size_px) const;

  private:
    SvgDocument(RsvgHandle *render, RsvgHandle *measure, std::string path, PointF render_offset);

    RsvgHandle *render_handle;
    RsvgHandle *measure_handle;
    std::string file_path;
    // Where the measuring document's origin lies in the rendering document.
    PointF render_offset;
    mutable std::unordered_map<std::string, std::optional<RectF>> rect_cache;
};
} // namespace aurorae
