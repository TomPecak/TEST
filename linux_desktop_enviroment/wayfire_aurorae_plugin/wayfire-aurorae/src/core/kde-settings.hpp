#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace aurorae::kde
{
// Reads KDE's own configuration so that, with default plugin options, windows
// look like they do in a Plasma session on the same machine.

// The title bar font from kdeglobals ([WM] activeFont), as a Pango font
// description.
std::optional<std::string> titlebar_font();

// The icon theme from kdeglobals ([Icons] Theme).
std::optional<std::string> icon_theme();

// The Aurorae theme selected in kwinrc, if the KWin decoration is an Aurorae
// SVG theme.
std::optional<std::string> aurorae_theme();

// Converts a QFont::toString() value ("Noto Sans,10,-1,5,700,0,...") to a
// Pango font description ("Noto Sans Bold 10"). Exposed for testing.
std::optional<std::string> qt_font_to_pango(std::string_view qt_font);
} // namespace aurorae::kde
