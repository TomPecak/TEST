#pragma once

#include "raster.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace aurorae
{
/**
 * Window icons for menu buttons the theme draws no SVG for, the way KWin's
 * Aurorae shows the window icon there.
 *
 * Wayland windows only report an app id, so the icon is looked up like a
 * launcher would: the app id's .desktop file (also matched by
 * StartupWMClass) names the icon, which is then searched in the configured
 * icon theme, hicolor and /usr/share/pixmaps. This is a pragmatic subset of
 * the icon theme specification: common directory layouts, no full
 * index.theme parsing.
 *
 * Lookups and rasters are cached; failed lookups too.
 */
class AppIcons
{
  public:
    // Returns the icon rendered at size_px, or null if none is found.
    const Raster *icon(const std::string& app_id, int size_px);

  private:
    std::optional<std::string> icon_name(const std::string& app_id);
    std::optional<std::string> icon_file(const std::string& name, int size_px);
    void index_desktop_files();

    std::unordered_map<std::string, std::optional<std::string>> files; // app id -> icon file
    std::unordered_map<std::string, std::string> wm_class_icons;       // lowercase wm class -> icon name
    bool indexed = false;
    RasterCache rasters;
};
} // namespace aurorae
