// system_theme.h -- what colour scheme the desktop is using.
//
// SDL2 has no theme API (SDL_GetSystemTheme is SDL3), and a custom Slint
// platform gets no scheme from the backend either, so the launcher has to ask
// the OS itself and push the answer into Palette.color-scheme.

#pragma once

namespace swos {

enum class ColourScheme {
    Unknown = 0,  // could not tell -- Slint falls back to its own default
    Light = 1,
    Dark = 2,
};

// The desktop's current preference. Returns Unknown when it cannot be
// determined, which is not an error: the UI then just uses Slint's default.
ColourScheme detect_system_colour_scheme();

// How the user wants the launcher themed, persisted in gameLauncher.ini.
enum class ThemePreference {
    System = 0,
    Light = 1,
    Dark = 2,
};

// Resolves a preference against the live system setting.
ColourScheme resolve_theme(ThemePreference preference);

} // namespace swos
