#pragma once

#include "geometry.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

typedef struct _cairo_surface cairo_surface_t;

namespace aurorae
{
struct SurfaceDeleter
{
    void operator ()(cairo_surface_t *surface) const;
};

using SurfacePtr = std::unique_ptr<cairo_surface_t, SurfaceDeleter>;

// Creates a transparent premultiplied ARGB32 image surface.
SurfacePtr make_surface(Size size_px);

/** Data a rendering backend attaches to a raster, e.g. its GPU texture. */
struct RasterBackendData
{
    virtual ~RasterBackendData() = default;
};

/**
 * An immutable CPU image (cairo ARGB32, premultiplied) plus an optional
 * backend-owned copy of it. The GPU backend uploads the image once and then
 * drops the CPU copy; the preview tool draws the CPU image directly.
 */
class Raster
{
  public:
    explicit Raster(SurfacePtr surface);

    Size size() const
    {
        return pixel_size;
    }

    // Null after release_cpu_copy().
    cairo_surface_t *surface() const
    {
        return image.get();
    }

    void release_cpu_copy() const
    {
        image.reset();
    }

    RasterBackendData *backend_data() const
    {
        return backend.get();
    }

    void set_backend_data(std::unique_ptr<RasterBackendData> data) const
    {
        backend = std::move(data);
    }

  private:
    Size pixel_size;
    mutable SurfacePtr image;
    mutable std::unique_ptr<RasterBackendData> backend;
};

/** How an element is turned into a raster. */
enum class RasterKind : unsigned char
{
    Plain,      // the element stretched to the raster size
    TileStrip,  // a tile repeated to fill a larger image (see FramePiece)
};

struct RasterKey
{
    const void *source = nullptr; // the SvgDocument (or other owner)
    std::string element;
    Size size;      // final raster size in pixels
    Size tile;      // tile size for TileStrip, unused otherwise
    RasterKind kind = RasterKind::Plain;

    bool operator ==(const RasterKey&) const = default;
};

struct RasterKeyHash
{
    size_t operator ()(const RasterKey& k) const noexcept;
};

/**
 * Memoizes rasters by key. Pointers returned stay valid for the lifetime of
 * the cache, so per-window draw lists can hold them directly.
 *
 * The cache is owned by a theme; all windows using that theme share it, so an
 * element is rasterized once per pixel size no matter how many windows show
 * it. The set of sizes is small (a handful of states times output scales), so
 * there is no eviction.
 */
class RasterCache
{
  public:
    using Factory = std::function<SurfacePtr()>;

    // Returns null if the factory produced no image.
    const Raster *get(const RasterKey& key, const Factory& factory);

    size_t size() const
    {
        return entries.size();
    }

  private:
    std::unordered_map<RasterKey, std::unique_ptr<Raster>, RasterKeyHash> entries;
};
} // namespace aurorae
