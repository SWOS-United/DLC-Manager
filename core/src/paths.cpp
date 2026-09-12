// paths.cpp -- see paths.h.

#include "paths.h"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace swos {

std::string executable_dir()
{
    // SDL_GetBasePath handles the per-platform mess (GetModuleFileName,
    // _NSGetExecutablePath, /proc/self/exe) and always ends with a separator.
    if (char *base = SDL_GetBasePath()) {
        std::string out(base);
        SDL_free(base);
        if (out.size() > 1) {
            const char last = out.back();
            if (last == '/' || last == '\\') out.pop_back();
        }
        return out;
    }
    return fs::current_path().string();
}

bool running_in_bundle()
{
#if defined(__APPLE__)
    // SDL_GetBasePath returns <App>.app/Contents/Resources/ for a bundled app
    // and the plain executable directory otherwise, so the suffix is the test.
    static const bool bundled = [] {
        const std::string base = executable_dir();
        const std::string suffix = "/Contents/Resources";
        return base.size() >= suffix.size() &&
               base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0;
    }();
    return bundled;
#else
    return false;
#endif
}

namespace {

// SDL_GetPrefPath asks the OS rather than the environment, and returns a
// trailing separator the rest of this file would double up.
std::string pref_path()
{
    // NULL organisation, matching SWOS-United's swos_io.c, so both land on the
    // same directory.
    char *pref = SDL_GetPrefPath(nullptr, "SWOS");
    if (!pref || !*pref) {
        if (pref) SDL_free(pref);
        return {};
    }
    std::string out(pref);
    SDL_free(pref);
    while (out.size() > 1 && (out.back() == '/' || out.back() == '\\')) out.pop_back();
    return out;
}

std::string resolve_user_dir()
{
#if defined(_WIN32)
    // %USERPROFILE%\Documents\SWOS. Forward slashes are fine for the CRT and
    // keep the rest of the path handling uniform.
    if (const char *profile = std::getenv("USERPROFILE"); profile && *profile) {
        std::string out = std::string(profile) + "/Documents/SWOS";
        std::replace(out.begin(), out.end(), '\\', '/');
        return out;
    }
#elif defined(__APPLE__)
    if (const char *home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/Library/Application Support/SWOS";
#else
    if (const char *home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.swos";
#endif

    // A .app launched from Finder gets a trimmed environment with HOME unset,
    // and the silent fallback to "." is read-only inside a bundle. Ask the OS.
    const std::string pref = pref_path();
    if (!pref.empty())
        std::fprintf(stderr, "[paths] HOME not set; using the OS preferences path: %s\n",
                     pref.c_str());
    return pref;
}

} // namespace

std::string user_dir()
{
    static const std::string dir = [] {
        std::string resolved = resolve_user_dir();
        if (resolved.empty()) {
            std::fprintf(stderr, "[paths] cannot determine the per-user directory; "
                                 "falling back to the executable directory\n");
            return executable_dir();
        }
        if (!ensure_directory(resolved)) {
            std::fprintf(stderr, "[paths] cannot create %s; falling back to the "
                                 "executable directory\n", resolved.c_str());
            return executable_dir();
        }
        return resolved;
    }();
    return dir;
}

std::string path_join(const std::string &a, const std::string &b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    return (fs::path(a) / fs::path(b)).string();
}

std::string parent_path(const std::string &p)
{
    return fs::path(p).parent_path().string();
}

bool file_exists(const std::string &p)
{
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

bool directory_exists(const std::string &p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

bool ensure_directory(const std::string &p)
{
    if (p.empty()) return true;
    std::error_code ec;
    if (fs::is_directory(p, ec)) return true;
    fs::create_directories(p, ec);
    return !ec;
}

bool rename_file(const std::string &from, const std::string &to)
{
    std::error_code ec;
    fs::rename(from, to, ec);
    return !ec;
}

bool remove_file(const std::string &p)
{
    std::error_code ec;
    fs::remove(p, ec);
    return !ec;
}

bool remove_directory(const std::string &p)
{
    std::error_code ec;
    fs::remove_all(p, ec);
    return !ec;
}

std::vector<std::string> list_directory(const std::string &dir, bool directories_only)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        if (directories_only && !entry.is_directory(ec)) continue;
        out.push_back(entry.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

int64_t file_size(const std::string &p)
{
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec) return -1;
    return static_cast<int64_t>(size);
}

std::string read_first_line(const std::string &p)
{
    std::ifstream in(p);
    if (!in) return {};

    std::string line;
    std::getline(in, line);
    const auto begin = line.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = line.find_last_not_of(" \t\r\n");
    return line.substr(begin, end - begin + 1);
}

std::string swos_dir(const std::string &base_dir)
{
    const std::string override_path = path_join(base_dir, "swos-path.txt");
    if (file_exists(override_path)) {
        const std::string configured = read_first_line(override_path);
        if (!configured.empty()) return configured;
    }
    return path_join(base_dir, "SWOS");
}

const char *game_executable_name()
{
    // The Delphi launcher hard-coded "swos-port-Release.exe". SWOS-United's
    // Makefile produces bin/SWOS (bin/SWOS.exe on Windows), which is what this
    // launcher targets once the two trees merge.
#if defined(_WIN32)
    return "SWOS.exe";
#else
    return "SWOS";
#endif
}

} // namespace swos
