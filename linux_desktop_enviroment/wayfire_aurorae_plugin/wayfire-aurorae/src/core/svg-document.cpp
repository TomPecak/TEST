#include "svg-document.hpp"

#include <cairo.h>
#include <gio/gio.h>
#include <librsvg/rsvg.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

// How librsvg is made to measure like QtSvg
// ==========================================
//
// KSvg sizes frame pieces by QSvgRenderer::boundsOnElement(), so element
// sizes decide the frame margins and must match exactly. librsvg differs:
//
//  1. (Layer geometry is reported in the document's intrinsic pixels, i.e.
//     with the viewBox scaled to width/height. That matches QtSvg/KSvg, as
//     verified against KWin, so nothing to fix there.)
//  2. Images are missing from ink rectangles, and their logical rectangles
//     ignore the image's own transform. Fix: wrap each <image> in a group
//     carrying its id and transform, and for measuring replace the image by
//     a rect of the same geometry.
//  3. Ink rectangles include filter effect regions and honour clip-path and
//     mask; QtSvg's bounds do not. Fix: measure a copy without them.
//  7. QtSvg stretches an <image> to its width and height; librsvg honours
//     preserveAspectRatio (default xMidYMid meet). Fix: force "none".
//  6. librsvg clips rendering to the document's canvas; QtSvg renders an
//     element wherever it is (themes keep e.g. overlays off the page). Fix:
//     grow the canvas by a wide margin on all sides, keeping its scale.
//  5. librsvg honours shape-rendering:crispEdges, snapping geometry (and so
//     ink rectangles) to whole pixels and drawing without antialiasing.
//     QtSvg ignores the property. Fix: remove it.
//  4. QtSvg draws an element even if an ancestor group is display:none (a
//     hidden Inkscape layer); librsvg draws nothing. Fix: make ancestors of
//     theme elements visible. Elements hidden themselves stay hidden.

namespace aurorae
{
namespace
{
std::string fragment(std::string_view id)
{
    std::string f;
    f.reserve(id.size() + 1);
    f += '#';
    f += id;
    return f;
}

std::optional<RectF> to_rect(const RsvgRectangle& r)
{
    if ((r.width <= 0) || (r.height <= 0))
    {
        return std::nullopt;
    }

    return RectF{r.x, r.y, r.width, r.height};
}

RsvgRectangle viewport_of(RsvgHandle *handle)
{
    double w = 0, h = 0;
    if (!rsvg_handle_get_intrinsic_size_in_pixels(handle, &w, &h) || (w <= 0) || (h <= 0))
    {
        // Without a size, user units are pixels whatever the viewport.
        w = h = 100;
    }

    return {0, 0, w, h};
}

// ---------------------------------------------------------------------------
// File reading

bool read_file(const std::string& path, std::string& out, std::string *error)
{
    gchar *data  = nullptr;
    gsize length = 0;
    GError *err  = nullptr;
    if (!g_file_get_contents(path.c_str(), &data, &length, &err))
    {
        if (error)
        {
            *error = err->message;
        }

        g_error_free(err);
        return false;
    }

    out.assign(data, length);
    g_free(data);
    return true;
}

// .svgz files are gzip streams.
bool gunzip_if_needed(std::string& data)
{
    if ((data.size() < 2) || (uint8_t(data[0]) != 0x1f) || (uint8_t(data[1]) != 0x8b))
    {
        return true;
    }

    GInputStream *raw = g_memory_input_stream_new_from_data(data.data(), gssize(data.size()), nullptr);
    GZlibDecompressor *gz = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP);
    GInputStream *in = g_converter_input_stream_new(raw, G_CONVERTER(gz));
    GOutputStream *out = g_memory_output_stream_new_resizable();
    const gssize n = g_output_stream_splice(out, in,
        GOutputStreamSpliceFlags(G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET),
        nullptr, nullptr);
    std::string result;
    if (n >= 0)
    {
        auto *mem = G_MEMORY_OUTPUT_STREAM(out);
        result.assign(static_cast<const char*>(g_memory_output_stream_get_data(mem)),
            g_memory_output_stream_get_data_size(mem));
    }

    g_object_unref(out);
    g_object_unref(in);
    g_object_unref(gz);
    g_object_unref(raw);
    if (n < 0)
    {
        return false;
    }

    data = std::move(result);
    return true;
}

// ---------------------------------------------------------------------------
// XML helpers

bool is_element(const xmlNode *n, const char *name)
{
    return n->type == XML_ELEMENT_NODE && xmlStrcmp(n->name, BAD_CAST name) == 0;
}

std::optional<std::string> get_attr(xmlNode *n, const char *name)
{
    xmlChar *v = xmlGetProp(n, BAD_CAST name);
    if (!v)
    {
        return std::nullopt;
    }

    std::string result(reinterpret_cast<const char*>(v));
    xmlFree(v);
    return result;
}

void move_attr(xmlNode *from, xmlNode *to, const char *name)
{
    if (auto v = get_attr(from, name))
    {
        xmlSetProp(to, BAD_CAST name, BAD_CAST v->c_str());
        xmlUnsetProp(from, BAD_CAST name);
    }
}

std::string strip_spaces(std::string_view s)
{
    std::string out;
    for (char c : s)
    {
        if (!std::isspace(static_cast<unsigned char>(c)))
        {
            out += c;
        }
    }

    return out;
}

// Removes presentation attributes and style declarations for which
// `drop(property, value)` returns true.
template<class Pred>
void remove_properties(xmlNode *n, std::initializer_list<const char*> properties, Pred drop)
{
    for (const char *property : properties)
    {
        if (auto v = get_attr(n, property); v && drop(property, strip_spaces(*v)))
        {
            xmlUnsetProp(n, BAD_CAST property);
        }
    }

    auto style = get_attr(n, "style");
    if (!style)
    {
        return;
    }

    std::string kept;
    size_t start = 0;
    while (start <= style->size())
    {
        const size_t end  = std::min(style->find(';', start), style->size());
        const auto decl   = style->substr(start, end - start);
        const auto colon  = decl.find(':');
        const auto prop   = strip_spaces(decl.substr(0, colon));
        const auto value  = colon == std::string::npos ? "" : strip_spaces(decl.substr(colon + 1));
        bool remove = false;
        for (const char *property : properties)
        {
            remove |= prop == property && drop(property, value);
        }

        if (!remove && !strip_spaces(decl).empty())
        {
            kept += decl;
            kept += ';';
        }

        start = end + 1;
    }

    xmlSetProp(n, BAD_CAST "style", BAD_CAST kept.c_str());
}

// Ids KSvg looks up in decoration and button SVGs.
bool is_theme_element_id(std::string_view id)
{
    if (id.starts_with("hint-"))
    {
        return true;
    }

    static constexpr std::array<std::string_view, 9> pieces = {
        "center", "top", "bottom", "left", "right", "topleft", "topright", "bottomleft", "bottomright",
    };
    for (auto piece : pieces)
    {
        if ((id == piece) || (id.ends_with(piece) && (id.size() > piece.size()) &&
                              (id[id.size() - piece.size() - 1] == '-')))
        {
            return true;
        }
    }

    return false;
}

void collect(xmlNode *n, std::vector<xmlNode*>& all)
{
    for (xmlNode *c = n->children; c; c = c->next)
    {
        if (c->type == XML_ELEMENT_NODE)
        {
            all.push_back(c);
            collect(c, all);
        }
    }
}

std::string dump(xmlDoc *doc)
{
    xmlChar *buffer = nullptr;
    int size = 0;
    xmlDocDumpMemory(doc, &buffer, &size);
    std::string xml(reinterpret_cast<const char*>(buffer), size_t(size));
    xmlFree(buffer);
    return xml;
}

// A length in px; QtSvg converts physical units at 90 DPI.
std::optional<double> parse_length(const std::optional<std::string>& text)
{
    if (!text)
    {
        return std::nullopt;
    }

    char *end = nullptr;
    const double value = std::strtod(text->c_str(), &end);
    if (end == text->c_str())
    {
        return std::nullopt;
    }

    const std::string unit = strip_spaces(end);
    const double factor = unit.empty() || unit == "px" ? 1.0 :
        unit == "pt" ? 90.0 / 72.0 : unit == "pc" ? 15.0 : unit == "in" ? 90.0 :
        unit == "cm" ? 90.0 / 2.54 : unit == "mm" ? 9.0 / 2.54 : 0.0;
    if (factor == 0.0)
    {
        return std::nullopt; // percentages, em: leave the document alone
    }

    return value * factor;
}

struct Canvas
{
    double vx = 0, vy = 0, vw = 0, vh = 0; // viewBox (user units)
    double kx = 1, ky = 1;                 // pixels per user unit
};

// The root's coordinate system, if it has a definite pixel size.
std::optional<Canvas> read_canvas(xmlNode *root)
{
    Canvas c;
    bool has_viewbox = false;
    if (auto viewbox = get_attr(root, "viewBox"))
    {
        std::string vb = *viewbox;
        std::replace(vb.begin(), vb.end(), ',', ' ');
        has_viewbox = (std::sscanf(vb.c_str(), "%lf %lf %lf %lf", &c.vx, &c.vy, &c.vw, &c.vh) == 4) &&
            (c.vw > 0) && (c.vh > 0);
    }

    const auto width  = parse_length(get_attr(root, "width"));
    const auto height = parse_length(get_attr(root, "height"));
    if (!has_viewbox)
    {
        if (!width || !height)
        {
            return std::nullopt;
        }

        // Without a viewBox, one user unit is one pixel.
        c.vw = *width;
        c.vh = *height;
    }

    c.kx = width ? *width / c.vw : 1.0;
    c.ky = height ? *height / c.vh : 1.0;
    return c;
}

// Makes the canvas cover `area` (pixels of the original canvas) at the same
// scale. Returns the pixel offset of the original origin in the new canvas.
PointF grow_canvas(xmlNode *root, const Canvas& c, RectF area)
{
    // Whole pixels, so elements keep their sub-pixel position.
    const RectF original{0, 0, c.vw * c.kx, c.vh * c.ky};
    area = area.united(original);
    const double left = std::floor(area.x), top = std::floor(area.y);
    area = {left, top, std::ceil(area.right()) - left, std::ceil(area.bottom()) - top};
    const std::string viewbox = std::to_string(c.vx + area.x / c.kx) + " " +
        std::to_string(c.vy + area.y / c.ky) + " " + std::to_string(area.width / c.kx) + " " +
        std::to_string(area.height / c.ky);
    xmlSetProp(root, BAD_CAST "viewBox", BAD_CAST viewbox.c_str());
    xmlSetProp(root, BAD_CAST "width", BAD_CAST std::to_string(area.width).c_str());
    xmlSetProp(root, BAD_CAST "height", BAD_CAST std::to_string(area.height).c_str());
    xmlSetProp(root, BAD_CAST "preserveAspectRatio", BAD_CAST "none");
    return {-area.x, -area.y};
}

using XmlDoc = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>;

// Fixes 2 (wrapping), 4 and 5, which both documents share.
XmlDoc parse_and_normalize(std::string data)
{
    XmlDoc doc{nullptr, &xmlFreeDoc};
    if (!gunzip_if_needed(data))
    {
        return doc;
    }

    // No network access and no entity substitution; allow the large
    // attribute values of embedded images.
    doc.reset(xmlReadMemory(data.data(), int(data.size()), nullptr, nullptr,
        XML_PARSE_NONET | XML_PARSE_HUGE | XML_PARSE_NOERROR | XML_PARSE_NOWARNING));
    xmlNode *root = doc ? xmlDocGetRootElement(doc.get()) : nullptr;
    if (!root)
    {
        doc.reset();
        return doc;
    }

    std::vector<xmlNode*> elements;
    collect(root, elements);
    auto always = [] (auto, const std::string&) { return true; };

    for (xmlNode *n : elements)
    {
        // 4. Unhide ancestors of theme elements.
        if (auto id = get_attr(n, "id"); id && is_theme_element_id(*id))
        {
            for (xmlNode *p = n->parent; p && (p != root) && (p->type == XML_ELEMENT_NODE); p = p->parent)
            {
                remove_properties(p, {"display"}, [] (auto, const std::string& v) { return v == "none"; });
            }
        }

        // 5. Antialias like QtSvg.
        remove_properties(n, {"shape-rendering"}, always);

        // 2. <image id transform/> -> <g id transform><image/></g>
        if (is_element(n, "image"))
        {
            // 7. Stretch like QtSvg.
            xmlSetProp(n, BAD_CAST "preserveAspectRatio", BAD_CAST "none");

            xmlNode *g = xmlNewNode(n->ns, BAD_CAST "g");
            move_attr(n, g, "id");
            move_attr(n, g, "transform");
            xmlReplaceNode(n, g);
            xmlAddChild(g, n);
        }
    }

    remove_properties(root, {"shape-rendering"}, always);
    return doc;
}

// Fixes 2 (images as rects) and 3, for the measuring copy.
void strip_for_measuring(xmlNode *root)
{
    std::vector<xmlNode*> elements;
    collect(root, elements);
    for (xmlNode *n : elements)
    {
        remove_properties(n, {"filter", "clip-path", "mask"}, [] (auto, const std::string&) { return true; });
        if (is_element(n, "image"))
        {
            xmlNode *rect = xmlNewNode(n->ns, BAD_CAST "rect");
            for (const char *attr : {"x", "y", "width", "height"})
            {
                if (auto v = get_attr(n, attr))
                {
                    xmlSetProp(rect, BAD_CAST attr, BAD_CAST v->c_str());
                }
            }

            xmlReplaceNode(n, rect);
            xmlFreeNode(n);
        }
    }
}

RsvgHandle *make_handle(const std::string& bytes, const std::string& path, std::string *error)
{
    GInputStream *stream = g_memory_input_stream_new_from_data(bytes.data(), gssize(bytes.size()), nullptr);
    GFile *base = g_file_new_for_path(path.c_str()); // resolves relative image hrefs
    GError *err = nullptr;
    RsvgHandle *handle = rsvg_handle_new_from_stream_sync(stream, base, RSVG_HANDLE_FLAG_UNLIMITED,
        nullptr, &err);
    g_object_unref(base);
    g_object_unref(stream);
    if (!handle)
    {
        if (error)
        {
            *error = err ? err->message : "unknown error";
        }

        g_clear_error(&err);
        return nullptr;
    }

    // Qt resolves physical units at 90 DPI.
    rsvg_handle_set_dpi(handle, 90.0);
    return handle;
}
} // namespace

std::shared_ptr<SvgDocument> SvgDocument::load(const std::string& path, std::string *error)
{
    std::string data;
    if (!read_file(path, data, error))
    {
        return nullptr;
    }

    XmlDoc doc = parse_and_normalize(data);
    if (!doc)
    {
        // Not XML we understand; librsvg may still cope with the raw bytes.
        RsvgHandle *handle = make_handle(data, path, error);
        if (!handle)
        {
            return nullptr;
        }

        return std::shared_ptr<SvgDocument>(new SvgDocument(handle, g_object_ref(handle), path, {0, 0}));
    }

    // Measuring copy first: it tells how far the content reaches.
    XmlDoc measure_doc{xmlCopyDoc(doc.get(), 1), &xmlFreeDoc};
    strip_for_measuring(xmlDocGetRootElement(measure_doc.get()));
    RsvgHandle *measure = make_handle(dump(measure_doc.get()), path, error);
    if (!measure)
    {
        return nullptr;
    }

    // 6. Grow the rendering copy's canvas over all content, with room for
    // filter effects, which the measuring copy leaves out.
    PointF offset{0, 0};
    xmlNode *root = xmlDocGetRootElement(doc.get());
    if (auto canvas = read_canvas(root))
    {
        const RsvgRectangle viewport = viewport_of(measure);
        RsvgRectangle ink{}, logical{};
        if (rsvg_handle_get_geometry_for_layer(measure, nullptr, &viewport, &ink, &logical, nullptr))
        {
            const double pad = 64 + 0.25 * std::max(ink.width, ink.height);
            offset = grow_canvas(root, *canvas, {ink.x - pad, ink.y - pad, ink.width + 2 * pad,
                ink.height + 2 * pad});
        }
    }

    RsvgHandle *render = make_handle(dump(doc.get()), path, error);
    if (!render)
    {
        g_object_unref(measure);
        return nullptr;
    }

    return std::shared_ptr<SvgDocument>(new SvgDocument(render, measure, path, offset));
}

SvgDocument::SvgDocument(RsvgHandle *render, RsvgHandle *measure, std::string path, PointF offset) :
    render_handle(render), measure_handle(measure), file_path(std::move(path)), render_offset(offset)
{}

SvgDocument::~SvgDocument()
{
    g_object_unref(render_handle);
    g_object_unref(measure_handle);
}

std::optional<RectF> SvgDocument::element_rect(std::string_view id) const
{
    std::string key(id);
    if (auto it = rect_cache.find(key); it != rect_cache.end())
    {
        return it->second;
    }

    std::optional<RectF> result;
    const std::string frag = fragment(id);
    if (rsvg_handle_has_sub(measure_handle, frag.c_str()))
    {
        const RsvgRectangle viewport = viewport_of(measure_handle);
        RsvgRectangle ink{}, logical{};
        if (rsvg_handle_get_geometry_for_layer(measure_handle, frag.c_str(), &viewport, &ink, &logical,
            nullptr))
        {
            result = to_rect(ink);
        }

        if (!result && rsvg_handle_get_geometry_for_element(measure_handle, frag.c_str(), &ink, &logical,
            nullptr))
        {
            // Hidden elements (often hint-* markers) still have bounds in Qt.
            result = to_rect(ink);
        }
    }

    rect_cache.emplace(std::move(key), result);
    return result;
}

bool SvgDocument::has_element(std::string_view id) const
{
    return element_rect(id).has_value();
}

SizeF SvgDocument::element_size(std::string_view id) const
{
    auto r = element_rect(id);
    if (!r)
    {
        return {};
    }

    return {std::round(r->width), std::round(r->height)};
}

SurfacePtr SvgDocument::render_element(std::string_view id, Size size_px) const
{
    const auto rect = element_rect(id);
    if (size_px.empty() || !rect)
    {
        return nullptr;
    }

    // Draw the element in place in the document, transformed so that its
    // rect fills the image (QSvgRenderer::render(painter, id, bounds)).
    auto surface = make_surface(size_px);
    cairo_t *cr  = cairo_create(surface.get());
    cairo_scale(cr, size_px.width / rect->width, size_px.height / rect->height);
    cairo_translate(cr, -(rect->x + render_offset.x), -(rect->y + render_offset.y));

    const RsvgRectangle viewport = viewport_of(render_handle);
    const std::string frag = fragment(id);
    const bool ok = rsvg_handle_render_layer(render_handle, cr, frag.c_str(), &viewport, nullptr);
    cairo_destroy(cr);
    return ok ? std::move(surface) : nullptr;
}
} // namespace aurorae
