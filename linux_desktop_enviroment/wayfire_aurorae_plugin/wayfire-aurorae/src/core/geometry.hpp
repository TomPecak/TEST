#pragma once

#include <algorithm>
#include <cmath>

namespace aurorae
{
// Plain value types used by the renderer-agnostic core. Coordinates are in
// logical pixels unless a name says otherwise ("_px" = device pixels).

struct PointF
{
    double x = 0;
    double y = 0;
};

struct SizeF
{
    double width  = 0;
    double height = 0;

    bool empty() const
    {
        return width <= 0 || height <= 0;
    }

    bool operator ==(const SizeF&) const = default;
};

struct Size
{
    int width  = 0;
    int height = 0;

    bool empty() const
    {
        return width <= 0 || height <= 0;
    }

    bool operator ==(const Size&) const = default;
};

struct RectF
{
    double x      = 0;
    double y      = 0;
    double width  = 0;
    double height = 0;

    double left() const
    {
        return x;
    }

    double top() const
    {
        return y;
    }

    double right() const
    {
        return x + width;
    }

    double bottom() const
    {
        return y + height;
    }

    bool empty() const
    {
        return width <= 0 || height <= 0;
    }

    bool contains(PointF p) const
    {
        return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
    }

    bool intersects(const RectF& o) const
    {
        return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
    }

    RectF translated(double dx, double dy) const
    {
        return {x + dx, y + dy, width, height};
    }

    RectF united(const RectF& o) const
    {
        if (empty())
        {
            return o;
        }

        if (o.empty())
        {
            return *this;
        }

        const double l = std::min(x, o.x);
        const double t = std::min(y, o.y);
        return {l, t, std::max(right(), o.right()) - l, std::max(bottom(), o.bottom()) - t};
    }

    bool operator ==(const RectF&) const = default;
};

struct Margins
{
    double left   = 0;
    double top    = 0;
    double right  = 0;
    double bottom = 0;

    bool is_null() const
    {
        return left == 0 && top == 0 && right == 0 && bottom == 0;
    }

    bool operator ==(const Margins&) const = default;
};

inline SizeF grown(SizeF s, const Margins& m)
{
    return {s.width + m.left + m.right, s.height + m.top + m.bottom};
}

// Qt's qRound(), which the reference implementation uses for all size
// rounding. Differs from std::round() only for negative halves.
inline int qround(double v)
{
    return v >= 0.0 ? int(v + 0.5) : int(v - 0.5);
}

struct Color
{
    double r = 0;
    double g = 0;
    double b = 0;
    double a = 1;

    bool operator ==(const Color&) const = default;
};
} // namespace aurorae
