// settings.h -- the launcher's persisted settings.
//
// A direct port of TSetting in miniLauncher_u.pas, keeping every ini section,
// key spelling and default value the Delphi launcher used so an existing
// ini/gameLauncher.ini keeps working unchanged. Two spellings are deliberate
// oddities inherited from the original and must not be "fixed":
//   * aspect_ratio is stored under the key "aspectRatioEx"
//   * career_fix_opt has seven slots but only the first four are written back
//
// The option tables below are the combo-box contents lifted from
// miniLauncher_u.dfm; index values are what both the ini and the game's command
// line carry, so their order is part of the file format.

#pragma once

#include <array>
#include <string>
#include <vector>

namespace swos {

// --- Option tables ---------------------------------------------------------
namespace options {

inline constexpr const char *kScreenMode[] = {"PC DOS Screen", "Amiga Screen", "Widescreen"};
inline constexpr const char *kGameStyle[] = {"PC DOS", "Amiga", "Moon Soccer"};
inline constexpr const char *kImprovedCpuTactics[] = {"Normal", "Expert"};
inline constexpr const char *kScreenEnhancement[] = {"Pixel Scaler", "Shader Program"};
inline constexpr const char *kScreenFilter[] = {"None",  "Simple2x", "Scale2x", "Scale3x", "LQ2x",
                                                "LQ3x",  "HQ2x",     "HQ3x",    "2xBRZ",   "3xBRZ"};
inline constexpr const char *kShaderProgram[] = {"None", "CRT-Simple", "CRT-Lottes", "CRT-Geom"};
inline constexpr const char *kNewScoreboard[] = {"Disabled", "Left-Top", "Right-Top", "Left-Bottom",
                                                 "Right-Bottom"};
inline constexpr const char *kFlashingCursor[] = {"Original", "No Flashing"};
inline constexpr const char *kMouseCursor[] = {"Windows", "Amiga Workbench 2.04",
                                               "Amiga Workbench 1.X"};
inline constexpr const char *kMenuStyle[] = {"Amiga", "PC DOS"};
inline constexpr const char *kMenuBgImage[] = {"Auto", "PC DOS", "Amiga", "Random"};
inline constexpr const char *kColourPalette[] = {"Original", "Purple instead of Brown"};
inline constexpr const char *kStrongerGoalkeeperAi[] = {"Original", "Stronger"};
inline constexpr const char *kScaleQuality[] = {"None", "Blur"};

// The Delphi launcher hard-coded one resolution list per screen mode. They are
// kept as the *suggested* entries; the UI also offers every mode the display
// actually reports (see Window::enumerate_displays).
inline constexpr const char *kResOriginalPc[] = {"640 x 400", "960 x 600", "1280 x 800",
                                                 "4096 x 2160"};
inline constexpr const char *kResOriginalAmiga[] = {"672 x 544", "1008 x 816", "1344 x 1088",
                                                    "4096 x 2160"};
inline constexpr const char *kResWidescreen[] = {
        "960 x 540",   "1200 x 675",  "1280 x 720",  "1366 x 768",
        "1440 x 810",  "1920 x 1080", "2400 x 1350", "2880 x 1620",
        "3360 x 1890", "3840 x 2160", "4096 x 2160", "1511 x 850"};

// alphaBlendingMenuPercent is an index into 0,5,10..100.
inline int alpha_percent_for_index(int index) { return index * 5; }
inline int alpha_index_for_percent(int percent) { return percent / 5; }

} // namespace options

// Matches TSetting field for field. Defaults are the ones LoadIni passes to
// TIniFile.ReadInteger / ReadString.
struct Settings {
    // [General] -- gameplay
    int game_style = 0;
    int colour_palette = 0;
    int improved_cpu_tactics = 0;
    int stronger_goalkeeper_ai = 0;
    int super_power_cpu_mode = 0;
    std::string season_year = "1996";
    int career_crash_fix = 1;
    std::array<int, 7> career_fix_opt = {1, 1, 1, 1, 1, 1, 1};

    // [General] -- video
    int screen_mode = 0;
    int res_original_pc = 0;
    int res_original_amiga = 0;
    int res_widescreen = 0;
    int full_screen = 0;
    int aspect_ratio = 1;   // stored as "aspectRatioEx"
    int auto_centering = 0;
    int screen_enhancement = 0;
    int screen_filter = 0;
    int shader_program = 0;
    int scale_quality = 0;
    int monitor_id = 0;
    int monitor_id2 = 0;

    // [General] -- presentation
    int menu_bg_image = 0;
    std::string menu_bg_image_s = "Auto";
    int menu_style = 0;
    int new_scoreboard = 1;
    int alt_images = 0;
    int alpha_blending_menu = 0;
    int alpha_blending_menu_percent = 9;  // index -> 45%
    int flashing_cursor = 0;
    int mouse_cursor = 0;
    int new_show_result = 0;
    int remove_darken_banner = 0;
    int show_fps = 0;
    int improved_pitch_palette = 1;
    int ball_trails = 0;
    int custom_pitch = 0;

    // [General] -- startup
    int skip_intro = 1;
    int skip_reel = 1;
    int no_load_pause = 0;

    // [General] -- developer
    int imgui_mode = 0;
    int dev_team_db_update = 0;

    // [Launcher]
    int window_scale = 0;  // index into 100/125/150/175/200 %
    int auto_check_db_version = 1;
    int auto_close_launcher = 0;
    // 0 = follow the desktop, 1 = light, 2 = dark. New in the C++ port: the
    // Delphi `theme` key below named a VCL style and has no equivalent here,
    // but it is still round-tripped so an ini shared with the old launcher
    // keeps working.
    int theme_mode = 0;
    int active_tab = 0;

    // [DLC]
    int maximize_dlc_window = 1;
    int randomize_diy = 1;

    std::string theme = "Smokey Quartz Kamri";

    // --- Persistence -----------------------------------------------------
    // Reads ini/gameLauncher.ini under `base_dir`. Missing keys keep their
    // defaults, so a missing or partial file is fine.
    bool load(const std::string &base_dir);
    bool save(const std::string &base_dir) const;

    // Path this instance loads from / saves to.
    static std::string ini_path(const std::string &base_dir);

    // --- Derived ---------------------------------------------------------
    // The windowed UI zoom as a multiplier (window_scale index -> 1.00 .. 2.00).
    float ui_scale() const { return 1.0f + 0.25f * static_cast<float>(window_scale); }
    void set_ui_scale_index(int index) { window_scale = index; }

    // Resolution the game should run at, resolved from screen_mode plus the
    // matching per-mode index. Returns false when the stored entry does not
    // parse, in which case w/h are left untouched.
    bool resolve_resolution(int &w, int &h) const;

    // The resolution list that screen_mode selects.
    std::vector<std::string> resolution_list() const;
    int resolution_index() const;
    void set_resolution_index(int index);
};

} // namespace swos
