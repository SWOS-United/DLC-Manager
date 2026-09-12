# Architecture

Each app is an SDL2 program with an OpenGL 3.3 core context and a Slint UI
composited into that context as a texture. Nothing else in the process owns a
window.

## Layout

```
core/                shared library (swos-core)
apps/launcher/       swos-launcher
apps/dlc_manager/    swos-dlc-manager
```

The core is shared rather than duplicated because the Delphi build shares it
too: `dlc_info_u.pas` is used by both the launcher (which applies the selected
DLC before starting the game) and the DLC manager, and both read and write the
same `ini/gameLauncher.ini`. Keeping them in one project means the DLC model is
edited once — the same reason SWOS-United merged its two repos.

```
        SDL2 window  +  GL 3.3 core context          src/window.cpp
                 |                   ^
        SDL events |                 | fullscreen quad
                 v                   |
   GlWindowAdapter --> SoftwareRenderer --> CPU RGB8 buffer --> GL texture
                                                          src/slint_gl_platform.cpp
```

## Why not Slint's own backend

Slint ships backends (winit + FemtoVG or Skia) that open and own a window. These
apps cannot use them for two reasons:

1. The explicit target is SDL2 + OpenGL 3.3 core.
2. This app is destined to merge into **SWOS-United**, whose renderer already
   owns an SDL2 window. Two window systems in one process is not a thing worth
   untangling later.

So the launcher registers a custom `slint::platform::Platform`. That interface is
stable in the C++ API (only `TargetPixelBuffer` and a couple of rotation helpers
sit behind `SLINT_FEATURE_EXPERIMENTAL`, and we use neither).

## The presentation path

`slint::platform::SoftwareRenderer` renders the UI into a `std::vector<Rgb8Pixel>`
sized to the window's *drawable* (physical pixel) size. That buffer is uploaded
to a `GL_RGB8` texture and drawn on a fullscreen quad by a small GLSL 330 program
(`src/slint_gl_platform.cpp`).

Consequences worth knowing:

* **The UI is opaque.** `Rgb8Pixel` has no alpha, so the UI covers the window
  completely and nothing can be composited underneath it. That is right for a
  launcher. When the game renderer arrives and the UI needs to float over it, the
  options are Slint's `SkiaRenderer` (it takes a native window handle) or the
  experimental `TargetPixelBuffer`; neither is needed yet.
* **Repainting is whole-surface, not damage-tracked.** The renderer is
  constructed with `RepaintBufferType::NewBuffer`. `ReusedBuffer` would let Slint
  hand back only the dirty rectangles for a partial `glTexSubImage2D`, but it
  requires the buffer to still hold the previous frame — which stops being true
  the moment a resize reallocates it, and the C++ API exposes no way to force a
  one-shot full repaint afterwards. Since frames are produced **only on demand**
  (see below) rather than every vsync, a full software repaint per interaction is
  cheap and has no such failure mode. `render()` already returns the dirty
  `PhysicalRegion` if this ever needs revisiting.

## The event loop

`main()` drives the loop; Slint's `run_event_loop()` is never called. Each pass:

1. `SDL_WaitEventTimeout` — blocks until there is input, waking in time for the
   next Slint timer or animation frame (`duration_until_next_timer_update`).
2. SDL events go to `GlWindowAdapter::handle_sdl_event`, which translates them
   into `dispatch_pointer_*` / `dispatch_key_*` calls.
3. `SdlPlatform::drain_tasks()` runs anything posted by `run_in_event_loop`.
4. `slint::platform::update_timers_and_animations()`.
5. A frame is rendered and swapped **only if** Slint asked for a redraw or an
   animation is running.

The launcher is therefore idle (0% CPU) when nothing is happening.

## Coordinates and scaling

Three sizes are in play and it is easy to mix them up:

| | what it is |
|---|---|
| window size | what the window manager reports; SDL mouse coordinates are in these units |
| drawable size | physical pixels; GL viewport and the Slint surface are this size |
| logical size | what the `.slint` file's `px` units mean |

Slint's scale factor is `dpi_scale * ui_scale`, where `dpi_scale` is
drawable ÷ window (HiDPI / retina) and `ui_scale` is the user's interface-zoom
setting (100%–200%, the Delphi `windowScale`). Mouse coordinates arrive in window
units, so converting them only needs `ui_scale` — the DPI part cancels.

## Window handling

`src/window.cpp` is ported from SWOS-United's `src/platform_sdl.c` so the two
behave identically after the merge. The parts that exist for a specific reason:

* The windowed scale is clamped to the target monitor's **usable** bounds before
  creation, so a default that suits a desktop does not overflow a laptop panel.
* The fit is then **redone after creation** with the real decorated geometry.
  `SDL_GetDisplayUsableBounds` cannot account for the title bar (the OS adds it
  outside the client rect) and `SDL_WINDOWPOS_CENTERED_DISPLAY` centres the
  *client* rect, so without this pass a tall window puts its title bar off the
  top of the screen.
* Usable bounds larger than the physical display are capped: some multi-monitor
  drivers return the whole virtual desktop for a single display index, which
  otherwise defeats the shrink entirely.
* Fullscreen has two flavours, as in the game: `Desktop`
  (`SDL_WINDOW_FULLSCREEN_DESKTOP`, borderless, no mode change, fast to toggle)
  and `Exclusive` (`SDL_WINDOW_FULLSCREEN`, real mode change, always covers the
  taskbar).

## Theming

Slint's own backends read the desktop light/dark setting; a custom platform gets
no such signal, and SDL2 has no theme API (`SDL_GetSystemTheme` is SDL3). So
`src/system_theme.cpp` asks the OS directly — the `AppsUseLightTheme` registry
value on Windows, `AppleInterfaceStyle` on macOS, `gsettings` then `GTK_THEME` on
Linux — and `main()` pushes the answer into the `Theme` global, which sets
`Palette.color-scheme`. The user's preference (system / light / dark) persists in
`gameLauncher.ini` as `[Launcher] themeMode`.

Icons are Tabler's monochrome `currentColor` SVGs, tinted at runtime with Slint's
`colorize`, so a single file serves both themes.

## Debug hooks

Environment variables, in the style of SWOS-United's `SWOS_*` hooks. They exist
so the UI can be smoke-tested where the window manager cannot be screenshotted
(WSLg, CI runners):

| variable | effect |
|---|---|
| `SWOS_LAUNCHER_TAB=<n>` | open on tab *n* |
| `SWOS_LAUNCHER_CLICK=<x>,<y>` | synthesise a left click at window pixel *x,y* |
| `SWOS_LAUNCHER_SCREENSHOT=<file>` | write the settled frame to a BMP, then exit |
| `SWOS_DLC_CATEGORY=<n>` | DLC manager: open category *n* |
| `SWOS_DLC_REFRESH=1` | DLC manager: fetch every catalogue at startup |
| `SWOS_DLC_DOWNLOAD=<row>` | DLC manager: install the pack in row *row* |
| `SWOS_DLC_REMOVE=<row>` | DLC manager: uninstall the pack in row *row* |
| `SWOS_DLC_SCREENSHOT=<file>` | DLC manager: capture once idle, then exit |

The capture fires once the UI has settled — nothing left to redraw, and for the
DLC manager no download or extraction still running — with a deadline so it
cannot hang a CI run.

## Background work (DLC manager)

Downloads run on a worker thread inside `swos::Download`; extraction gets its
own thread because a big sprite pack takes seconds of CPU. The UI thread never
blocks on either: it polls them once per frame and reads progress through a
mutex-guarded snapshot. That is also why the event loop caps its wait at one
frame while anything is in flight, instead of sleeping until the next input.
