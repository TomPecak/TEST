// aurorae-preview: renders a mock window decorated with an Aurorae theme to a
// PNG, using exactly the layout and rasterization code of the Wayfire plugin.
// Handy for checking themes without restarting the compositor, for visual
// regression tests and for benchmarks.

#include "core/cairo-backend.hpp"
#include "core/caption.hpp"
#include "core/decoration-painter.hpp"
#include "core/kde-settings.hpp"
#include "core/svg-document.hpp"
#include "core/theme.hpp"

#include <cairo.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using namespace aurorae;

namespace
{
struct Options
{
    std::string theme;
    SizeF client{480, 260};
    std::string title = "Aurorae preview — Wayfire";
    double scale = 1.0;
    bool inactive  = false;
    bool maximized = false;
    std::string hover;   // button letter, e.g. "X"
    std::string pressed; // button letter
    bool sheet = false;
    bool benchmark = false;
    bool dump = false;
    std::string out = "preview.png";
    std::string hide;              // button letters to hide
    Color backdrop{0.36, 0.42, 0.50, 1};
};

struct Rendered
{
    SurfacePtr image;
    Size size;
};

void usage()
{
    std::puts(
        "usage: aurorae-preview --theme NAME|DIR [options]\n"
        "  --size WxH            client area size (default 480x260)\n"
        "  --title TEXT          window title\n"
        "  --scale S             output scale (default 1)\n"
        "  --inactive            render an inactive window\n"
        "  --maximized           render a maximized window\n"
        "  --hover L / --pressed L  button letter (X close, A maximize, I minimize, ...)\n"
        "  --border-size NAME    none|no_sides|tiny|normal|large|...|theme (default normal)\n"
        "  --button-size NAME    tiny|normal|large|... (default normal)\n"
        "  --buttons-left L      button layout, e.g. MS\n"
        "  --buttons-right L     button layout, e.g. HIAX\n"
        "  --font DESC           Pango font (default: KDE title bar font)\n"
        "  --text-shadow         draw the theme's legacy text shadow\n"
        "  --hide LETTERS        hide these buttons (as if the window manager does not offer them)\n"
        "  --backdrop R,G,B      backdrop color, 0-255 (default 92,107,128)\n"
        "  --sheet               render active/inactive/hover/pressed/maximized side by side\n"
        "  --dump                print element sizes, margins and the laid-out pieces\n"
        "  --benchmark           time theme loading, rasterization and layout\n"
        "  --list                list installed themes\n"
        "  -o FILE               output PNG (default preview.png)");
}

std::optional<ButtonType> letter_type(const std::string& letter)
{
    auto types = parse_button_layout(letter);
    if (types.empty())
    {
        return std::nullopt;
    }

    return types.front();
}

WindowState make_state(const Options& o)
{
    WindowState state;
    state.client    = o.client;
    state.active    = !o.inactive;
    state.maximized = o.maximized;
    // Mirror what Wayland windows offer: no context help, no app menu.
    state.buttons[size_t(ButtonType::ContextHelp)].visible     = false;
    state.buttons[size_t(ButtonType::ApplicationMenu)].visible = false;
    for (ButtonType type : parse_button_layout(o.hide))
    {
        state.buttons[size_t(type)].visible = false;
    }

    return state;
}

Rendered render(const Theme& theme, const Settings& settings, const Options& o)
{
    const WindowState state = make_state(o);
    const DecorationGeometry g = compute_geometry(theme, settings, state);

    std::vector<ButtonPointerState> pointer(g.buttons.size());
    for (size_t i = 0; i < g.buttons.size(); ++i)
    {
        if (letter_type(o.hover) == g.buttons[i].type)
        {
            pointer[i].hovered = true;
        }

        if (letter_type(o.pressed) == g.buttons[i].type)
        {
            pointer[i].hovered = pointer[i].pressed = true;
        }
    }

    // Image covers everything we draw plus a margin, on a neutral backdrop.
    const double margin = 16;
    const RectF area = g.bounds.united(g.frame);
    const PointF origin{(margin - area.x) * o.scale, (margin - area.y) * o.scale};
    const Size size{int(std::ceil((area.width + 2 * margin) * o.scale)),
        int(std::ceil((area.height + 2 * margin) * o.scale))};

    auto image = make_surface(size);
    cairo_t *cr = cairo_create(image.get());
    cairo_set_source_rgb(cr, o.backdrop.r, o.backdrop.g, o.backdrop.b);
    cairo_paint(cr);

    // The client area, as a window would draw it.
    cairo_rectangle(cr, origin.x + g.client.x * o.scale, origin.y + g.client.y * o.scale,
        g.client.width * o.scale, g.client.height * o.scale);
    cairo_set_source_rgb(cr, 0.937, 0.941, 0.945);
    cairo_fill(cr);

    // Like KWin, the decoration never paints over the client area.
    cairo_save(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_rectangle(cr, 0, 0, size.width, size.height);
    cairo_rectangle(cr, origin.x + g.client.x * o.scale, origin.y + g.client.y * o.scale,
        g.client.width * o.scale, g.client.height * o.scale);
    cairo_clip(cr);

    DecorationScene scene{theme, settings, g, state.active, state.maximized, pointer, nullptr};
    std::vector<DrawItem> items;
    append_frame_items(items, scene, o.scale, origin);
    cairo_draw(cr, items);

    auto caption = render_caption(o.title, caption_style(theme, settings, state.active),
        {g.caption.width, g.caption.height}, o.scale);
    if (caption.surface)
    {
        cairo_set_source_surface(cr, caption.surface.get(), origin.x + (g.caption.x + caption.x) * o.scale,
            origin.y + g.caption.y * o.scale);
        cairo_paint(cr);
    }

    items.clear();
    append_button_items(items, scene, o.scale, origin);
    cairo_draw(cr, items);
    cairo_restore(cr);
    cairo_destroy(cr);
    return {std::move(image), size};
}

void write_png(cairo_surface_t *surface, const std::string& path)
{
    if (cairo_surface_write_to_png(surface, path.c_str()) != CAIRO_STATUS_SUCCESS)
    {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
    } else
    {
        std::printf("wrote %s\n", path.c_str());
    }
}

void render_sheet(const Theme& theme, const Settings& settings, Options o)
{
    struct Variant
    {
        bool inactive, maximized;
        std::string hover, pressed;
    };
    const Variant variants[] = {
        {false, false, "", ""},
        {true, false, "", ""},
        {false, false, "X", ""},
        {false, false, "", "A"},
        {false, true, "", ""},
    };

    std::vector<Rendered> parts;
    Size total{0, 0};
    for (const auto& v : variants)
    {
        o.inactive  = v.inactive;
        o.maximized = v.maximized;
        o.hover   = v.hover;
        o.pressed = v.pressed;
        parts.push_back(render(theme, settings, o));
        total.width  = std::max(total.width, parts.back().size.width);
        total.height += parts.back().size.height;
    }

    auto sheet = make_surface(total);
    cairo_t *cr = cairo_create(sheet.get());
    cairo_set_source_rgb(cr, o.backdrop.r, o.backdrop.g, o.backdrop.b);
    cairo_paint(cr);
    double y = 0;
    for (auto& p : parts)
    {
        cairo_set_source_surface(cr, p.image.get(), 0, y);
        cairo_paint(cr);
        y += p.size.height;
    }

    cairo_destroy(cr);
    write_png(sheet.get(), o.out);
}

void dump(const Theme& theme, const Settings& settings, const Options& o)
{
    const WindowState state = make_state(o);
    const DecorationGeometry g = compute_geometry(theme, settings, state);
    std::printf("borders  l=%g t=%g r=%g b=%g\n", g.borders.left, g.borders.top, g.borders.right, g.borders.bottom);
    std::printf("frame    %g,%g %gx%g (size %dx%d)\n", g.frame.x, g.frame.y, g.frame.width, g.frame.height,
        g.frame_size.width, g.frame_size.height);
    std::printf("caption  %g,%g %gx%g\n", g.caption.x, g.caption.y, g.caption.width, g.caption.height);
    for (const auto& b : g.buttons)
    {
        std::printf("button   %d at %g,%g %gx%g\n", int(b.type), b.rect.x, b.rect.y, b.rect.width, b.rect.height);
    }

    const FrameSvg& frame = theme.decoration(state.active, state.maximized);
    const Margins m = frame.margins();
    std::printf("frame margins l=%g t=%g r=%g b=%g\n", m.left, m.top, m.right, m.bottom);
    for (size_t i = 0; i < PIECE_COUNT; ++i)
    {
        const auto& id = frame.element(Piece(i));
        if (auto r = frame.document().element_rect(id))
        {
            std::printf("  %-34s %8.2f,%8.2f %7.2fx%7.2f\n", id.c_str(), r->x, r->y, r->width, r->height);
        }
    }

    std::vector<FramePiece> pieces;
    frame.layout(g.frame_size, g.frame_borders, o.scale, pieces);
    for (const auto& p : pieces)
    {
        std::printf("  piece %-28s dst %7.2f,%7.2f %7.2fx%7.2f raster %dx%d%s\n",
            frame.element(p.piece).c_str(), p.dst.x, p.dst.y, p.dst.width, p.dst.height,
            p.raster.width, p.raster.height, p.tiled ? " tiled" : "");
    }
}

void benchmark(const std::string& theme_name, const Settings& settings, const Options& o)
{
    using clock = std::chrono::steady_clock;
    auto ms = [] (clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };

    auto t0 = clock::now();
    auto theme = Theme::load(theme_name);
    auto t1 = clock::now();
    if (!theme)
    {
        return;
    }

    // First paint: rasterizes every element needed for an active window.
    WindowState state = make_state(o);
    std::vector<DrawItem> items;
    std::vector<ButtonPointerState> pointer;
    auto build = [&] (const WindowState& s)
    {
        const auto g = compute_geometry(*theme, settings, s);
        pointer.assign(g.buttons.size(), {});
        DecorationScene scene{*theme, settings, g, s.active, s.maximized, pointer, nullptr};
        items.clear();
        append_frame_items(items, scene, o.scale, {0, 0});
        append_button_items(items, scene, o.scale, {0, 0});
    };

    build(state);
    auto t2 = clock::now();

    // Steady state: an interactive resize through 1000 sizes. All rasters are
    // cached, so this is pure layout work.
    for (int i = 0; i < 1000; ++i)
    {
        state.client = {300.0 + i, 200.0 + i / 2};
        build(state);
    }

    auto t3 = clock::now();
    std::printf("theme load:            %8.2f ms\n", ms(t1 - t0));
    std::printf("first decoration:      %8.2f ms (%zu rasters)\n", ms(t2 - t1), theme->rasters().size());
    std::printf("resize step (avg/1000):%8.4f ms, %zu quads, rasters now %zu\n",
        ms(t3 - t2) / 1000.0, items.size(), theme->rasters().size());
}
} // namespace

int main(int argc, char **argv)
{
    Options o;
    Settings settings;
    if (auto font = kde::titlebar_font())
    {
        settings.font = *font;
    }

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string
        {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(2);
            }

            return argv[++i];
        };

        if (a == "--theme")
        {
            o.theme = next();
        } else if (a == "--size")
        {
            int w = 0, h = 0;
            if (std::sscanf(next().c_str(), "%dx%d", &w, &h) == 2)
            {
                o.client = {double(w), double(h)};
            }
        } else if (a == "--title")
        {
            o.title = next();
        } else if (a == "--scale")
        {
            o.scale = std::atof(next().c_str());
        } else if (a == "--inactive")
        {
            o.inactive = true;
        } else if (a == "--maximized")
        {
            o.maximized = true;
        } else if (a == "--hover")
        {
            o.hover = next();
        } else if (a == "--pressed")
        {
            o.pressed = next();
        } else if (a == "--border-size")
        {
            settings.border_size = parse_border_size(next()).value_or(settings.border_size);
        } else if (a == "--button-size")
        {
            settings.button_size_factor = parse_button_size(next()).value_or(1.0);
        } else if (a == "--buttons-left")
        {
            settings.buttons_left = parse_button_layout(next());
        } else if (a == "--buttons-right")
        {
            settings.buttons_right = parse_button_layout(next());
        } else if (a == "--font")
        {
            settings.font = next();
        } else if (a == "--text-shadow")
        {
            settings.legacy_text_shadow = true;
        } else if (a == "--hide")
        {
            o.hide = next();
        } else if (a == "--backdrop")
        {
            int r = 0, g = 0, b = 0;
            if (std::sscanf(next().c_str(), "%d,%d,%d", &r, &g, &b) == 3)
            {
                o.backdrop = {r / 255.0, g / 255.0, b / 255.0, 1};
            }
        } else if (a == "--sheet")
        {
            o.sheet = true;
        } else if (a == "--dump")
        {
            o.dump = true;
        } else if (a == "--benchmark")
        {
            o.benchmark = true;
        } else if (a == "--list")
        {
            for (const auto& name : theme_locator::list())
            {
                std::puts(name.c_str());
            }

            return 0;
        } else if (a == "-o")
        {
            o.out = next();
        } else
        {
            usage();
            return a == "--help" ? 0 : 2;
        }
    }

    if (o.theme.empty())
    {
        o.theme = kde::aurorae_theme().value_or("");
    }

    if (o.benchmark)
    {
        benchmark(o.theme, settings, o);
        return 0;
    }

    std::string error;
    auto theme = Theme::load(o.theme, &error);
    if (!theme)
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        usage();
        return 1;
    }

    if (o.dump)
    {
        dump(*theme, settings, o);
        return 0;
    }

    if (o.sheet)
    {
        render_sheet(*theme, settings, o);
    } else
    {
        write_png(render(*theme, settings, o).image.get(), o.out);
    }

    return 0;
}
