#pragma once

#include "geometry.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace aurorae
{
/**
 * Reader for the KConfig flavour of INI files used by Aurorae themes
 * (<theme>rc, metadata.desktop) and by KDE settings (kdeglobals, kwinrc).
 *
 * Mirrors the KConfig behaviour that themes rely on: whitespace around keys
 * and values is trimmed, '#' starts a comment, the last duplicate key wins,
 * localized keys ("Name[pl]") are kept as separate keys and the typed getters
 * fall back to the default when a value cannot be converted.
 */
class IniFile
{
  public:
    static std::optional<IniFile> load(const std::string& path);
    static IniFile parse(std::string_view text);

    std::optional<std::string> get(std::string_view group, std::string_view key) const;

    std::string get_string(std::string_view group, std::string_view key, std::string fallback) const;
    int get_int(std::string_view group, std::string_view key, int fallback) const;
    bool get_bool(std::string_view group, std::string_view key, bool fallback) const;
    Color get_color(std::string_view group, std::string_view key, Color fallback) const;

    bool has_group(std::string_view group) const;

  private:
    using Group = std::map<std::string, std::string, std::less<>>;
    std::map<std::string, Group, std::less<>> groups;
};

// Conversions shared with the KDE settings readers. Exposed for testing.
std::optional<int> parse_int(std::string_view text);
std::optional<bool> parse_bool(std::string_view text);
std::optional<Color> parse_color(std::string_view text);
} // namespace aurorae
