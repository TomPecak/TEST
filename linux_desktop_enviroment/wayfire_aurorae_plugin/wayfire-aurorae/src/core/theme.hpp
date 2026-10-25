#pragma once

#include "buttons.hpp"
#include "frame-svg.hpp"
#include "raster.hpp"
#include "settings.hpp"
#include "theme-config.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aurorae
{
class SvgDocument;

/** Theme lookup in the same places KWin searches. */
namespace theme_locator
{
// $XDG_DATA_HOME/aurorae/themes followed by $XDG_DATA_DIRS/*/aurorae/themes.
std::vector<std::string> search_dirs();

// Resolves a theme name, or an absolute path to a theme directory, to the
// directory holding its decoration.svg(z). Returns nullopt if not found.
std::optional<std::string> find(const std::string& name_or_path);

// Names of all installed themes, sorted, without duplicates.
std::vector<std::string> list();
} // namespace theme_locator

/** The SVGs of one button type: the normal set and, for maximize, restore. */
struct ButtonAsset
{
    ButtonFrameSet normal;
    std::optional<ButtonFrameSet> restore;
};

/**
 * A loaded Aurorae theme: configuration, parsed SVGs, the frames for every
 * decoration state, and the raster cache shared by all windows using it.
 *
 * Metrics follow aurorae/v2/decorationtheme.cpp, the SVG theme engine of
 * current KWin.
 */
class Theme
{
  public:
    static std::shared_ptr<Theme> load(const std::string& name_or_path, std::string *error = nullptr);

    const std::string& name() const
    {
        return theme_name;
    }

    const std::string& directory() const
    {
        return theme_dir;
    }

    const ThemeConfig& config() const
    {
        return cfg;
    }

    const FrameSvg& decoration(bool active, bool maximized) const;

    // Null if the theme has no inner border for this state.
    const FrameSvg *inner_border(bool active) const;

    // Null if the theme has no SVG for the button.
    const ButtonAsset *button(ButtonType type) const;

    Margins borders(BorderSize size, double button_factor) const;
    Margins maximized_borders(double button_factor) const;

    // Unscaled button size from the theme; multiply by the button factor.
    SizeF button_size(ButtonType type) const;

    RasterCache& rasters() const
    {
        return raster_cache;
    }

  private:
    Theme() = default;

    double title_height(double button_factor) const;

    std::string theme_name;
    std::string theme_dir;
    ThemeConfig cfg;

    std::shared_ptr<SvgDocument> decoration_svg;
    std::optional<FrameSvg> frame_active, frame_inactive, frame_maximized, frame_maximized_inactive;
    std::optional<FrameSvg> inner_active, inner_inactive;
    std::array<std::optional<ButtonAsset>, size_t(ButtonType::Spacer) + 1> buttons;

    mutable RasterCache raster_cache;
};
} // namespace aurorae
