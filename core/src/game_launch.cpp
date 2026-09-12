// game_launch.cpp -- see game_launch.h. Ported from getCLI / SetupSWOSIni /
// WinExecAndWait32 in miniLauncher_u.pas.

#include "game_launch.h"

#include "ini_file.h"
#include "paths.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sstream>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <spawn.h>
#    include <sys/wait.h>
#    include <unistd.h>
extern char **environ;
#endif

namespace swos {

namespace {

std::string flag(const char *name, int value)
{
    return std::string("--") + name + "=" + std::to_string(value);
}

std::string flag(const char *name, const std::string &value)
{
    return std::string("--") + name + "=" + value;
}

} // namespace

std::vector<std::string> build_arguments(const LaunchRequest &req)
{
    const Settings &s = req.settings;
    std::vector<std::string> args;

    args.push_back(flag("game-style", s.game_style));
    args.push_back(flag("screen-mode", s.screen_mode));
    // The game's window-mode is 1 = windowed, 2 = fullscreen.
    args.push_back(flag("window-mode", s.full_screen == 1 ? 2 : 1));
    args.push_back(flag("aspect-ratio", s.aspect_ratio == 1 ? 1 : 0));
    args.push_back(flag("auto-centering", s.auto_centering == 1 ? 1 : 0));
    args.push_back(flag("screen-enhancement", s.screen_enhancement));
    args.push_back(flag("filter", s.screen_filter));
    args.push_back(flag("shader-program", s.shader_program));
    // getCLI keys --shader off the *filter* rather than scaleQuality; the
    // scaleQuality branch next to it was commented out and is kept that way.
    args.push_back(flag("shader", s.screen_filter == 0 ? 0 : 1));

    // The background-image option is only live in widescreen mode; every other
    // mode forces it off, matching the cmbMenuBGImage.Enabled guard in SetUI.
    const bool bg_image_enabled = (s.screen_mode == 2);
    args.push_back(flag("menu-background-image", bg_image_enabled ? s.menu_bg_image : 0));

    int w = req.override_width;
    int h = req.override_height;
    if (w <= 0 || h <= 0) {
        w = 0;
        h = 0;
        s.resolve_resolution(w, h);
    }
    if (w > 0 && h > 0) {
        args.push_back(flag("window-width", w));
        args.push_back(flag("window-height", h));
    }

    args.push_back(flag("amigaish", s.menu_style));
    args.push_back(flag("new-scoreboard", s.new_scoreboard));

    if (s.skip_intro == 1) args.push_back("--no-intro");
    if (s.skip_reel == 1) args.push_back("--no-image-reels");
    if (s.no_load_pause == 1) args.push_back("--no-load-pause");

    if (bg_image_enabled && s.alpha_blending_menu == 1) {
        args.push_back(
                flag("alpha-blending-menu",
                     std::to_string(options::alpha_percent_for_index(s.alpha_blending_menu_percent))));
    }

    if (s.new_show_result == 1) args.push_back(flag("new-show-result", 1));
    if (s.remove_darken_banner == 1) args.push_back(flag("remove-darken-banner", 1));
    if (s.show_fps == 1) args.push_back(flag("show-fps", 1));

    args.push_back(flag("improved-pitch-palette", s.improved_pitch_palette));
    args.push_back(flag("ball-trails", s.ball_trails));
    args.push_back(flag("flashing-menu-cursor", s.flashing_cursor));
    args.push_back(flag("improved-cpu-tactics", s.improved_cpu_tactics));
    args.push_back(flag("mouse-cursor", s.mouse_cursor));
    args.push_back(flag("monitor-id", s.monitor_id));
    args.push_back(flag("stronger-goalkeeper-ai", s.stronger_goalkeeper_ai));

    if (s.screen_enhancement == 1 && s.imgui_mode == 1) args.push_back(flag("imgui-mode", 1));

    args.push_back(flag("season-year", s.season_year));
    // getCLI only sets these two in "normal" mode; "96-97" leaves the game's
    // own defaults alone.
    if (req.mode == "normal") {
        args.push_back(flag("career-crash-fix", std::string("1111111")));
        args.push_back(flag("always-points-per-win-3", 1));
    }

    return args;
}

std::string arguments_to_string(const std::vector<std::string> &args)
{
    std::ostringstream out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) out << ' ';
        if (args[i].find_first_of(" \t\"") != std::string::npos)
            out << '"' << args[i] << '"';
        else
            out << args[i];
    }
    return out.str();
}

bool write_game_ini(const LaunchRequest &req)
{
    const std::string path = path_join(req.swos_dir, "swos.ini");
    IniFile ini;
    ini.load(path);  // preserve every other section the game owns

    ini.write_int("video", "windowMode", req.settings.full_screen == 1 ? 2 : 1);

    int w = req.override_width;
    int h = req.override_height;
    if (w <= 0 || h <= 0) {
        w = 0;
        h = 0;
        if (!req.settings.resolve_resolution(w, h)) return false;
    }
    ini.write_int("video", "windowWidth", w);
    ini.write_int("video", "windowHeight", h);
    ini.write_int("video", "fullScreenWidth", w);
    ini.write_int("video", "fullScreenHeight", h);

    return ini.save(path);
}

LaunchResult run_game(const LaunchRequest &req)
{
    LaunchResult result;

    const std::string exe = path_join(req.swos_dir, game_executable_name());
    if (!file_exists(exe)) {
        result.error = "Game executable not found:\n" + exe;
        return result;
    }

    const std::vector<std::string> args = build_arguments(req);

#if defined(_WIN32)
    // CreateProcess wants one writable command-line buffer with argv[0] first.
    std::string command = "\"" + exe + "\" " + arguments_to_string(args);
    std::vector<char> buffer(command.begin(), command.end());
    buffer.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};

    if (!CreateProcessA(nullptr, buffer.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        req.swos_dir.c_str(), &si, &pi)) {
        result.error = "CreateProcess failed (error " + std::to_string(GetLastError()) + ")";
        return result;
    }
    result.started = true;
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exit_code = static_cast<int>(code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#else
    std::vector<char *> argv;
    std::string exe_copy = exe;
    argv.push_back(exe_copy.data());
    std::vector<std::string> owned = args;
    for (auto &a : owned) argv.push_back(a.data());
    argv.push_back(nullptr);

    // The game resolves its data relative to the working directory, so chdir
    // into swos_dir for the child and restore afterwards -- the same dance
    // runSWOS2020 did with SetCurrentDir.
    char cwd_before[4096] = {0};
    if (!getcwd(cwd_before, sizeof cwd_before)) cwd_before[0] = '\0';
    if (chdir(req.swos_dir.c_str()) != 0) {
        result.error = "Cannot change directory to " + req.swos_dir;
        return result;
    }

    pid_t pid = 0;
    const int rc = posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), environ);

    if (cwd_before[0]) {
        if (chdir(cwd_before) != 0)
            std::fprintf(stderr, "[launch] could not restore working directory\n");
    }

    if (rc != 0) {
        result.error = "posix_spawn failed: " + std::string(std::strerror(rc));
        return result;
    }
    result.started = true;

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        // interrupted by a signal -- keep waiting
    }
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif

    return result;
}

} // namespace swos
