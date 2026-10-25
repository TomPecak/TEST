#include "ini-file.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>
#include <vector>

namespace aurorae
{
namespace
{
std::string_view trim(std::string_view s)
{
    auto is_space = [] (char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!s.empty() && is_space(s.front()))
    {
        s.remove_prefix(1);
    }

    while (!s.empty() && is_space(s.back()))
    {
        s.remove_suffix(1);
    }

    return s;
}

std::string to_lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
        [] (unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// KConfig escape sequences that can appear in values.
std::string unescape(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if ((s[i] != '\\') || (i + 1 >= s.size()))
        {
            out += s[i];
            continue;
        }

        switch (s[++i])
        {
          case 's':
            out += ' ';
            break;

          case 't':
            out += '\t';
            break;

          case 'n':
            out += '\n';
            break;

          case 'r':
            out += '\r';
            break;

          default:
            out += s[i];
            break;
        }
    }

    return out;
}

std::vector<std::string_view> split(std::string_view s, char sep)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true)
    {
        const size_t pos = s.find(sep, start);
        parts.push_back(s.substr(start, pos - start));
        if (pos == std::string_view::npos)
        {
            return parts;
        }

        start = pos + 1;
    }
}

std::optional<Color> parse_hex_color(std::string_view hex)
{
    // #rgb, #rrggbb, #aarrggbb (QColor's interpretation)
    auto nibble = [] (char c) -> int
    {
        if ((c >= '0') && (c <= '9'))
        {
            return c - '0';
        }

        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return (c >= 'a') && (c <= 'f') ? 10 + c - 'a' : -1;
    };

    std::vector<int> v;
    for (char c : hex)
    {
        const int n = nibble(c);
        if (n < 0)
        {
            return std::nullopt;
        }

        v.push_back(n);
    }

    auto byte = [&] (size_t i) { return (v[i] * 16 + v[i + 1]) / 255.0; };
    switch (v.size())
    {
      case 3:
        return Color{v[0] / 15.0, v[1] / 15.0, v[2] / 15.0, 1.0};

      case 6:
        return Color{byte(0), byte(2), byte(4), 1.0};

      case 8:
        return Color{byte(2), byte(4), byte(6), byte(0)};

      default:
        return std::nullopt;
    }
}
} // namespace

std::optional<int> parse_int(std::string_view text)
{
    text = trim(text);
    if (!text.empty() && (text.front() == '+'))
    {
        text.remove_prefix(1);
    }

    int value = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if ((ec != std::errc{}) || (ptr != text.data() + text.size()) || text.empty())
    {
        return std::nullopt;
    }

    return value;
}

std::optional<bool> parse_bool(std::string_view text)
{
    const std::string v = to_lower(trim(text));
    if ((v == "true") || (v == "on") || (v == "yes") || (v == "1"))
    {
        return true;
    }

    if ((v == "false") || (v == "off") || (v == "no") || (v == "0"))
    {
        return false;
    }

    return std::nullopt;
}

std::optional<Color> parse_color(std::string_view text)
{
    text = trim(text);
    if (text.empty())
    {
        return std::nullopt;
    }

    if (text.front() == '#')
    {
        return parse_hex_color(text.substr(1));
    }

    const auto parts = split(text, ',');
    if ((parts.size() != 3) && (parts.size() != 4))
    {
        return std::nullopt;
    }

    int c[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < parts.size(); ++i)
    {
        auto v = parse_int(parts[i]);
        if (!v || (*v < 0) || (*v > 255))
        {
            return std::nullopt;
        }

        c[i] = *v;
    }

    return Color{c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, c[3] / 255.0};
}

std::optional<IniFile> IniFile::load(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

IniFile IniFile::parse(std::string_view text)
{
    IniFile ini;
    Group *current = nullptr;

    for (auto raw_line : split(text, '\n'))
    {
        const auto line = trim(raw_line);
        if (line.empty() || (line.front() == '#'))
        {
            continue;
        }

        if (line.front() == '[')
        {
            const size_t end = line.find(']');
            const auto name  = line.substr(1, end == std::string_view::npos ? end : end - 1);
            current = &ini.groups[std::string(name)];
            continue;
        }

        const size_t eq = line.find('=');
        if ((eq == std::string_view::npos) || !current)
        {
            continue;
        }

        const auto key = trim(line.substr(0, eq));
        if (key.empty())
        {
            continue;
        }

        (*current)[std::string(key)] = unescape(trim(line.substr(eq + 1)));
    }

    return ini;
}

std::optional<std::string> IniFile::get(std::string_view group, std::string_view key) const
{
    auto g = groups.find(group);
    if (g == groups.end())
    {
        return std::nullopt;
    }

    auto v = g->second.find(key);
    if (v == g->second.end())
    {
        return std::nullopt;
    }

    return v->second;
}

std::string IniFile::get_string(std::string_view group, std::string_view key, std::string fallback) const
{
    return get(group, key).value_or(std::move(fallback));
}

int IniFile::get_int(std::string_view group, std::string_view key, int fallback) const
{
    auto v = get(group, key);
    return v ? parse_int(*v).value_or(fallback) : fallback;
}

bool IniFile::get_bool(std::string_view group, std::string_view key, bool fallback) const
{
    auto v = get(group, key);
    return v ? parse_bool(*v).value_or(fallback) : fallback;
}

Color IniFile::get_color(std::string_view group, std::string_view key, Color fallback) const
{
    auto v = get(group, key);
    return v ? parse_color(*v).value_or(fallback) : fallback;
}

bool IniFile::has_group(std::string_view group) const
{
    return groups.find(group) != groups.end();
}
} // namespace aurorae
