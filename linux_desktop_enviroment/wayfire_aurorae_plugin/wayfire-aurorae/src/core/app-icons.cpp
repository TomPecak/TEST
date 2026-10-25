#include "app-icons.hpp"
#include "ini-file.hpp"
#include "kde-settings.hpp"

#include <algorithm>
#include <cairo.h>
#include <cstdlib>
#include <filesystem>
#include <librsvg/rsvg.h>
#include <vector>

namespace fs = std::filesystem;

namespace aurorae
{
namespace
{
std::string lowercase(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [] (unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> data_dirs()
{
    const char *home = std::getenv("HOME");
    const char *data_home = std::getenv("XDG_DATA_HOME");
    std::vector<std::string> dirs{(data_home && *data_home) ? data_home :
        std::string(home ? home : "") + "/.local/share"};

    const char *env = std::getenv("XDG_DATA_DIRS");
    const std::string list = (env && *env) ? env : "/usr/local/share:/usr/share";
    size_t start = 0;
    while (start <= list.size())
    {
        const size_t end = std::min(list.find(':', start), list.size());
        if (end > start)
        {
            dirs.push_back(list.substr(start, end - start));
        }

        start = end + 1;
    }

    return dirs;
}

bool is_file(const fs::path& p)
{
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

std::optional<std::string> desktop_icon(const fs::path& file)
{
    auto ini = IniFile::load(file.string());
    if (!ini)
    {
        return std::nullopt;
    }

    auto icon = ini->get("Desktop Entry", "Icon");
    if (!icon || icon->empty())
    {
        return std::nullopt;
    }

    return icon;
}

SurfacePtr scaled_to(cairo_surface_t *source, int size)
{
    const int w = cairo_image_surface_get_width(source);
    const int h = cairo_image_surface_get_height(source);
    if ((w <= 0) || (h <= 0))
    {
        return nullptr;
    }

    auto out = make_surface({size, size});
    cairo_t *cr = cairo_create(out.get());
    const double scale = std::min(double(size) / w, double(size) / h);
    cairo_translate(cr, (size - w * scale) / 2.0, (size - h * scale) / 2.0);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, source, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);
    return out;
}

SurfacePtr load_icon(const std::string& path, int size)
{
    if (path.ends_with(".png"))
    {
        SurfacePtr png{cairo_image_surface_create_from_png(path.c_str())};
        if (cairo_surface_status(png.get()) != CAIRO_STATUS_SUCCESS)
        {
            return nullptr;
        }

        return scaled_to(png.get(), size);
    }

    RsvgHandle *handle = rsvg_handle_new_from_file(path.c_str(), nullptr);
    if (!handle)
    {
        return nullptr;
    }

    auto out = make_surface({size, size});
    cairo_t *cr = cairo_create(out.get());
    const RsvgRectangle viewport{0, 0, double(size), double(size)};
    const bool ok = rsvg_handle_render_document(handle, cr, &viewport, nullptr);
    cairo_destroy(cr);
    g_object_unref(handle);
    return ok ? std::move(out) : nullptr;
}
} // namespace

void AppIcons::index_desktop_files()
{
    indexed = true;
    std::error_code ec;
    for (const auto& dir : data_dirs())
    {
        for (const auto& entry : fs::directory_iterator(fs::path(dir) / "applications", ec))
        {
            if (entry.path().extension() != ".desktop")
            {
                continue;
            }

            auto ini = IniFile::load(entry.path().string());
            if (!ini)
            {
                continue;
            }

            const auto icon = ini->get("Desktop Entry", "Icon");
            if (!icon || icon->empty())
            {
                continue;
            }

            // Later directories have lower priority: keep the first match.
            if (auto wm_class = ini->get("Desktop Entry", "StartupWMClass"))
            {
                wm_class_icons.emplace(lowercase(*wm_class), *icon);
            }

            // "org.kde.dolphin.desktop" also answers to app id "dolphin".
            std::string stem = entry.path().stem().string();
            wm_class_icons.emplace(lowercase(stem), *icon);
            if (const auto dot = stem.rfind('.'); dot != std::string::npos)
            {
                wm_class_icons.emplace(lowercase(stem.substr(dot + 1)), *icon);
            }
        }
    }
}

std::optional<std::string> AppIcons::icon_name(const std::string& app_id)
{
    for (const auto& candidate : {app_id, lowercase(app_id)})
    {
        for (const auto& dir : data_dirs())
        {
            const fs::path file = fs::path(dir) / "applications" / (candidate + ".desktop");
            if (is_file(file))
            {
                if (auto icon = desktop_icon(file))
                {
                    return icon;
                }
            }
        }
    }

    if (!indexed)
    {
        index_desktop_files();
    }

    if (auto it = wm_class_icons.find(lowercase(app_id)); it != wm_class_icons.end())
    {
        return it->second;
    }

    // Many applications name their icon after their app id.
    return app_id;
}

std::optional<std::string> AppIcons::icon_file(const std::string& name, int size_px)
{
    if (name.starts_with("/"))
    {
        return is_file(name) ? std::optional(name) : std::nullopt;
    }

    const auto dirs = data_dirs();
    std::vector<fs::path> bases;
    for (const auto& dir : dirs)
    {
        bases.push_back(fs::path(dir) / "icons");
    }

    // Configured theme, the themes it inherits, then hicolor.
    std::vector<std::string> themes{kde::icon_theme().value_or("breeze")};
    for (const auto& base : bases)
    {
        if (auto index = IniFile::load((base / themes.front() / "index.theme").string()))
        {
            const std::string inherits = index->get_string("Icon Theme", "Inherits", "");
            size_t start = 0;
            while (start < inherits.size())
            {
                const size_t end = std::min(inherits.find(',', start), inherits.size());
                themes.push_back(inherits.substr(start, end - start));
                start = end + 1;
            }

            break;
        }
    }

    themes.push_back("hicolor");

    std::vector<int> sizes{size_px, 48, 64, 32, 128, 256, 24, 22, 16};
    for (const auto& theme : themes)
    {
        for (const auto& base : bases)
        {
            const fs::path root = base / theme;
            std::error_code ec;
            if (!fs::is_directory(root, ec))
            {
                continue;
            }

            for (int s : sizes)
            {
                const std::string dim = std::to_string(s);
                for (const fs::path& dir : {root / (dim + "x" + dim) / "apps", root / "apps" / dim})
                {
                    for (const char *ext : {".svg", ".png"})
                    {
                        if (is_file(dir / (name + ext)))
                        {
                            return (dir / (name + ext)).string();
                        }
                    }
                }
            }

            for (const fs::path& dir : {root / "scalable" / "apps", root / "apps" / "scalable"})
            {
                if (is_file(dir / (name + ".svg")))
                {
                    return (dir / (name + ".svg")).string();
                }
            }
        }
    }

    for (const auto& dir : dirs)
    {
        for (const char *ext : {".png", ".svg"})
        {
            const fs::path p = fs::path(dir) / "pixmaps" / (name + ext);
            if (is_file(p))
            {
                return p.string();
            }
        }
    }

    return std::nullopt;
}

const Raster *AppIcons::icon(const std::string& app_id, int size_px)
{
    if (app_id.empty() || (size_px <= 0))
    {
        return nullptr;
    }

    auto it = files.find(app_id);
    if (it == files.end())
    {
        std::optional<std::string> file;
        if (auto name = icon_name(app_id))
        {
            file = icon_file(*name, size_px);
        }

        it = files.emplace(app_id, file).first;
    }

    if (!it->second)
    {
        return nullptr;
    }

    const std::string& path = *it->second;
    return rasters.get({this, path, {size_px, size_px}, {}, RasterKind::Plain},
        [&] { return load_icon(path, size_px); });
}
} // namespace aurorae
