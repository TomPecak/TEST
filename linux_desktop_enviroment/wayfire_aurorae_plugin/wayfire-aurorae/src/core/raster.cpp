#include "raster.hpp"

#include <cairo.h>

namespace aurorae
{
void SurfaceDeleter::operator ()(cairo_surface_t *surface) const
{
    cairo_surface_destroy(surface);
}

SurfacePtr make_surface(Size size_px)
{
    // A fresh cairo image surface is zero-initialized, i.e. transparent.
    return SurfacePtr{cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
        std::max(size_px.width, 0), std::max(size_px.height, 0))};
}

Raster::Raster(SurfacePtr surface) : image(std::move(surface))
{
    cairo_surface_flush(image.get());
    pixel_size = {
        cairo_image_surface_get_width(image.get()),
        cairo_image_surface_get_height(image.get()),
    };
}

size_t RasterKeyHash::operator ()(const RasterKey& k) const noexcept
{
    size_t h = std::hash<const void*>{}(k.source);
    auto mix = [&h] (size_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
    mix(std::hash<std::string>{}(k.element));
    mix(size_t(k.size.width) << 32 | unsigned(k.size.height));
    mix(size_t(k.tile.width) << 32 | unsigned(k.tile.height));
    mix(size_t(k.kind));
    return h;
}

const Raster *RasterCache::get(const RasterKey& key, const Factory& factory)
{
    auto it = entries.find(key);
    if (it != entries.end())
    {
        return it->second.get();
    }

    auto surface = factory();
    if (!surface || (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS))
    {
        // Remember the failure so we do not retry on every frame.
        entries.emplace(key, nullptr);
        return nullptr;
    }

    auto raster = std::make_unique<Raster>(std::move(surface));
    auto *ptr   = raster.get();
    entries.emplace(key, std::move(raster));
    return ptr;
}
} // namespace aurorae
