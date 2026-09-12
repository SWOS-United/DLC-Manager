// game_launch.h -- turn Settings into a game command line and run it.
//
// build_arguments() is a port of TfrmMain.getCLI; write_game_ini() ports
// TfrmMain.SetupSWOSIni. The DLC plumbing getCLI also did (team database,
// commentary, sprite packs) is not here yet -- see docs/PORTING.md for what is
// still outstanding.

#pragma once

#include "settings.h"

#include <string>
#include <vector>

namespace swos {

struct LaunchRequest {
    Settings settings;
    std::string base_dir;   // launcher directory (gCurDir)
    std::string swos_dir;   // where the game binary and its data live
    // getCLI took a `mode` string; "normal" is the Run button, "96-97" the
    // retro variant that swaps in the 96-97 graphics and a fixed career mask.
    std::string mode = "normal";
    // Resolution override in effect for this run (0 = use the settings value).
    int override_width = 0;
    int override_height = 0;
};

struct LaunchResult {
    bool started = false;
    int exit_code = 0;
    std::string error;
};

// The argv the game is launched with, excluding argv[0].
std::vector<std::string> build_arguments(const LaunchRequest &req);

// Joins build_arguments() into a single display/debug string, quoting anything
// containing whitespace.
std::string arguments_to_string(const std::vector<std::string> &args);

// Writes <swos_dir>/swos.ini [video] so the game picks up the window mode and
// resolution even when it is started outside the launcher.
bool write_game_ini(const LaunchRequest &req);

// Runs the game and blocks until it exits, matching WinExecAndWait32. The
// caller is expected to hide its own window first.
LaunchResult run_game(const LaunchRequest &req);

} // namespace swos
