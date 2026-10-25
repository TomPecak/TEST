#pragma once

#include "caption.hpp"
#include "decoration-layout.hpp"
#include "draw-list.hpp"

#include <span>
#include <vector>

namespace aurorae
{
class Raster;
class Theme;

/** Pointer state of one laid-out button. */
struct ButtonPointerState
{
    bool hovered = false;
    bool pressed = false;
};

/** Everything needed to paint one decoration (except the caption text). */
struct DecorationScene
{
    const Theme& theme;
    const Settings& settings;
    const DecorationGeometry& geometry;
    bool active    = true;
    bool maximized = false;
    // Parallel to geometry.buttons.
    std::span<const ButtonPointerState> pointer;
    // Window icon for a menu button the theme has no SVG for; may be null.
    const Raster *icon = nullptr;
};

// The quads of the decoration frame and the inner border, in paint order.
// Positions are device pixels; origin_px is where decoration coordinate
// (0, 0) lands.
void append_frame_items(std::vector<DrawItem>& out, const DecorationScene& scene, double dpr,
    PointF origin_px);

// The quads of all buttons.
void append_button_items(std::vector<DrawItem>& out, const DecorationScene& scene, double dpr,
    PointF origin_px);

// Quads of a single button (for partial updates on hover).
void append_button_item(std::vector<DrawItem>& out, const DecorationScene& scene, size_t index,
    double dpr, PointF origin_px);

CaptionStyle caption_style(const Theme& theme, const Settings& settings, bool active);
} // namespace aurorae
