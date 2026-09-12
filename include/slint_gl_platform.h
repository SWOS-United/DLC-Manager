// slint_gl_platform.h -- host Slint inside our own SDL2 + OpenGL 3.3 window.
//
// Slint's default backend opens and owns a window of its own (winit + femtovg or
// Skia). The launcher can't use that: SDL2 owns the window and the GL context,
// and once this app folds into SWOS-United the game renderer will share them.
// So we register a custom slint::platform::Platform whose WindowAdapter drives
// Slint's SoftwareRenderer into a CPU buffer, then uploads that buffer as a GL
// texture and presents it on a fullscreen quad.
//
// Rendering is event-driven: a frame is produced only when Slint asks for a
// redraw or an animation is running, never on a free-running loop. That is what
// makes a full-surface software render affordable -- see the RepaintBufferType
// note in the .cpp.

#pragma once

#include "window.h"

#include <slint-platform.h>
#include <slint.h>

#include <functional>
#include <memory>
#include <vector>

namespace swos {

// Owns the CPU pixel buffer Slint renders into plus the GL objects that put it
// on screen.
class UiSurface {
public:
    bool init();
    void destroy();

    // Reallocates the buffer and texture. No-op when the size is unchanged.
    void resize(int width, int height);

    int width() const { return m_width; }
    int height() const { return m_height; }
    std::span<slint::Rgb8Pixel> pixels() { return {m_pixels.data(), m_pixels.size()}; }

    void upload_full();
    // Draws the surface over the whole viewport.
    void draw(int viewport_w, int viewport_h) const;

private:
    std::vector<slint::Rgb8Pixel> m_pixels;
    int m_width = 0;
    int m_height = 0;

    unsigned m_tex = 0;
    unsigned m_vao = 0;
    unsigned m_vbo = 0;
    unsigned m_program = 0;
    int m_u_rect = -1;
    int m_u_tex = -1;
};

class GlWindowAdapter final : public slint::platform::WindowAdapter {
public:
    explicit GlWindowAdapter(Window &window);

    // --- WindowAdapter ---------------------------------------------------
    slint::platform::AbstractRenderer &renderer() override { return m_renderer; }
    slint::PhysicalSize size() override;
    void set_size(slint::PhysicalSize size) override;
    void request_redraw() override { m_redraw_requested = true; }
    void set_visible(bool visible) override;
    void update_window_properties(const WindowProperties &props) override;
    std::optional<slint::PhysicalPosition> position() override;
    void set_position(slint::PhysicalPosition pos) override;

    // --- Host side -------------------------------------------------------
    // Renders a frame if one is due and returns true if it did. The caller
    // swaps buffers.
    bool render_if_needed();
    bool needs_redraw() const;

    // Re-reads the drawable size and tells Slint about it. Call after any
    // resize / fullscreen switch / DPI change.
    void sync_size();

    // UI zoom on top of the display's own DPI scaling (1.0 = 100%).
    void set_ui_scale(float scale);
    float ui_scale() const { return m_ui_scale; }

    bool close_requested() const { return m_close_requested; }
    void clear_close_request() { m_close_requested = false; }

    void handle_sdl_event(const SDL_Event &e);

    // True once the Slint window has asked to be shown.
    bool visible() const { return m_visible; }

private:
    float scale_factor() const;
    slint::LogicalPosition to_logical(int window_x, int window_y) const;
    void dispatch_special_key(SDL_Keycode sym, Uint16 mod, bool pressed, bool repeat);

    Window &m_window;
    slint::platform::SoftwareRenderer m_renderer;
    UiSurface m_surface;

    float m_ui_scale = 1.0f;
    float m_last_scale_factor = 0.0f;
    bool m_redraw_requested = true;
    bool m_close_requested = false;
    bool m_visible = false;
    // Slint requested fullscreen through its own window properties (e.g. a
    // `fullscreen: true` binding); mirrored onto the SDL window.
    bool m_slint_fullscreen = false;
};

class SdlPlatform final : public slint::platform::Platform {
public:
    explicit SdlPlatform(Window &window) : m_window(window) { }

    std::unique_ptr<slint::platform::WindowAdapter> create_window_adapter() override;

    void set_clipboard_text(const slint::SharedString &text, Clipboard clipboard) override;
    std::optional<slint::SharedString> clipboard_text(Clipboard clipboard) override;

    // We drive the loop ourselves in main(), so run_event_loop() is never
    // called. run_in_event_loop() still has to work: Slint uses it to hop work
    // back onto the UI thread, so we post an SDL user event to wake the loop.
    void run_in_event_loop(Task task) override;
    void quit_event_loop() override;

    // Drains tasks queued by run_in_event_loop(). Call once per loop iteration.
    static void drain_tasks();
    static Uint32 wakeup_event_type();

    GlWindowAdapter *adapter() const { return m_adapter; }

private:
    Window &m_window;
    GlWindowAdapter *m_adapter = nullptr;  // owned by Slint, borrowed here
};

} // namespace swos
