// paths.cpp -- see paths.h.

#include "paths.h"

#include <SDL.h>

#include <algorithm>
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
