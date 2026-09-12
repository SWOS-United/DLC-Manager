// slint_gl_platform.cpp -- see slint_gl_platform.h.

#include "slint_gl_platform.h"

#include "gl_core33.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <queue>
#include <string>

namespace swos {

namespace {

// The UI is opaque and covers the window, so a plain RGB blit is all we need.
// Alpha compositing over a GL layer underneath would require Slint's
// experimental TargetPixelBuffer or the Skia renderer -- see docs/ARCHITECTURE.md.
constexpr const char *kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 a_pos;
uniform vec4 u_rect;   // x, y, w, h in normalised device coordinates
out vec2 v_uv;
void main() {
    // Buffer row 0 is the top of the UI, so flip V against the NDC Y axis.
    v_uv = vec2(a_pos.x, 1.0 - a_pos.y);
    gl_Position = vec4(u_rect.xy + a_pos * u_rect.zw, 0.0, 1.0);
}
)";

constexpr const char *kFragmentShader = R"(#version 330 core
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 frag_color;
void main() {
    frag_color = vec4(texture(u_tex, v_uv).rgb, 1.0);
}
)";

unsigned compile_shader(GLenum type, const char *source)
{
    const unsigned shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(std::max(len, 1)), '\0');
        glGetShaderInfoLog(shader, len, nullptr, log.data());
        std::fprintf(stderr, "[ui] %s shader failed to compile:\n%s\n",
                     type == GL_VERTEX_SHADER ? "vertex" : "fragment", log.c_str());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Tasks posted from other threads via Platform::run_in_event_loop.
std::mutex g_task_mutex;
std::queue<slint::platform::Platform::Task> g_tasks;
Uint32 g_wakeup_event = 0;

} // namespace

// ---------------------------------------------------------------------------
// UiSurface
// ---------------------------------------------------------------------------

bool UiSurface::init()
{
    const unsigned vs = compile_shader(GL_VERTEX_SHADER, kVertexShader);
    const unsigned fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) return false;

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(std::max(len, 1)), '\0');
        glGetProgramInfoLog(m_program, len, nullptr, log.data());
        std::fprintf(stderr, "[ui] shader program failed to link:\n%s\n", log.c_str());
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    m_u_rect = glGetUniformLocation(m_program, "u_rect");
    m_u_tex = glGetUniformLocation(m_program, "u_tex");

    // A unit quad as a triangle strip; the vertex shader maps it into NDC.
    static const GLfloat quad[] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f};
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
    glBindVertexArray(0);

    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    // NEAREST: the buffer is rendered at the drawable's exact pixel size, so the
    // quad is always a 1:1 blit and any filtering would only soften text.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void UiSurface::destroy()
{
    if (m_tex) glDeleteTextures(1, &m_tex);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_program) glDeleteProgram(m_program);
    m_tex = m_vbo = m_vao = m_program = 0;
    m_pixels.clear();
    m_width = m_height = 0;
}

void UiSurface::resize(int width, int height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width == m_width && height == m_height) return;

    m_width = width;
    m_height = height;
    // Slint repaints the whole surface each frame (NewBuffer), so the initial
    // contents only matter for the window's very first presented frame.
    m_pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), slint::Rgb8Pixel{});

    glBindTexture(GL_TEXTURE_2D, m_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void UiSurface::upload_full()
{
    if (m_pixels.empty()) return;
    glBindTexture(GL_TEXTURE_2D, m_tex);
    // Rgb8Pixel is 3 tightly packed bytes, so rows are not 4-byte aligned.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGB, GL_UNSIGNED_BYTE,
                    m_pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void UiSurface::draw(int viewport_w, int viewport_h) const
{
    if (!m_program || !m_tex) return;

    glViewport(0, 0, viewport_w, viewport_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glUniform1i(m_u_tex, 0);
    glUniform4f(m_u_rect, -1.f, -1.f, 2.f, 2.f);  // cover the whole viewport

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
}

// ---------------------------------------------------------------------------
// GlWindowAdapter
// ---------------------------------------------------------------------------

namespace {

bool lc_has_min(const slint::platform::WindowAdapter::WindowProperties &props)
{
    const auto lc = props.layout_constraints();
    return lc.min.has_value() && lc.min->width > 0 && lc.min->height > 0;
}

// An unconstrained maximum comes back as a huge sentinel rather than nullopt;
// clamping the OS window to that is both pointless and, on some window
// managers, a resize.
bool lc_has_usable_max(const slint::platform::WindowAdapter::WindowProperties &props)
{
    const auto lc = props.layout_constraints();
    return lc.max.has_value() && lc.max->width > 0 && lc.max->height > 0 &&
           lc.max->width < 100000.0f && lc.max->height < 100000.0f;
}

} // namespace

GlWindowAdapter::GlWindowAdapter(Window &window)
    : m_window(window)
      // NewBuffer = repaint the whole surface each frame.
      //
      // ReusedBuffer would let Slint damage-track and let us push only the
      // changed rectangles with glTexSubImage2D, but it requires the buffer to
      // still hold the previous frame -- which stops being true the moment a
      // resize reallocates it, and the C++ API exposes no way to force a
      // one-shot full repaint afterwards. Since we render only on demand (see
      // render_if_needed) rather than every vsync, a full software repaint per
      // interaction is cheap and has no such failure mode. If profiling ever
      // says otherwise, render() already hands back the dirty PhysicalRegion.
    , m_renderer(slint::platform::SoftwareRenderer::RepaintBufferType::NewBuffer)
{
    // Safe here: Slint only asks for a window adapter after Window::create()
    // has made the GL context current and resolved the entry points.
    if (!m_surface.init())
        std::fprintf(stderr, "[ui] failed to create the GL presentation surface\n");
}

float GlWindowAdapter::scale_factor() const
{
    // Slint's scale factor folds together the display's own DPI scaling and the
    // user's UI zoom, so logical coordinates stay in design units either way.
    return std::max(0.1f, m_window.dpi_scale() * m_ui_scale);
}

slint::PhysicalSize GlWindowAdapter::size()
{
    return slint::PhysicalSize({static_cast<uint32_t>(std::max(m_window.drawable_width(), 1)),
                                static_cast<uint32_t>(std::max(m_window.drawable_height(), 1))});
}

void GlWindowAdapter::set_size(slint::PhysicalSize size)
{
    // Slint asked for a specific size (slint::Window::set_size, or a layout
    // preferred size at first show). Honour it in window coordinates -- the
    // drawable may be larger on HiDPI -- then let sync_size() report back what
    // the window manager actually gave us.
    if (m_window.is_fullscreen()) return;

    const float dpi = std::max(0.1f, m_window.dpi_scale());
    SDL_SetWindowSize(m_window.sdl_window(), static_cast<int>(size.width / dpi),
                      static_cast<int>(size.height / dpi));
    m_window.refresh_drawable_size();
    sync_size();
}

void GlWindowAdapter::set_visible(bool visible)
{
    m_visible = visible;
    if (!m_window.sdl_window()) return;

    if (visible) {
        SDL_ShowWindow(m_window.sdl_window());
        // Slint initialises its scale factor lazily; the docs call this out as
        // the right moment to seed it.
        sync_size();
    } else {
        SDL_HideWindow(m_window.sdl_window());
    }
}

void GlWindowAdapter::update_window_properties(const WindowProperties &props)
{
    if (!m_window.sdl_window()) return;

    // Slint calls this whenever a property queried here is re-evaluated, which
    // a tab switch is enough to trigger. Every branch is therefore
    // edge-triggered: pushing the same value at SDL again is not free.
    // SDL_SetWindowMinimumSize in particular re-applies the window size, and
    // X11 re-places the client rect relative to the frame, so an unconditional
    // call walks the window up-left by the decoration size on every repaint.

    const slint::SharedString title = props.title();
    if (m_applied_title != title.data()) {
        m_applied_title = title.data();
        SDL_SetWindowTitle(m_window.sdl_window(), m_applied_title.c_str());
    }

    // Let a `fullscreen:` binding in the .slint file drive the real window,
    // without fighting Alt+Enter.
    const bool want_fullscreen = props.is_fullscreen();
    if (want_fullscreen != m_slint_fullscreen) {
        m_slint_fullscreen = want_fullscreen;
        m_window.set_fullscreen(want_fullscreen ? FullscreenMode::Desktop : FullscreenMode::None);
        sync_size();
    }

    const bool maximized = props.is_maximized();
    const bool minimized = props.is_minimized();
    if (maximized != m_applied_maximized) {
        m_applied_maximized = maximized;
        if (maximized)
            SDL_MaximizeWindow(m_window.sdl_window());
        else if (!minimized)
            SDL_RestoreWindow(m_window.sdl_window());
    }
    if (minimized != m_applied_minimized) {
        m_applied_minimized = minimized;
        if (minimized)
            SDL_MinimizeWindow(m_window.sdl_window());
        else if (!maximized)
            SDL_RestoreWindow(m_window.sdl_window());
    }

    // Propagate the UI's own layout constraints so the user cannot drag the
    // window smaller than the design allows. Constraints are logical; SDL wants
    // window units, and window units are logical times the UI zoom (the DPI
    // half of the scale factor cancels).
    const float zoom = std::max(0.1f, m_ui_scale);

    if (lc_has_min(props)) {
        const auto lc = props.layout_constraints();
        const int w = static_cast<int>(lc.min->width * zoom);
        const int h = static_cast<int>(lc.min->height * zoom);
        if (w != m_applied_min_w || h != m_applied_min_h) {
            m_applied_min_w = w;
            m_applied_min_h = h;
            SDL_SetWindowMinimumSize(m_window.sdl_window(), std::max(w, 1), std::max(h, 1));
        }
    }

    if (lc_has_usable_max(props)) {
        const auto lc = props.layout_constraints();
        const int w = static_cast<int>(lc.max->width * zoom);
        const int h = static_cast<int>(lc.max->height * zoom);
        if (w != m_applied_max_w || h != m_applied_max_h) {
            m_applied_max_w = w;
            m_applied_max_h = h;
            SDL_SetWindowMaximumSize(m_window.sdl_window(), std::max(w, 1), std::max(h, 1));
        }
    }
}

std::optional<slint::PhysicalPosition> GlWindowAdapter::position()
{
    if (!m_window.sdl_window()) return std::nullopt;
    int x = 0, y = 0;
    SDL_GetWindowPosition(m_window.sdl_window(), &x, &y);
    return slint::PhysicalPosition({x, y});
}

void GlWindowAdapter::set_position(slint::PhysicalPosition pos)
{
    if (m_window.sdl_window()) SDL_SetWindowPosition(m_window.sdl_window(), pos.x, pos.y);
}

void GlWindowAdapter::sync_size()
{
    m_window.refresh_drawable_size();

    const float sf = scale_factor();
    if (sf != m_last_scale_factor) {
        m_last_scale_factor = sf;
        window().dispatch_scale_factor_change_event(sf);
    }

    const int pw = std::max(m_window.drawable_width(), 1);
    const int ph = std::max(m_window.drawable_height(), 1);
    m_surface.resize(pw, ph);
    window().dispatch_resize_event(
            slint::LogicalSize({static_cast<float>(pw) / sf, static_cast<float>(ph) / sf}));
    m_redraw_requested = true;
}

void GlWindowAdapter::set_ui_scale(float scale)
{
    scale = std::clamp(scale, 0.5f, 4.0f);
    if (scale == m_ui_scale) return;
    m_ui_scale = scale;
    sync_size();
}

bool GlWindowAdapter::needs_redraw() const
{
    return m_redraw_requested || window().has_active_animations();
}

bool GlWindowAdapter::render_if_needed()
{
    if (!needs_redraw()) return false;
    if (m_surface.width() <= 0 || m_surface.height() <= 0) return false;

    m_redraw_requested = false;
    m_renderer.render(m_surface.pixels(), static_cast<size_t>(m_surface.width()));
    m_surface.upload_full();
    m_surface.draw(m_window.drawable_width(), m_window.drawable_height());
    return true;
}

slint::LogicalPosition GlWindowAdapter::to_logical(int window_x, int window_y) const
{
    // SDL reports pointer coordinates in window (not drawable) units, so the
    // DPI part of the scale factor cancels out and only the UI zoom remains.
    const float s = std::max(0.1f, m_ui_scale);
    return slint::LogicalPosition(
            {static_cast<float>(window_x) / s, static_cast<float>(window_y) / s});
}

void GlWindowAdapter::dispatch_special_key(SDL_Keycode sym, Uint16 mod, bool pressed,
                                           bool repeat)
{
    namespace keys = slint::platform::key_codes;
    std::optional<slint::SharedString> text;

    switch (sym) {
    case SDLK_BACKSPACE: text = slint::SharedString(keys::Backspace); break;
    case SDLK_TAB:
        text = slint::SharedString((mod & KMOD_SHIFT) ? keys::Backtab : keys::Tab);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  text = slint::SharedString(keys::Return); break;
    case SDLK_ESCAPE:    text = slint::SharedString(keys::Escape); break;
    case SDLK_DELETE:    text = slint::SharedString(keys::Delete); break;
    case SDLK_UP:        text = slint::SharedString(keys::UpArrow); break;
    case SDLK_DOWN:      text = slint::SharedString(keys::DownArrow); break;
    case SDLK_LEFT:      text = slint::SharedString(keys::LeftArrow); break;
    case SDLK_RIGHT:     text = slint::SharedString(keys::RightArrow); break;
    case SDLK_HOME:      text = slint::SharedString(keys::Home); break;
    case SDLK_END:       text = slint::SharedString(keys::End); break;
    case SDLK_PAGEUP:    text = slint::SharedString(keys::PageUp); break;
    case SDLK_PAGEDOWN:  text = slint::SharedString(keys::PageDown); break;
    case SDLK_INSERT:    text = slint::SharedString(keys::Insert); break;
    case SDLK_MENU:      text = slint::SharedString(keys::Menu); break;
    case SDLK_LSHIFT:    text = slint::SharedString(keys::Shift); break;
    case SDLK_RSHIFT:    text = slint::SharedString(keys::ShiftR); break;
    case SDLK_LCTRL:     text = slint::SharedString(keys::Control); break;
    case SDLK_RCTRL:     text = slint::SharedString(keys::ControlR); break;
    case SDLK_LALT:      text = slint::SharedString(keys::Alt); break;
    case SDLK_RALT:      text = slint::SharedString(keys::AltGr); break;
    case SDLK_LGUI:      text = slint::SharedString(keys::Meta); break;
    case SDLK_RGUI:      text = slint::SharedString(keys::MetaR); break;
    default: break;
    }

    if (sym >= SDLK_F1 && sym <= SDLK_F12) {
        // slint::platform::key_codes::F1..F24 are contiguous from U+F704 in the
        // Unicode private use area, so index off F1 rather than listing twelve
        // cases. Encoded by hand: the constants are u8 string views, and a
        // private-use character literal in source does not survive editing.
        constexpr char32_t kF1 = 0xF704;
        const char32_t code = kF1 + static_cast<char32_t>(sym - SDLK_F1);
        std::string utf8;
        utf8 += static_cast<char>(0xE0 | (code >> 12));
        utf8 += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        utf8 += static_cast<char>(0x80 | (code & 0x3F));
        text = slint::SharedString(utf8);
    }

    // Ctrl+<letter> never produces an SDL_TEXTINPUT event, so shortcuts like
    // Ctrl+C would otherwise never reach Slint. Synthesise the letter here.
    if (!text && (mod & KMOD_CTRL) && sym >= SDLK_a && sym <= SDLK_z) {
        const char letter = static_cast<char>('a' + (sym - SDLK_a));
        text = slint::SharedString(std::string(1, letter));
    }

    if (!text) return;
    if (!pressed)
        window().dispatch_key_release_event(*text);
    else if (repeat)
        window().dispatch_key_press_repeat_event(*text);
    else
        window().dispatch_key_press_event(*text);
}

void GlWindowAdapter::handle_sdl_event(const SDL_Event &e)
{
    switch (e.type) {
    case SDL_QUIT:
        m_close_requested = true;
        window().dispatch_close_requested_event();
        break;

    case SDL_WINDOWEVENT:
        switch (e.window.event) {
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_RESIZED:
            sync_size();
            break;
        case SDL_WINDOWEVENT_EXPOSED:
        case SDL_WINDOWEVENT_SHOWN:
            m_redraw_requested = true;
            break;
        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
            // Moved to a monitor with a different DPI -- the scale factor and
            // hence the whole layout may change.
            sync_size();
            break;
        case SDL_WINDOWEVENT_LEAVE:
            window().dispatch_pointer_exit_event();
            break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            window().dispatch_window_active_changed_event(true);
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            window().dispatch_window_active_changed_event(false);
            break;
        case SDL_WINDOWEVENT_CLOSE:
            m_close_requested = true;
            window().dispatch_close_requested_event();
            break;
        default: break;
        }
        break;

    case SDL_MOUSEMOTION:
        window().dispatch_pointer_move_event(to_logical(e.motion.x, e.motion.y));
        break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        slint::PointerEventButton button;
        switch (e.button.button) {
        case SDL_BUTTON_LEFT:   button = slint::PointerEventButton::Left; break;
        case SDL_BUTTON_RIGHT:  button = slint::PointerEventButton::Right; break;
        case SDL_BUTTON_MIDDLE: button = slint::PointerEventButton::Middle; break;
        case SDL_BUTTON_X1:     button = slint::PointerEventButton::Back; break;
        case SDL_BUTTON_X2:     button = slint::PointerEventButton::Forward; break;
        default:                button = slint::PointerEventButton::Other; break;
        }
        const auto pos = to_logical(e.button.x, e.button.y);
        if (e.type == SDL_MOUSEBUTTONDOWN)
            window().dispatch_pointer_press_event(pos, button);
        else
            window().dispatch_pointer_release_event(pos, button);
        break;
    }

    case SDL_MOUSEWHEEL: {
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        // Slint expects a pixel-ish delta; SDL reports notches.
        constexpr float kPixelsPerNotch = 40.0f;
        float dx = e.wheel.preciseX * kPixelsPerNotch;
        float dy = e.wheel.preciseY * kPixelsPerNotch;
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            dx = -dx;
            dy = -dy;
        }
        window().dispatch_pointer_scroll_event(to_logical(mx, my), dx, dy);
        break;
    }

    case SDL_TEXTINPUT:
        // Printable characters arrive here already composed (IME, dead keys),
        // which is why SDL_KEYDOWN only handles the non-printable ones.
        window().dispatch_key_press_event(slint::SharedString(e.text.text));
        window().dispatch_key_release_event(slint::SharedString(e.text.text));
        break;

    case SDL_KEYDOWN:
        dispatch_special_key(e.key.keysym.sym, e.key.keysym.mod, true, e.key.repeat != 0);
        break;

    case SDL_KEYUP:
        dispatch_special_key(e.key.keysym.sym, e.key.keysym.mod, false, false);
        break;

    default: break;
    }
}

// ---------------------------------------------------------------------------
// SdlPlatform
// ---------------------------------------------------------------------------

std::unique_ptr<slint::platform::WindowAdapter> SdlPlatform::create_window_adapter()
{
    auto adapter = std::make_unique<GlWindowAdapter>(m_window);
    m_adapter = adapter.get();
    return adapter;
}

void SdlPlatform::set_clipboard_text(const slint::SharedString &text, Clipboard clipboard)
{
    if (clipboard != Clipboard::DefaultClipboard) return;  // no X11 primary selection here
    SDL_SetClipboardText(text.data());
}

std::optional<slint::SharedString> SdlPlatform::clipboard_text(Clipboard clipboard)
{
    if (clipboard != Clipboard::DefaultClipboard) return std::nullopt;
    if (!SDL_HasClipboardText()) return std::nullopt;

    char *text = SDL_GetClipboardText();
    if (!text) return std::nullopt;
    slint::SharedString out(text);
    SDL_free(text);
    return out;
}

Uint32 SdlPlatform::wakeup_event_type()
{
    if (g_wakeup_event == 0) g_wakeup_event = SDL_RegisterEvents(1);
    return g_wakeup_event;
}

void SdlPlatform::run_in_event_loop(Task task)
{
    {
        std::lock_guard lock(g_task_mutex);
        g_tasks.push(std::move(task));
    }
    // Wake a loop that is blocked in SDL_WaitEvent.
    SDL_Event wake{};
    wake.type = wakeup_event_type();
    SDL_PushEvent(&wake);
}

void SdlPlatform::drain_tasks()
{
    for (;;) {
        std::optional<Task> task;
        {
            std::lock_guard lock(g_task_mutex);
            if (g_tasks.empty()) return;
            task.emplace(std::move(g_tasks.front()));
            g_tasks.pop();
        }
        std::move(*task).run();
    }
}

void SdlPlatform::quit_event_loop()
{
    SDL_Event quit{};
    quit.type = SDL_QUIT;
    SDL_PushEvent(&quit);
}

} // namespace swos
