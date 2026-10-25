#pragma once

#include "geometry.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurorae
{
class SvgDocument;

enum Border : unsigned
{
    NoBorder     = 0,
    TopBorder    = 1,
    BottomBorder = 2,
    LeftBorder   = 4,
    RightBorder  = 8,
    AllBorders   = TopBorder | BottomBorder | LeftBorder | RightBorder,
};
using Borders = unsigned;

enum class Piece : unsigned char
{
    Center, Top, Bottom, Left, Right, TopLeft, TopRight, BottomLeft, BottomRight,
    Overlay, // drawn over the whole frame, see FrameSvg
};
constexpr size_t PIECE_COUNT = 10;

/** One element of a laid-out frame. */
struct FramePiece
{
    Piece piece;
    // Destination in device pixels, relative to the frame's top-left corner.
    RectF dst;
    // Size in device pixels at which the element is rasterized.
    Size raster;
    // Repeat the raster across dst (from its top-left) instead of stretching it.
    bool tiled = false;
    // Only this part of dst is drawn (device pixels, frame-relative).
    std::optional<RectF> clip;
};

/**
 * A nine-slice frame described by SVG elements named "<prefix>-topleft",
 * "<prefix>-top", ..., "<prefix>-center" — a reimplementation of the parts
 * of KSvg::FrameSvg that Aurorae uses.
 *
 * Supported hints: hint-{top,bottom,left,right}-margin, hint-stretch-borders,
 * hint-tile-center, hint-no-border-padding, and the "overlay" element with
 * hint-overlay-{pos-right,pos-bottom,stretch,tile-horizontal,tile-vertical}.
 * hint-compose-over-border and masked overlays are Plasma-theme features
 * Aurorae themes do not use.
 *
 * Layout reproduces KSvg's arithmetic (device-pixel content rect, rounded
 * corner positions, ceil'ed sizes) so frames line up pixel for pixel. Unlike
 * KSvg, elements are rasterized at device resolution on HiDPI outputs instead
 * of being upscaled from 1x pixmaps.
 */
class FrameSvg
{
  public:
    FrameSvg(std::shared_ptr<const SvgDocument> doc, std::string_view requested_prefix);

    // KSvg::FrameSvg::hasElementPrefix(): whether "<prefix>-center" exists.
    static bool has_prefix(const SvgDocument& doc, std::string_view prefix);

    const SvgDocument& document() const
    {
        return *doc;
    }

    // Whether the frame can be drawn at all (its center element exists).
    bool drawable() const
    {
        return exists[size_t(Piece::Center)];
    }

    const std::string& element(Piece p) const
    {
        return ids[size_t(p)];
    }

    // KSvg::FrameSvg::margins() for the given enabled borders.
    Margins margins(Borders enabled = AllBorders) const;

    // Appends the pieces of a frame of the given logical size. The result
    // depends only on the arguments, so callers may cache it.
    void layout(Size frame_size, Borders enabled, double dpr, std::vector<FramePiece>& out) const;

  private:
    enum class OverlayMode { None, Place, PosRight, PosBottom, Stretch, Tile };

    Size raster_size(Piece p, double dpr) const;
    void layout_overlay(Size frame_size, double dpr, std::vector<FramePiece>& out) const;

    std::shared_ptr<const SvgDocument> doc;
    std::array<std::string, PIECE_COUNT> ids;
    std::array<bool, PIECE_COUNT> exists{};
    std::array<SizeF, PIECE_COUNT> sizes{}; // KSvg elementSize(), rounded

    Margins fixed_margins; // from hint-*-margin, else the border sizes
    bool no_border_padding = false;
    bool stretch_borders   = false;
    bool tile_center = false;
    OverlayMode overlay = OverlayMode::None;
    bool overlay_tile_h = false;
    bool overlay_tile_v = false;
};
} // namespace aurorae
