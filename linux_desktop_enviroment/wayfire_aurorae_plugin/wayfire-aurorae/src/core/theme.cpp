#include "theme.hpp"
#include "ini-file.hpp"
#include "svg-document.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace aurorae
{
namespace theme_locator
{
namespace
{
std::string env_or(const char *name, const std::string& fallback)
{
    const char *v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

std::optional<std::string> svg_in(const fs::path& dir, std::string_view stem)
{
    std::error_code ec;
    for (const char *ext : {".svg", ".svgz"})
    {
        auto p = dir / (std::string(stem) + ext);
        if (fs::is_regular_file(p, ec))
        {
            return p.string();
        }
    }

    return std::nullopt;
}
} // namespace

std::vector<std::string> search_dirs()
{
    const std::string home = env_or("HOME", "");
    std::vector<std::string> dirs;
    dirs.push_back(env_or("XDG_DATA_HOME", home + "/.local/share") + "/aurorae/themes");

    const std::string data_dirs = env_or("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
    size_t start = 0;
    while (start <= data_dirs.size())
    {
        const size_t end = std::min(data_dirs.find(':', start), data_dirs.size());
        if (end > start)
        {
            dirs.push_back(data_dirs.substr(start, end - start) + "/aurorae/themes");
        }

        start = end + 1;
    }

    return dirs;
}

std::optional<std::string> find(const std::string& name_or_path)
{
    if (name_or_path.empty())
    {
        return std::nullopt;
    }

    if (name_or_path.find('/') != std::string::npos)
    {
        if (svg_in(name_or_path, "decoration"))
        {
            return fs::path(name_or_path).lexically_normal().string();
        }

        return std::nullopt;
    }

    for (const auto& dir : search_dirs())
    {
        const fs::path candidate = fs::path(dir) / name_or_path;
        if (svg_in(candidate, "decoration"))
        {
            return candidate.string();
        }
    }

    return std::nullopt;
}

std::vector<std::string> list()
{
    std::set<std::string> names;
    std::error_code ec;
    for (const auto& dir : search_dirs())
    {
        for (const auto& entry : fs::directory_iterator(dir, ec))
        {
            if (entry.is_directory(ec) && svg_in(entry.path(), "decoration"))
            {
                names.insert(entry.path().filename().string());
            }
        }
    }

    return {names.begin(), names.end()};
}
} // namespace theme_locator

std::shared_ptr<Theme> Theme::load(const std::string& name_or_path, std::string *error)
{
    auto fail = [&] (std::string msg) -> std::shared_ptr<Theme>
    {
        if (error)
        {
            *error = std::move(msg);
        }

        return nullptr;
    };

    const auto dir = theme_locator::find(name_or_path);
    if (!dir)
    {
        return fail("theme '" + name_or_path + "' not found (searched XDG data dirs for aurorae/themes)");
    }

    std::shared_ptr<Theme> theme(new Theme());
    theme->theme_dir  = *dir;
    theme->theme_name = fs::path(*dir).filename().string();

    const fs::path root(*dir);
    std::string svg_error;
    const auto decoration_path = theme_locator::svg_in(root, "decoration");
    theme->decoration_svg = SvgDocument::load(*decoration_path, &svg_error);
    if (!theme->decoration_svg)
    {
        return fail("cannot load " + *decoration_path + ": " + svg_error);
    }

    // A theme without an rc file uses the defaults, like KConfig would.
    const auto rc = IniFile::load((root / (theme->theme_name + "rc")).string());
    theme->cfg = ThemeConfig::from_ini(rc.value_or(IniFile{}));

    // Decoration frames and their fallbacks (aurorae/v2/decoration.cpp)
    const auto& svg = theme->decoration_svg;
    auto pick = [&] (const char *preferred, const char *fallback)
    {
        return FrameSvg(svg, FrameSvg::has_prefix(*svg, preferred) ? preferred : fallback);
    };

    theme->frame_active   = FrameSvg(svg, "decoration");
    theme->frame_inactive = pick("decoration-inactive", "decoration");
    theme->frame_maximized = pick("decoration-maximized", "decoration");
    theme->frame_maximized_inactive = FrameSvg::has_prefix(*svg, "decoration-maximized-inactive") ?
        FrameSvg(svg, "decoration-maximized-inactive") : *theme->frame_maximized;

    if (!theme->frame_active->drawable())
    {
        return fail(*decoration_path + " has no decoration-center (or center) element");
    }

    if (FrameSvg::has_prefix(*svg, "innerborder"))
    {
        theme->inner_active = FrameSvg(svg, "innerborder");
    }

    if (FrameSvg::has_prefix(*svg, "innerborder-inactive"))
    {
        theme->inner_inactive = FrameSvg(svg, "innerborder-inactive");
    }

    // Buttons. A missing SVG means the theme has no such button.
    auto load_set = [&] (std::string_view stem) -> std::optional<ButtonFrameSet>
    {
        const auto path = theme_locator::svg_in(root, stem);
        if (!path)
        {
            return std::nullopt;
        }

        auto doc = SvgDocument::load(*path);
        if (!doc)
        {
            return std::nullopt;
        }

        return ButtonFrameSet(std::move(doc));
    };

    for (size_t i = 0; i < theme->buttons.size(); ++i)
    {
        const auto type = ButtonType(i);
        const auto stem = button_file_stem(type);
        if (stem.empty())
        {
            continue;
        }

        if (auto set = load_set(stem))
        {
            theme->buttons[i] = ButtonAsset{std::move(*set), std::nullopt};
        }
    }

    if (auto& max = theme->buttons[size_t(ButtonType::Maximize)])
    {
        max->restore = load_set("restore");
    }

    return theme;
}

const FrameSvg& Theme::decoration(bool active, bool maximized) const
{
    if (maximized)
    {
        return active ? *frame_maximized : *frame_maximized_inactive;
    }

    return active ? *frame_active : *frame_inactive;
}

const FrameSvg *Theme::inner_border(bool active) const
{
    const auto& frame = active ? inner_active : inner_inactive;
    return frame ? &*frame : nullptr;
}

const ButtonAsset *Theme::button(ButtonType type) const
{
    const auto& asset = buttons[size_t(type)];
    return asset ? &*asset : nullptr;
}

SizeF Theme::button_size(ButtonType type) const
{
    const auto& c = cfg;
    double width = c.button_width;
    switch (type)
    {
      case ButtonType::Menu:
        width = c.button_width_menu;
        break;

      case ButtonType::ApplicationMenu:
        width = c.button_width_app_menu;
        break;

      case ButtonType::OnAllDesktops:
        width = c.button_width_all_desktops;
        break;

      case ButtonType::Minimize:
        width = c.button_width_minimize;
        break;

      case ButtonType::Maximize:
        width = c.button_width_maximize_restore;
        break;

      case ButtonType::Close:
        width = c.button_width_close;
        break;

      case ButtonType::ContextHelp:
        width = c.button_width_help;
        break;

      case ButtonType::Shade:
        width = c.button_width_shade;
        break;

      case ButtonType::KeepBelow:
        width = c.button_width_keep_below;
        break;

      case ButtonType::KeepAbove:
        width = c.button_width_keep_above;
        break;

      case ButtonType::Spacer:
        width = c.explicit_button_spacer;
        break;
    }

    return {width, c.button_height};
}

double Theme::title_height(double button_factor) const
{
    return std::max(cfg.title_height, cfg.button_height * button_factor + cfg.button_margin_top);
}

// DecorationTheme::borders(). Only the top position is supported: KWin's own
// button and caption layout assumes it, other positions are broken there too.
Margins Theme::borders(BorderSize size, double button_factor) const
{
    double min_margin = 0, max_margin = 0;
    switch (size)
    {
      case BorderSize::None:
        break;

      case BorderSize::NoSides:
      case BorderSize::Tiny:
        min_margin = 1;
        max_margin = 4;
        break;

      case BorderSize::Normal:
        min_margin = 4;
        max_margin = 6;
        break;

      case BorderSize::Large:
        min_margin = 6;
        max_margin = 8;
        break;

      case BorderSize::VeryLarge:
        min_margin = 8;
        max_margin = 12;
        break;

      case BorderSize::Huge:
        min_margin = 12;
        max_margin = 20;
        break;

      case BorderSize::VeryHuge:
        min_margin = 23;
        max_margin = 30;
        break;

      case BorderSize::Oversized:
        min_margin = 36;
        max_margin = 48;
        break;

      case BorderSize::Theme:
        min_margin = 0;
        max_margin = 1e9;
        break;
    }

    Margins m{
        std::clamp(cfg.border_left, min_margin, max_margin),
        title_height(button_factor) + cfg.title_edges.top + cfg.title_edges.bottom,
        std::clamp(cfg.border_right, min_margin, max_margin),
        std::clamp(cfg.border_bottom, min_margin, max_margin),
    };

    if (size == BorderSize::None)
    {
        m.left = m.right = m.bottom = 0;
    } else if (size == BorderSize::NoSides)
    {
        m.left = m.right = 0;
    }

    return m;
}

Margins Theme::maximized_borders(double button_factor) const
{
    const double title = title_height(button_factor) + cfg.title_edges_maximized.top +
        cfg.title_edges_maximized.bottom;
    return {0, title, 0, 0};
}
} // namespace aurorae
