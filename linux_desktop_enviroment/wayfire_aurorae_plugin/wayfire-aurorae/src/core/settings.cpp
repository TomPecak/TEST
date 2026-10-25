#include "settings.hpp"

#include <array>
#include <utility>

namespace aurorae
{
std::optional<BorderSize> parse_border_size(std::string_view name)
{
    static constexpr std::array<std::pair<std::string_view, BorderSize>, 10> names = {{
        {"none", BorderSize::None},
        {"no_sides", BorderSize::NoSides},
        {"tiny", BorderSize::Tiny},
        {"normal", BorderSize::Normal},
        {"large", BorderSize::Large},
        {"very_large", BorderSize::VeryLarge},
        {"huge", BorderSize::Huge},
        {"very_huge", BorderSize::VeryHuge},
        {"oversized", BorderSize::Oversized},
        {"theme", BorderSize::Theme},
    }};

    for (const auto& [n, size] : names)
    {
        if (n == name)
        {
            return size;
        }
    }

    return std::nullopt;
}

std::optional<double> parse_button_size(std::string_view name)
{
    // KDecoration3::BorderSize-like scale; Aurorae maps index n to
    // 1 + (n - 1) * 0.2 with "normal" being index 1.
    static constexpr std::array<std::string_view, 7> names = {
        "tiny", "normal", "large", "very_large", "huge", "very_huge", "oversized",
    };

    for (size_t i = 0; i < names.size(); ++i)
    {
        if (names[i] == name)
        {
            return 1.0 + (double(i) - 1.0) * 0.2;
        }
    }

    return std::nullopt;
}
} // namespace aurorae
