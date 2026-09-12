// system_theme.cpp -- see system_theme.h.

#include "system_theme.h"

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <string>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#elif defined(__APPLE__)
#    include <CoreFoundation/CoreFoundation.h>
#endif

namespace swos {

namespace {

#if !defined(_WIN32) && !defined(__APPLE__)
// Runs a command and returns its first line of stdout, or "" if it is not
// installed or fails. Used for the gsettings probes below.
std::string command_output(const char *command)
{
    FILE *pipe = popen(command, "r");
    if (!pipe) return {};

    char buffer[256] = {0};
    const char *line = std::fgets(buffer, sizeof buffer, pipe);
    const int status = pclose(pipe);
    if (!line || status != 0) return {};

    std::string out(buffer);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == '\'' ||
                            out.back() == ' '))
        out.pop_back();
    // gsettings quotes its answer: 'prefer-dark'
    if (!out.empty() && out.front() == '\'') out.erase(out.begin());
    return out;
}

bool contains_ci(const std::string &haystack, const char *needle)
{
    std::string lower;
    lower.reserve(haystack.size());
    for (char c : haystack) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find(needle) != std::string::npos;
}
#endif

} // namespace

ColourScheme detect_system_colour_scheme()
{
#if defined(_WIN32)
    // AppsUseLightTheme: 0 = dark, 1 = light. Absent on Windows versions older
    // than the 2018 dark-mode release, where light is the only option.
    DWORD value = 1;
    DWORD size = sizeof value;
    const LSTATUS status = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status != ERROR_SUCCESS) return ColourScheme::Unknown;
    return value == 0 ? ColourScheme::Dark : ColourScheme::Light;

#elif defined(__APPLE__)
    // AppleInterfaceStyle only exists while dark mode is on; its absence means
    // light, so "not found" is a real answer here rather than Unknown.
    CFPropertyListRef value = CFPreferencesCopyAppValue(CFSTR("AppleInterfaceStyle"),
                                                        kCFPreferencesAnyApplication);
    if (!value) return ColourScheme::Light;

    ColourScheme scheme = ColourScheme::Light;
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        const CFStringRef text = static_cast<CFStringRef>(value);
        if (CFStringCompare(text, CFSTR("Dark"), kCFCompareCaseInsensitive) == kCFCompareEqualTo)
            scheme = ColourScheme::Dark;
    }
    CFRelease(value);
    return scheme;

#else
    // Freedesktop: the portal setting is authoritative but needs a session bus,
    // so try gsettings first -- it covers GNOME and the many desktops that
    // mirror its keys -- then fall back to the GTK theme name.
    const std::string scheme = command_output(
            "gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null");
    if (!scheme.empty()) {
        if (contains_ci(scheme, "dark")) return ColourScheme::Dark;
        if (contains_ci(scheme, "light")) return ColourScheme::Light;
        // "default" means the user expressed no preference.
    }

    const std::string theme =
            command_output("gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null");
    if (!theme.empty()) return contains_ci(theme, "dark") ? ColourScheme::Dark : ColourScheme::Light;

    if (const char *env = std::getenv("GTK_THEME"))
        return contains_ci(env, "dark") ? ColourScheme::Dark : ColourScheme::Light;

    return ColourScheme::Unknown;
#endif
}

ColourScheme resolve_theme(ThemePreference preference)
{
    switch (preference) {
    case ThemePreference::Light: return ColourScheme::Light;
    case ThemePreference::Dark: return ColourScheme::Dark;
    case ThemePreference::System: break;
    }
    return detect_system_colour_scheme();
}

} // namespace swos
