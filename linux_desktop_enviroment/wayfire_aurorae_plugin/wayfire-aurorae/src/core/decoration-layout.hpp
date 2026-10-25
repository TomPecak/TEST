#pragma once

#include "buttons.hpp"
#include "frame-svg.hpp"
#include "geometry.hpp"
#include "settings.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace aurorae
{
class Theme;

constexpr size_t BUTTON_TYPE_COUNT = size_t(ButtonType::Spacer) + 1;

/** What the window manager allows for a button on a particular window. */
struct ButtonAvailability
{
    bool visible = true;
    bool enabled = true;
    bool checked = false;
};

/** Everything about a window that affects its decoration's geometry. */
struct WindowState
{
    SizeF client;
    bool active    = true;
    bool maximized = false;
    bool resizable = true;
    std::array<ButtonAvailability, BUTTON_TYPE_COUNT> buttons;
};

struct LaidOutButton
{
    ButtonType type;
    RectF rect;    // decoration coordinates
    int group = 0; // 0 = left, 1 = right
    bool enabled = true;
    bool checked = false;
};

/**
 * Geometry of one decoration, in decoration coordinates: (0, 0) is the
 * top-left corner of the decoration (the window including its borders, not
 * including the shadow padding).
 */
struct DecorationGeometry
{
    Margins borders;
    SizeF size;          // client + borders
    RectF client;
    RectF frame;         // where the decoration FrameSvg is drawn
    Size frame_size;     // ... and its integer size, as KSvg sees it
    Borders frame_borders = AllBorders;
    RectF inner_border;  // inner border frame (clipped to the decoration), empty if none
    Size inner_border_size;
    RectF title_bar;     // KDecoration's titleBar(): the move area
    RectF caption;
    std::array<RectF, 2> groups;
    std::vector<LaidOutButton> buttons; // visible buttons only
    RectF bounds;        // everything we draw or take input on
};

DecorationGeometry compute_geometry(const Theme& theme, const Settings& settings, const WindowState& state);

/** What lies under a point of the decoration. */
struct HitResult
{
    enum class Kind { None, Frame, TitleBar, Button, Resize };

    Kind kind = Kind::None;
    int button = -1;        // index into DecorationGeometry::buttons
    uint32_t edges = 0;     // WLR_EDGE_* style bit mask for Resize
};

// Edge bits, identical to wlroots' enum wlr_edges.
enum ResizeEdge : uint32_t
{
    EdgeTop = 1, EdgeBottom = 2, EdgeLeft = 4, EdgeRight = 8,
};

HitResult hit_test(const DecorationGeometry& geometry, const Settings& settings, bool resizable,
    PointF point);
} // namespace aurorae
