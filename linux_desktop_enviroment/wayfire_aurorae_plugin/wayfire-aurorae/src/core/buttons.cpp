#include "buttons.hpp"
#include "svg-document.hpp"

namespace aurorae
{
std::vector<ButtonType> parse_button_layout(std::string_view letters)
{
    std::vector<ButtonType> buttons;
    for (char c : letters)
    {
        switch (c)
        {
          case 'M':
            buttons.push_back(ButtonType::Menu);
            break;

          case 'N':
            buttons.push_back(ButtonType::ApplicationMenu);
            break;

          case 'S':
            buttons.push_back(ButtonType::OnAllDesktops);
            break;

          case 'H':
            buttons.push_back(ButtonType::ContextHelp);
            break;

          case 'I':
            buttons.push_back(ButtonType::Minimize);
            break;

          case 'A':
            buttons.push_back(ButtonType::Maximize);
            break;

          case 'X':
            buttons.push_back(ButtonType::Close);
            break;

          case 'F':
            buttons.push_back(ButtonType::KeepAbove);
            break;

          case 'B':
            buttons.push_back(ButtonType::KeepBelow);
            break;

          case 'L':
            buttons.push_back(ButtonType::Shade);
            break;

          case '_':
            buttons.push_back(ButtonType::Spacer);
            break;

          default:
            break;
        }
    }

    return buttons;
}

std::string_view button_file_stem(ButtonType type)
{
    switch (type)
    {
      case ButtonType::Menu:
        return "menu";

      case ButtonType::ApplicationMenu:
        return "appmenu";

      case ButtonType::OnAllDesktops:
        return "alldesktops";

      case ButtonType::Minimize:
        return "minimize";

      case ButtonType::Maximize:
        return "maximize";

      case ButtonType::Close:
        return "close";

      case ButtonType::ContextHelp:
        return "help";

      case ButtonType::Shade:
        return "shade";

      case ButtonType::KeepBelow:
        return "keepbelow";

      case ButtonType::KeepAbove:
        return "keepabove";

      case ButtonType::Spacer:
        return "";
    }

    return "";
}

ButtonFrameSet::ButtonFrameSet(std::shared_ptr<const SvgDocument> document) :
    doc(std::move(document)),
    active(doc, "active")
{
    hover       = make("hover");
    pressed     = make("pressed");
    deactivated = make("deactivated");
    inactive    = make("inactive");
    hover_inactive   = make("hover-inactive");
    pressed_inactive = make("pressed-inactive");
    deactivated_inactive = make("deactivated-inactive");
}

std::optional<FrameSvg> ButtonFrameSet::make(std::string_view prefix) const
{
    if (!FrameSvg::has_prefix(*doc, prefix))
    {
        return std::nullopt;
    }

    return FrameSvg(doc, prefix);
}

// SvgFrameSet::select() from aurorae/v2/decorationbutton.cpp
const FrameSvg& ButtonFrameSet::select(const ButtonStates& s) const
{
    if (s.window_active)
    {
        if (s.pressed)
        {
            if (pressed)
            {
                return *pressed;
            }
        } else
        {
            if (s.hovered && hover)
            {
                return *hover;
            }

            if (s.disabled && deactivated)
            {
                return *deactivated;
            }
        }

        return active;
    }

    if (s.pressed)
    {
        if (pressed_inactive)
        {
            return *pressed_inactive;
        }

        if (pressed)
        {
            return *pressed;
        }
    } else
    {
        if (s.hovered)
        {
            if (hover_inactive)
            {
                return *hover_inactive;
            }

            if (hover)
            {
                return *hover;
            }
        }

        if (s.disabled)
        {
            if (deactivated_inactive)
            {
                return *deactivated_inactive;
            }

            if (deactivated)
            {
                return *deactivated;
            }
        }
    }

    return inactive ? *inactive : active;
}
} // namespace aurorae
