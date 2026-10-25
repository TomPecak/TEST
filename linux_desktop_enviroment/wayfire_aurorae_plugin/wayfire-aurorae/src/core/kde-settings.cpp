#include "kde-settings.hpp"
#include "ini-file.hpp"

#include <algorithm>
#include <cstdlib>
#include <pango/pango.h>
#include <vector>

namespace aurorae::kde
{
namespace
{
// $XDG_CONFIG_HOME first, then $XDG_CONFIG_DIRS, like KConfig's cascade.
std::optional<std::string> read_kde_config(const std::string& file, std::string_view group,
    std::string_view key)
{
    const char *home = std::getenv("HOME");
    const char *config_home = std::getenv("XDG_CONFIG_HOME");
    std::vector<std::string> dirs;
    dirs.push_back((config_home && *config_home) ? config_home : std::string(home ? home : "") + "/.config");

    const char *config_dirs = std::getenv("XDG_CONFIG_DIRS");
    std::string rest = (config_dirs && *config_dirs) ? config_dirs : "/etc/xdg";
    size_t start = 0;
    while (start <= rest.size())
    {
        const size_t end = std::min(rest.find(':', start), rest.size());
        if (end > start)
        {
            dirs.push_back(rest.substr(start, end - start));
        }

        start = end + 1;
    }

    for (const auto& dir : dirs)
    {
        if (auto ini = IniFile::load(dir + "/" + file))
        {
            if (auto value = ini->get(group, key))
            {
                return value;
            }
        }
    }

    return std::nullopt;
}

std::vector<std::string_view> split_commas(std::string_view s)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true)
    {
        const size_t pos = s.find(',', start);
        parts.push_back(s.substr(start, pos == std::string_view::npos ? pos : pos - start));
        if (pos == std::string_view::npos)
        {
            return parts;
        }

        start = pos + 1;
    }
}

// Qt 5 used a 0..99 weight scale; map it to CSS/OpenType weights.
int qt5_weight_to_css(int w)
{
    struct
    {
        int qt5, css;
    } constexpr table[] = {
        {0, 100}, {12, 200}, {25, 300}, {50, 400}, {57, 500}, {63, 600}, {75, 700}, {81, 800}, {87, 900},
    };

    int best = 400, best_dist = 1000;
    for (const auto& entry : table)
    {
        const int dist = std::abs(entry.qt5 - w);
        if (dist < best_dist)
        {
            best = entry.css;
            best_dist = dist;
        }
    }

    return best;
}
} // namespace

std::optional<std::string> qt_font_to_pango(std::string_view qt_font)
{
    const auto fields = split_commas(qt_font);
    if ((fields.size() < 2) || fields[0].empty())
    {
        return std::nullopt;
    }

    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, std::string(fields[0]).c_str());

    const double point_size = std::atof(std::string(fields[1]).c_str());
    const int pixel_size    = fields.size() > 2 ? std::atoi(std::string(fields[2]).c_str()) : -1;
    if (point_size > 0)
    {
        pango_font_description_set_size(desc, int(point_size * PANGO_SCALE));
    } else if (pixel_size > 0)
    {
        pango_font_description_set_absolute_size(desc, pixel_size * PANGO_SCALE);
    } else
    {
        pango_font_description_set_size(desc, 10 * PANGO_SCALE);
    }

    if (fields.size() > 4)
    {
        // Qt 6 strings have 16+ fields and use CSS weights directly.
        const int weight = std::atoi(std::string(fields[4]).c_str());
        pango_font_description_set_weight(desc,
            PangoWeight(fields.size() >= 16 ? weight : qt5_weight_to_css(weight)));
    }

    if (fields.size() > 5)
    {
        const int style = std::atoi(std::string(fields[5]).c_str());
        pango_font_description_set_style(desc,
            style == 1 ? PANGO_STYLE_ITALIC : style == 2 ? PANGO_STYLE_OBLIQUE : PANGO_STYLE_NORMAL);
    }

    char *str = pango_font_description_to_string(desc);
    std::string result(str);
    g_free(str);
    pango_font_description_free(desc);
    return result;
}

std::optional<std::string> titlebar_font()
{
    if (auto font = read_kde_config("kdeglobals", "WM", "activeFont"))
    {
        return qt_font_to_pango(*font);
    }

    return std::nullopt;
}

std::optional<std::string> icon_theme()
{
    return read_kde_config("kdeglobals", "Icons", "Theme");
}

std::optional<std::string> aurorae_theme()
{
    constexpr std::string_view prefix = "__aurorae__svg__";
    auto theme = read_kde_config("kwinrc", "org.kde.kdecoration2", "theme");
    if (theme && (theme->rfind(prefix, 0) == 0) && (theme->size() > prefix.size()))
    {
        return theme->substr(prefix.size());
    }

    return std::nullopt;
}
} // namespace aurorae::kde
