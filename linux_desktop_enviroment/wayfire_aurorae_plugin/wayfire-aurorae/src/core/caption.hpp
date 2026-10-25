#pragma once

#include "geometry.hpp"
#include "raster.hpp"
#include "theme-config.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace aurorae
{
struct TextShadow
{
    Color color;
    double offset_x = 0;
    double offset_y = 0;
};

struct CaptionStyle
{
    std::string font; // Pango font description
    Color color;
    HAlign halign = HAlign::Left;
    VAlign valign = VAlign::Center;
    std::optional<TextShadow> shadow;

    bool operator ==(const CaptionStyle& o) const
    {
        return font == o.font && color == o.color && halign == o.halign && valign == o.valign &&
               shadow.has_value() == o.shadow.has_value() &&
               (!shadow || (shadow->color == o.shadow->color && shadow->offset_x == o.shadow->offset_x &&
                   shadow->offset_y == o.shadow->offset_y));
    }
};

/** A rendered title and where it goes inside the caption rectangle. */
struct CaptionImage
{
    SurfacePtr surface; // null if there is nothing to draw
    double x = 0;       // logical offset from the caption rectangle's left
};

// Width of the title in logical pixels, unelided.
double caption_text_width(std::string_view text, const CaptionStyle& style);

/**
 * Renders a window title for a caption rectangle: single line, aligned as
 * the theme says and elided in the middle when too long, like KWin's
 * Aurorae.
 *
 * The image is only as wide as the text. As long as the title fits, it does
 * not depend on the rectangle's width, so resizing a window does not
 * re-render it (callers compare caption_text_width() to the width).
 *
 * @param size Caption rectangle size in logical pixels.
 * @param dpr  Device pixel ratio of the image.
 */
CaptionImage render_caption(std::string_view text, const CaptionStyle& style, SizeF size, double dpr);
} // namespace aurorae
