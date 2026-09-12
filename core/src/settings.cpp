// settings.cpp -- see settings.h. Mirrors LoadIni / SaveIni in miniLauncher_u.pas.

#include "settings.h"

#include "ini_file.h"
#include "paths.h"

#include <cstdio>

namespace swos {

namespace {

constexpr const char *kGeneral = "General";
constexpr const char *kLauncher = "Launcher";
constexpr const char *kDlc = "DLC";

// The Delphi combo entries read "1920 x 1080". Parse leniently: any single 'x'
// with an integer either side, spaces optional.
bool parse_resolution(const std::string &text, int &w, int &h)
{
    const auto x = text.find_first_of("xX");
    if (x == std::string::npos) return false;
    try {
        size_t used = 0;
        const int pw = std::stoi(text.substr(0, x), &used);
        const int ph = std::stoi(text.substr(x + 1));
        if (pw <= 0 || ph <= 0) return false;
        w = pw;
        h = ph;
        return true;
    } catch (...) {
        return false;
    }
}

template <size_t N>
std::vector<std::string> to_vector(const char *const (&arr)[N])
{
    return std::vector<std::string>(arr, arr + N);
}

} // namespace

std::string Settings::ini_path(const std::string &base_dir)
{
    return path_join(path_join(base_dir, "ini"), "gameLauncher.ini");
}

bool Settings::load(const std::string &base_dir)
{
    // LoadIni renames a legacy miniLauncher.ini to gameLauncher.ini first; keep
    // doing that so an old install upgrades in place.
    const std::string legacy = path_join(path_join(base_dir, "ini"), "miniLauncher.ini");
    const std::string current = ini_path(base_dir);
    if (file_exists(legacy) && !file_exists(current)) rename_file(legacy, current);

    IniFile ini;
    const bool existed = ini.load(current);

    game_style = ini.read_int(kGeneral, "gameStyle", 0);
    colour_palette = ini.read_int(kGeneral, "colPal", 0);
    improved_cpu_tactics = ini.read_int(kGeneral, "improvedCPUTactics", 0);

    screen_mode = ini.read_int(kGeneral, "screenMode", 0);
    res_original_pc = ini.read_int(kGeneral, "scrResOriginalPCScreen", 0);
    res_original_amiga = ini.read_int(kGeneral, "scrResOriginalAmigaScreen", 0);
    res_widescreen = ini.read_int(kGeneral, "scrResWidescreen", 0);
    full_screen = ini.read_int(kGeneral, "fullScreen", 0);
    aspect_ratio = ini.read_int(kGeneral, "aspectRatioEx", 1);
    auto_centering = ini.read_int(kGeneral, "autoCentering", 0);

    screen_enhancement = ini.read_int(kGeneral, "screenEnhancement", 0);
    screen_filter = ini.read_int(kGeneral, "screenFilter", 0);
    shader_program = ini.read_int(kGeneral, "shaderProgram", 0);
    custom_pitch = ini.read_int(kGeneral, "customPitch", 0);

    scale_quality = ini.read_int(kGeneral, "scaleQuality", 0);
    menu_bg_image = ini.read_int(kGeneral, "menuBGImage", 0);
    menu_bg_image_s = ini.read_string(kGeneral, "menuBGImageS", "Auto");

    skip_intro = ini.read_int(kGeneral, "skipIntro", 1);
    skip_reel = ini.read_int(kGeneral, "skipReel", 1);
    no_load_pause = ini.read_int(kGeneral, "noLoadPause", 0);

    menu_style = ini.read_int(kGeneral, "menuStyle", 0);
    new_scoreboard = ini.read_int(kGeneral, "newScoreboard", 1);
    alt_images = ini.read_int(kGeneral, "altImages", 0);
    alpha_blending_menu = ini.read_int(kGeneral, "alphaBlendingMenu", 0);
    alpha_blending_menu_percent = ini.read_int(kGeneral, "alphaBlendingMenuPercent", 9);
    flashing_cursor = ini.read_int(kGeneral, "flashingCursor", 0);
    mouse_cursor = ini.read_int(kGeneral, "mouseCursor", 0);
    new_show_result = ini.read_int(kGeneral, "newShowResult", 0);
    remove_darken_banner = ini.read_int(kGeneral, "removeDarkenBanner", 0);
    show_fps = ini.read_int(kGeneral, "showFPS", 0);
    improved_pitch_palette = ini.read_int(kGeneral, "improvedPitchPalette", 1);
    ball_trails = ini.read_int(kGeneral, "ballTrails", 0);

    season_year = ini.read_string(kGeneral, "seasonYear", "1996");
    career_crash_fix = ini.read_int(kGeneral, "careerCrashFix", 1);
    for (int i = 0; i < 7; ++i)
        career_fix_opt[static_cast<size_t>(i)] =
                ini.read_int(kGeneral, "careerFixOpt" + std::to_string(i), 1);

    monitor_id = ini.read_int(kGeneral, "monitorID", 0);
    monitor_id2 = ini.read_int(kGeneral, "monitorID2", 0);

    window_scale = ini.read_int(kLauncher, "windowScale", 0);
    auto_check_db_version = ini.read_int(kLauncher, "autoCheckDBVersion", 1);
    auto_close_launcher = ini.read_int(kLauncher, "autoCloseLauncher", 0);
    theme_mode = ini.read_int(kLauncher, "themeMode", 0);
    active_tab = ini.read_int(kLauncher, "activeTab", 0);

    super_power_cpu_mode = ini.read_int(kGeneral, "superPowerCPUMode", 0);
    imgui_mode = ini.read_int(kGeneral, "imGuiMode", 0);
    dev_team_db_update = ini.read_int(kGeneral, "devTeamDBUpdate", 0);
    stronger_goalkeeper_ai = ini.read_int(kGeneral, "strongerGoalkeeperAI", 0);

    maximize_dlc_window = ini.read_int(kDlc, "maximizeDLCWindow", 1);
    randomize_diy = ini.read_int(kDlc, "randomizeDIY", 1);

    theme = ini.read_string(kGeneral, "theme", "Smokey Quartz Kamri");
    return existed;
}

bool Settings::save(const std::string &base_dir) const
{
    const std::string path = ini_path(base_dir);
    if (!ensure_directory(parent_path(path))) {
        std::fprintf(stderr, "[settings] cannot create %s\n", parent_path(path).c_str());
        return false;
    }

    // Load first so comments and any keys we do not model survive the rewrite.
    IniFile ini;
    ini.load(path);

    ini.write_int(kGeneral, "gameStyle", game_style);
    ini.write_int(kGeneral, "colPal", colour_palette);
    ini.write_int(kGeneral, "improvedCPUTactics", improved_cpu_tactics);

    ini.write_int(kGeneral, "screenMode", screen_mode);
    ini.write_int(kGeneral, "scrResOriginalPCScreen", res_original_pc);
    ini.write_int(kGeneral, "scrResOriginalAmigaScreen", res_original_amiga);
    ini.write_int(kGeneral, "scrResWidescreen", res_widescreen);
    ini.write_int(kGeneral, "fullScreen", full_screen);
    ini.write_int(kGeneral, "aspectRatioEx", aspect_ratio);
    ini.write_int(kGeneral, "autoCentering", auto_centering);

    ini.write_int(kGeneral, "screenEnhancement", screen_enhancement);
    ini.write_int(kGeneral, "screenFilter", screen_filter);
    ini.write_int(kGeneral, "shaderProgram", shader_program);
    ini.write_int(kGeneral, "customPitch", custom_pitch);

    ini.write_int(kGeneral, "scaleQuality", scale_quality);
    ini.write_int(kGeneral, "menuBGImage", menu_bg_image);
    ini.write_string(kGeneral, "menuBGImageS", menu_bg_image_s);

    ini.write_int(kGeneral, "skipIntro", skip_intro);
    ini.write_int(kGeneral, "skipReel", skip_reel);
    ini.write_int(kGeneral, "noLoadPause", no_load_pause);

    ini.write_int(kGeneral, "menuStyle", menu_style);
    ini.write_int(kGeneral, "newScoreboard", new_scoreboard);
    ini.write_int(kGeneral, "altImages", alt_images);
    ini.write_int(kGeneral, "alphaBlendingMenu", alpha_blending_menu);
    ini.write_int(kGeneral, "alphaBlendingMenuPercent", alpha_blending_menu_percent);
    ini.write_int(kGeneral, "flashingCursor", flashing_cursor);
    ini.write_int(kGeneral, "mouseCursor", mouse_cursor);
    ini.write_int(kGeneral, "newShowResult", new_show_result);
    ini.write_int(kGeneral, "removeDarkenBanner", remove_darken_banner);
    ini.write_int(kGeneral, "showFPS", show_fps);
    ini.write_int(kGeneral, "improvedPitchPalette", improved_pitch_palette);
    ini.write_int(kGeneral, "ballTrails", ball_trails);

    ini.write_string(kGeneral, "seasonYear", season_year);
    ini.write_int(kGeneral, "careerCrashFix", career_crash_fix);
    // SaveIni only ever wrote back the first four; the remaining three keep
    // whatever the file already held.
    for (int i = 0; i < 4; ++i)
        ini.write_int(kGeneral, "careerFixOpt" + std::to_string(i),
                      career_fix_opt[static_cast<size_t>(i)]);

    ini.write_int(kGeneral, "monitorID", monitor_id);
    ini.write_int(kGeneral, "monitorID2", monitor_id2);

    ini.write_int(kLauncher, "windowScale", window_scale);
    ini.write_int(kLauncher, "autoCheckDBVersion", auto_check_db_version);
    ini.write_int(kLauncher, "autoCloseLauncher", auto_close_launcher);
    ini.write_int(kLauncher, "themeMode", theme_mode);
    ini.write_int(kLauncher, "activeTab", active_tab);

    ini.write_int(kGeneral, "superPowerCPUMode", super_power_cpu_mode);
    ini.write_int(kGeneral, "imGuiMode", imgui_mode);
    ini.write_int(kGeneral, "devTeamDBUpdate", dev_team_db_update);
    ini.write_int(kGeneral, "strongerGoalkeeperAI", stronger_goalkeeper_ai);

    ini.write_int(kDlc, "maximizeDLCWindow", maximize_dlc_window);
    ini.write_int(kDlc, "randomizeDIY", randomize_diy);

    ini.write_string(kGeneral, "theme", theme);
    return ini.save(path);
}

std::vector<std::string> Settings::resolution_list() const
{
    switch (screen_mode) {
    case 0: return to_vector(options::kResOriginalPc);
    case 1: return to_vector(options::kResOriginalAmiga);
    default: return to_vector(options::kResWidescreen);
    }
}

int Settings::resolution_index() const
{
    switch (screen_mode) {
    case 0: return res_original_pc;
    case 1: return res_original_amiga;
    default: return res_widescreen;
    }
}

void Settings::set_resolution_index(int index)
{
    switch (screen_mode) {
    case 0: res_original_pc = index; break;
    case 1: res_original_amiga = index; break;
    default: res_widescreen = index; break;
    }
}

bool Settings::resolve_resolution(int &w, int &h) const
{
    const auto list = resolution_list();
    const int index = resolution_index();
    if (index < 0 || static_cast<size_t>(index) >= list.size()) return false;
    return parse_resolution(list[static_cast<size_t>(index)], w, h);
}

} // namespace swos
