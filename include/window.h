// window.h -- SDL2 window + OpenGL 3.3 core context.
//
// The window-placement logic here is ported from SWOS-United's
// src/platform_sdl.c (Platform_Init / Platform_ToggleFullscreen /
// Platform_SetWindowScale) so the two projects behave identically once they
// merge. The hard-won parts -- clamping the scale to the monitor's *usable*
// bounds, redoing that fit after creation with the real decorated geometry, and
// never trusting usable bounds larger than the physical display -- are kept
// as-is; see the comments at each step for why each one exists.
//
// What changed for the launcher: the game scales a fixed 336x272 framebuffer by
// whole numbers, whereas the launcher has a design size and a fractional scale
// (100%..200%), so `scale` is a float and the fit loop steps it down instead of
// decrementing an integer.

#pragma once

#include <SDL.h>

#include <string>
#include <vector>

namespace swos {

// Mirrors PlatformFullscreenMode in SWOS-United's platform_sdl.h.
//
// None:      windowed at `scale` (Alt+Enter toggles to Desktop at runtime).
// Desktop:   SDL_WINDOW_FULLSCREEN_DESKTOP -- borderless window sized to the
//            current desktop, no display-mode change. Fast to toggle.
// Exclusive: SDL_WINDOW_FULLSCREEN -- true fullscreen with a display-mode
//            change. Always covers the taskbar; slower to toggle, may flicker
//            on multi-monitor rigs, and restores the desktop mode on exit.
enum class FullscreenMode { None = 0, Desktop = 1, Exclusive = 2 };

struct DisplayMode {
    int width = 0;
    int height = 0;
    int refresh_hz = 0;

    std::string label() const;  // "1920 x 1080 @ 60 Hz"
};

struct DisplayInfo {
    int index = 0;
    std::string name;
    SDL_Rect bounds{};        // physical bounds in the virtual desktop
    SDL_Rect usable_bounds{}; // minus taskbar / dock
    DisplayMode desktop_mode; // the mode the display is currently in
    std::vector<DisplayMode> modes;  // de-duplicated, largest first
};

struct WindowOptions {
    std::string title = "SWOS Launcher";
    int base_width = 960;   // design size at scale 1.0
    int base_height = 640;
    float scale = 1.0f;     // windowed scale, 0.5 .. 4.0
    FullscreenMode fullscreen = FullscreenMode::None;
    int display = 0;        // target monitor (0 = primary), clamped to a real one
    bool vsync = true;
    bool resizable = true;
    // Exclusive fullscreen only: 0x0 means "keep the desktop mode".
    int fullscreen_width = 0;
    int fullscreen_height = 0;
};

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    // Creates the window and a 3.3 core context, and loads the GL entry points.
    // Returns false and fills last_error() on failure.
    bool create(const WindowOptions &opts);
    void destroy();

    SDL_Window *sdl_window() const { return m_window; }
    SDL_GLContext gl_context() const { return m_gl; }
    const std::string &last_error() const { return m_error; }

    // --- Geometry --------------------------------------------------------
    // Drawable size is in *physical* pixels and is what GL and the Slint
    // surface are sized to. On a HiDPI display it is larger than the window
    // size; dpi_scale() is their ratio.
    int drawable_width() const { return m_drawable_w; }
    int drawable_height() const { return m_drawable_h; }
    float dpi_scale() const;

    // Recomputes the cached drawable size. Call after a resize event.
    void refresh_drawable_size();

    // --- Fullscreen ------------------------------------------------------
    void set_fullscreen(FullscreenMode mode);
    void toggle_fullscreen();  // windowed <-> the flavour we launched with
    FullscreenMode fullscreen_mode() const { return m_fullscreen; }
    bool is_fullscreen() const { return m_fullscreen != FullscreenMode::None; }

    // Picks the exclusive-fullscreen display mode. 0x0 restores the desktop
    // mode. Takes effect on the next switch into Exclusive.
    void set_fullscreen_resolution(int w, int h);

    // --- Windowed scale --------------------------------------------------
    // No-op while fullscreen, matching Platform_SetWindowScale.
    void set_scale(float scale);
    float scale() const { return m_scale; }

    int display_index() const;
    void set_vsync(bool on);
    void swap() const;

    // Writes the current back buffer to a 24-bit BMP. Used by the
    // SWOS_LAUNCHER_SCREENSHOT debug hook and for bug reports; BMP keeps this
    // dependency-free. Must be called before swap(), while the frame is still
    // in the back buffer.
    bool capture_framebuffer(const std::string &path) const;

    // --- Display enumeration ---------------------------------------------
    // "All resolutions" in the launcher UI comes from here rather than the
    // hard-coded lists the Delphi launcher shipped.
    static std::vector<DisplayInfo> enumerate_displays();

private:
    // The two-stage fit ported from Platform_Init. `fit_scale_to_usable` runs
    // before creation (cheap, no decorations known); `fit_window_to_display`
    // redoes it afterwards with the real frame size and repositions the window.
    static float fit_scale_to_usable(int display, int base_w, int base_h, float scale);
    void fit_window_to_display(int display);

    SDL_Window *m_window = nullptr;
    SDL_GLContext m_gl = nullptr;
    std::string m_error;

    int m_base_w = 0;
    int m_base_h = 0;
    float m_scale = 1.0f;
    FullscreenMode m_fullscreen = FullscreenMode::None;
    // The flavour to return to when Alt+Enter turns fullscreen back on.
    FullscreenMode m_fullscreen_pref = FullscreenMode::Desktop;
    int m_fs_width = 0;
    int m_fs_height = 0;
    int m_drawable_w = 0;
    int m_drawable_h = 0;
};

} // namespace swos
