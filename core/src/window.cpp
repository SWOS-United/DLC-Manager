// window.cpp -- see window.h. Placement logic ported from SWOS-United's
// src/platform_sdl.c.

#include "window.h"

#include "gl_core33.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <set>
#include <tuple>

namespace swos {

namespace {

constexpr float kMinScale = 0.5f;
constexpr float kMaxScale = 4.0f;
// The fit loops step the scale down by this much until the window fits. The
// game's integer loop is `win_scale--`; a launcher wants finer granularity.
constexpr float kScaleStep = 0.05f;

int clamp_display(int display)
{
    int count = SDL_GetNumVideoDisplays();
    if (count < 1) count = 1;
    if (display < 0 || display >= count) return 0;
    return display;
}

} // namespace

std::string DisplayMode::label() const
{
    char buf[64];
    if (refresh_hz > 0)
        std::snprintf(buf, sizeof buf, "%d x %d @ %d Hz", width, height, refresh_hz);
    else
        std::snprintf(buf, sizeof buf, "%d x %d", width, height);
    return buf;
}

Window::~Window()
{
    destroy();
}

// Pre-creation fit, ported from the `SDL_GetDisplayUsableBounds` clamp in
// Platform_Init: shrink the scale so the windowed size fits the monitor's
// usable bounds (desktop minus taskbar/dock). Without it a default scale that
// suits a desktop overflows a laptop panel -- the "window is bigger than my
// monitor" report. Windowed only; fullscreen covers the display regardless.
float Window::fit_scale_to_usable(int display, int base_w, int base_h, float scale)
{
    SDL_Rect ub;
    if (SDL_GetDisplayUsableBounds(display, &ub) != 0 || ub.w <= 0 || ub.h <= 0) return scale;

    while (scale > kMinScale &&
           (static_cast<int>(base_w * scale) > ub.w || static_cast<int>(base_h * scale) > ub.h)) {
        scale -= kScaleStep;
    }
    return std::max(scale, kMinScale);
}

// Post-creation fit, ported verbatim in spirit from Platform_Init's second
// pass. The pre-creation clamp uses usable bounds, which can under-shrink (DPI
// virtualization on Windows, work-area quirks) and CANNOT account for the title
// bar -- that is added by the OS outside the client rect, and CENTERED_DISPLAY
// centres the *client* rect, so a tall window pushes its title bar off the top
// of the screen. Redo the fit with the real decorated geometry, then move the
// window so the whole frame is on-screen.
void Window::fit_window_to_display(int display)
{
    SDL_Rect ub, db;
    bool have_ub = (SDL_GetDisplayUsableBounds(display, &ub) == 0 && ub.w > 0 && ub.h > 0);
    const bool have_db = (SDL_GetDisplayBounds(display, &db) == 0 && db.w > 0 && db.h > 0);

    if (!have_ub && have_db) {  // usable query failed -- fall back to physical
        ub = db;
        have_ub = true;
    }
    if (have_ub && have_db) {
        // Never trust usable bounds bigger than the physical display: some
        // multi-monitor drivers return the whole virtual desktop for a single
        // display index, which defeats the shrink entirely. Cap to the real
        // size/origin so the fit reflects the monitor the window is on.
        ub.w = std::min(ub.w, db.w);
        ub.h = std::min(ub.h, db.h);
        ub.x = std::max(ub.x, db.x);
        ub.y = std::max(ub.y, db.y);
    }
    if (!have_ub) return;

    // Border thicknesses; bt is the title-bar height. All 0 if unsupported.
    int bt = 0, bl = 0, bb = 0, br = 0;
    SDL_GetWindowBordersSize(m_window, &bt, &bl, &bb, &br);

    while (m_scale > kMinScale &&
           (static_cast<int>(m_base_w * m_scale) + bl + br > ub.w ||
            static_cast<int>(m_base_h * m_scale) + bt + bb > ub.h)) {
        m_scale -= kScaleStep;
    }
    m_scale = std::max(m_scale, kMinScale);
    SDL_SetWindowSize(m_window, static_cast<int>(m_base_w * m_scale),
                      static_cast<int>(m_base_h * m_scale));

    // Reposition so the full frame (client + borders) sits inside the usable
    // bounds, and guarantee the title bar is never above the top edge.
    int wx = 0, wy = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(m_window, &wx, &wy);
    SDL_GetWindowSize(m_window, &ww, &wh);
    const int outer_w = ww + bl + br;
    const int outer_h = wh + bt + bb;

    if (wx - bl < ub.x) wx = ub.x + bl;
    if (wy - bt < ub.y) wy = ub.y + bt;
    if (wx - bl + outer_w > ub.x + ub.w) wx = ub.x + ub.w - outer_w + bl;
    if (wy - bt + outer_h > ub.y + ub.h) wy = ub.y + ub.h - outer_h + bt;
    if (wx - bl < ub.x) wx = ub.x + bl;  // re-pin if it is simply too wide
    if (wy - bt < ub.y) wy = ub.y + bt;  // keep the title bar on-screen
    SDL_SetWindowPosition(m_window, wx, wy);
}

bool Window::create(const WindowOptions &opts)
{
    m_base_w = std::max(1, opts.base_width);
    m_base_h = std::max(1, opts.base_height);
    m_scale = std::clamp(opts.scale, kMinScale, kMaxScale);
    m_fullscreen = opts.fullscreen;
    m_fullscreen_pref =
            opts.fullscreen == FullscreenMode::None ? FullscreenMode::Desktop : opts.fullscreen;
    m_fs_width = opts.fullscreen_width;
    m_fs_height = opts.fullscreen_height;

    const int display = clamp_display(opts.display);
    if (m_fullscreen == FullscreenMode::None)
        m_scale = fit_scale_to_usable(display, m_base_w, m_base_h, m_scale);

    // Request 3.3 core *before* creating the window. macOS only grants a core
    // profile above 3.2 when the forward-compatible flag is set, and hands back
    // a 2.1 legacy context without it.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    // ALLOW_HIGHDPI makes the drawable larger than the window on retina/fractional
    // displays; we render the UI at drawable resolution and tell Slint the ratio.
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    if (opts.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (m_fullscreen == FullscreenMode::Desktop)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    else if (m_fullscreen == FullscreenMode::Exclusive)
        flags |= SDL_WINDOW_FULLSCREEN;

    // CENTERED_DISPLAY opens (and, for fullscreen, covers) the requested monitor.
    m_window = SDL_CreateWindow(opts.title.c_str(), SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                                SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                                static_cast<int>(m_base_w * m_scale),
                                static_cast<int>(m_base_h * m_scale), flags);
    if (!m_window) {
        m_error = std::string("SDL_CreateWindow: ") + SDL_GetError();
        return false;
    }

    if (m_fullscreen == FullscreenMode::Exclusive && m_fs_width > 0 && m_fs_height > 0)
        set_fullscreen_resolution(m_fs_width, m_fs_height);

    if (m_fullscreen == FullscreenMode::None) {
        int disp = SDL_GetWindowDisplayIndex(m_window);
        if (disp < 0) disp = display;
        fit_window_to_display(disp);
    }

    m_gl = SDL_GL_CreateContext(m_window);
    if (!m_gl) {
        m_error = std::string("SDL_GL_CreateContext (3.3 core): ") + SDL_GetError();
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return false;
    }
    SDL_GL_MakeCurrent(m_window, m_gl);

    if (!gl::load()) {
        m_error = std::string("OpenGL 3.3 core entry point missing: ") + gl::missing_symbol();
        destroy();
        return false;
    }

    set_vsync(opts.vsync);
    refresh_drawable_size();

    // Diagnostic line mirroring the one Platform_Init logs, so a tester's log
    // says whether the fit worked and whether DPI is sane.
    int wx = 0, wy = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(m_window, &wx, &wy);
    SDL_GetWindowSize(m_window, &ww, &wh);
    SDL_Rect ub{};
    SDL_GetDisplayUsableBounds(display_index(), &ub);
    std::fprintf(stderr,
                 "[video] window %dx%d @%d,%d drawable %dx%d scale=%.2f dpi=%.2f display=%d "
                 "usable=%dx%d\n",
                 ww, wh, wx, wy, m_drawable_w, m_drawable_h, static_cast<double>(m_scale),
                 static_cast<double>(dpi_scale()), display_index(), ub.w, ub.h);
    std::fprintf(stderr, "[video] GL %s | %s\n",
                 reinterpret_cast<const char *>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    return true;
}

void Window::destroy()
{
    if (m_gl) {
        SDL_GL_DeleteContext(m_gl);
        m_gl = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

void Window::refresh_drawable_size()
{
    if (!m_window) return;
    SDL_GL_GetDrawableSize(m_window, &m_drawable_w, &m_drawable_h);
    if (m_drawable_w <= 0 || m_drawable_h <= 0)
        SDL_GetWindowSize(m_window, &m_drawable_w, &m_drawable_h);
}

float Window::dpi_scale() const
{
    if (!m_window) return 1.0f;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(m_window, &ww, &wh);
    if (ww <= 0 || m_drawable_w <= 0) return 1.0f;
    return static_cast<float>(m_drawable_w) / static_cast<float>(ww);
}

int Window::display_index() const
{
    if (!m_window) return 0;
    const int idx = SDL_GetWindowDisplayIndex(m_window);
    return idx < 0 ? 0 : idx;
}

void Window::set_fullscreen_resolution(int w, int h)
{
    m_fs_width = w;
    m_fs_height = h;
    if (!m_window) return;

    if (w <= 0 || h <= 0) {
        // Back to the desktop mode.
        SDL_DisplayMode desktop;
        if (SDL_GetDesktopDisplayMode(display_index(), &desktop) == 0)
            SDL_SetWindowDisplayMode(m_window, &desktop);
        return;
    }

    SDL_DisplayMode want{}, got{};
    want.w = w;
    want.h = h;
    want.refresh_rate = 0;       // closest
    want.format = 0;
    if (SDL_GetClosestDisplayMode(display_index(), &want, &got))
        SDL_SetWindowDisplayMode(m_window, &got);
}

void Window::set_fullscreen(FullscreenMode mode)
{
    if (!m_window || mode == m_fullscreen) return;

    if (mode == FullscreenMode::None) {
        SDL_SetWindowFullscreen(m_window, 0);
        SDL_SetWindowSize(m_window, static_cast<int>(m_base_w * m_scale),
                          static_cast<int>(m_base_h * m_scale));
        // Re-run the decorated fit: the display we return to may not be the one
        // we left from, and its usable area may be smaller.
        fit_window_to_display(display_index());
    } else {
        m_fullscreen_pref = mode;
        if (mode == FullscreenMode::Exclusive) set_fullscreen_resolution(m_fs_width, m_fs_height);
        const Uint32 flag = (mode == FullscreenMode::Desktop) ? SDL_WINDOW_FULLSCREEN_DESKTOP
                                                              : SDL_WINDOW_FULLSCREEN;
        if (SDL_SetWindowFullscreen(m_window, flag) != 0) {
            std::fprintf(stderr, "[video] SDL_SetWindowFullscreen: %s\n", SDL_GetError());
            return;
        }
    }

    m_fullscreen = mode;
    refresh_drawable_size();
}

void Window::toggle_fullscreen()
{
    set_fullscreen(m_fullscreen == FullscreenMode::None ? m_fullscreen_pref : FullscreenMode::None);
}

void Window::set_scale(float scale)
{
    scale = std::clamp(scale, kMinScale, kMaxScale);
    m_scale = scale;
    if (!m_window || is_fullscreen()) return;  // matches Platform_SetWindowScale

    SDL_SetWindowSize(m_window, static_cast<int>(m_base_w * m_scale),
                      static_cast<int>(m_base_h * m_scale));
    fit_window_to_display(display_index());
    refresh_drawable_size();
}

void Window::set_vsync(bool on)
{
    if (!m_gl) return;
    // -1 asks for adaptive vsync (late-swap tearing); SDL falls back to 1 when
    // the driver lacks EXT_swap_control_tear.
    if (!on || SDL_GL_SetSwapInterval(-1) != 0) SDL_GL_SetSwapInterval(on ? 1 : 0);
}

void Window::swap() const
{
    if (m_window) SDL_GL_SwapWindow(m_window);
}

bool Window::capture_framebuffer(const std::string &path) const
{
    if (m_drawable_w <= 0 || m_drawable_h <= 0) return false;

    const int w = m_drawable_w;
    const int h = m_drawable_h;
    std::vector<unsigned char> rgb(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());

    // BMP rows are bottom-up and padded to 4 bytes. glReadPixels is bottom-up
    // too (GL's origin is bottom-left), so the rows go out in the order they
    // came back -- no flip. Only the channel order differs: BMP wants BGR.
    const int row_padded = (w * 3 + 3) & ~3;
    const int pad = row_padded - w * 3;
    const uint32_t pixel_bytes = static_cast<uint32_t>(row_padded) * static_cast<uint32_t>(h);
    const uint32_t file_size = 54 + pixel_bytes;

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    auto u16 = [&out](uint16_t v) { out.write(reinterpret_cast<const char *>(&v), 2); };
    auto u32 = [&out](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), 4); };

    out.write("BM", 2);
    u32(file_size); u32(0); u32(54);                 // size, reserved, pixel offset
    u32(40); u32(static_cast<uint32_t>(w)); u32(static_cast<uint32_t>(h));
    u16(1); u16(24); u32(0); u32(pixel_bytes);       // planes, bpp, compression, image size
    u32(2835); u32(2835); u32(0); u32(0);            // 72 DPI, no palette

    std::vector<unsigned char> row(static_cast<size_t>(row_padded), 0);
    for (int y = 0; y < h; ++y) {
        const unsigned char *src = rgb.data() + static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < w; ++x) {
            row[static_cast<size_t>(x) * 3 + 0] = src[static_cast<size_t>(x) * 3 + 2];
            row[static_cast<size_t>(x) * 3 + 1] = src[static_cast<size_t>(x) * 3 + 1];
            row[static_cast<size_t>(x) * 3 + 2] = src[static_cast<size_t>(x) * 3 + 0];
        }
        std::memset(row.data() + static_cast<size_t>(w) * 3, 0, static_cast<size_t>(pad));
        out.write(reinterpret_cast<const char *>(row.data()), row_padded);
    }
    return out.good();
}

std::vector<DisplayInfo> Window::enumerate_displays()
{
    std::vector<DisplayInfo> out;
    const int count = SDL_GetNumVideoDisplays();
    for (int i = 0; i < count; ++i) {
        DisplayInfo d;
        d.index = i;
        if (const char *name = SDL_GetDisplayName(i)) d.name = name;
        if (d.name.empty()) d.name = "Display " + std::to_string(i + 1);
        SDL_GetDisplayBounds(i, &d.bounds);
        if (SDL_GetDisplayUsableBounds(i, &d.usable_bounds) != 0) d.usable_bounds = d.bounds;

        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(i, &dm) == 0)
            d.desktop_mode = {dm.w, dm.h, dm.refresh_rate};

        // De-duplicate on w/h/refresh: SDL reports one entry per pixel format,
        // so a single physical mode shows up several times.
        std::set<std::tuple<int, int, int>> seen;
        const int mode_count = SDL_GetNumDisplayModes(i);
        for (int m = 0; m < mode_count; ++m) {
            if (SDL_GetDisplayMode(i, m, &dm) != 0) continue;
            if (!seen.emplace(dm.w, dm.h, dm.refresh_rate).second) continue;
            d.modes.push_back({dm.w, dm.h, dm.refresh_rate});
        }
        std::sort(d.modes.begin(), d.modes.end(), [](const DisplayMode &a, const DisplayMode &b) {
            return std::tie(a.width, a.height, a.refresh_hz) >
                   std::tie(b.width, b.height, b.refresh_hz);
        });
        out.push_back(std::move(d));
    }
    return out;
}

} // namespace swos
