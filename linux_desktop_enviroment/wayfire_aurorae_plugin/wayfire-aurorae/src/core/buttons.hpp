#pragma once

#include "frame-svg.hpp"

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace aurorae
{
class SvgDocument;

/** KDecoration3::DecorationButtonType, restricted to what Aurorae draws. */
enum class ButtonType : unsigned char
{
    Menu,
    ApplicationMenu,
    OnAllDesktops,
    Minimize,
    Maximize,
    Close,
    ContextHelp,
    Shade,
    KeepBelow,
    KeepAbove,
    Spacer,
};

// Parses KDE's button layout notation ("MS", "HIAX", ...). Unknown letters
// are skipped.
std::vector<ButtonType> parse_button_layout(std::string_view letters);

// Theme file stem for a button type, or empty for buttons without an SVG.
std::string_view button_file_stem(ButtonType type);

/** The visual state of a button, as in Aurorae's DecorationButton::states(). */
struct ButtonStates
{
    bool window_active = true;
    bool hovered  = false;
    bool pressed  = false; // also set for checked toggle buttons
    bool disabled = false;
};

/**
 * One button SVG with a frame per state prefix ("active", "hover",
 * "pressed-inactive", ...), and Aurorae's fallback rules between them.
 */
class ButtonFrameSet
{
  public:
    explicit ButtonFrameSet(std::shared_ptr<const SvgDocument> doc);

    const FrameSvg& select(const ButtonStates& states) const;

  private:
    std::optional<FrameSvg> make(std::string_view prefix) const;

    std::shared_ptr<const SvgDocument> doc;
    FrameSvg active;
    std::optional<FrameSvg> hover, pressed, deactivated;
    std::optional<FrameSvg> inactive, hover_inactive, pressed_inactive, deactivated_inactive;
};
} // namespace aurorae
