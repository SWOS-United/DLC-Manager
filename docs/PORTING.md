# Porting notes: Delphi launcher -> C++

Source: `C:\Projects\GitHub\swos-game-launcher`, the Delphi/VCL "SWOS 2020 Game
Launcher" (`gameLauncher.dpr` + `miniLauncher_u.pas`, ~8000 lines, plus a 17 MB
`.dfm` whose bulk is embedded PNGs).

## Mapping

| Delphi | C++ |
|---|---|
| `TSetting` record | `swos::Settings` (`include/settings.h`) |
| `LoadIni` / `SaveIni` | `Settings::load` / `Settings::save` |
| `TIniFile` | `swos::IniFile` (`include/ini_file.h`) |
| `getCLI` | `swos::build_arguments` (`src/game_launch.cpp`) |
| `SetupSWOSIni` | `swos::write_game_ini` |
| `WinExecAndWait32` | `swos::run_game` (CreateProcess / posix_spawn) |
| `getSWOSDir` | `swos::swos_dir` (`src/paths.cpp`) |
| `SetUI` enable/visible rules | `if` / `enabled:` bindings in `ui/launcher.slint` |
| `gCurDir` | `swos::executable_dir()` |
| VCL form + `.dfm` | `ui/launcher.slint` |

## File format compatibility

`ini/gameLauncher.ini` is read and written with the **same sections, key
spellings and defaults** as the Delphi build, so an existing install keeps
working and the two launchers can share a file. Deliberate oddities inherited
from the original, which must not be "tidied up":

* `aspectRatio` is stored under the key **`aspectRatioEx`**.
* `careerFixOpt` has seven slots; `LoadIni` reads all seven but `SaveIni` only
  writes the first four, so the last three keep whatever is already in the file.
* A legacy `ini/miniLauncher.ini` is renamed to `gameLauncher.ini` on first run.
* The `theme` key named a VCL style (`Smokey Quartz Kamri`). It has no meaning
  here but is round-tripped so a shared file survives.

New keys added by this port, all under `[Launcher]`: `themeMode` (0 system,
1 light, 2 dark) and `activeTab`.

The game's `swos.ini` `[video]` section is rewritten on launch exactly as
`SetupSWOSIni` did, and every other section in that file is preserved.

## Command line

`build_arguments` reproduces `getCLI` flag for flag for the settings that are
ported. Verified against the Delphi output for default settings — identical.

Two conditional branches in `getCLI` are reproduced rather than simplified, since
they look like bugs but are what the game expects:

* `--shader` is derived from **`screenFilter`**, not `scaleQuality` (the
  `scaleQuality` branch beside it was commented out in the original).
* `--menu-background-image` and `--alpha-blending-menu` are forced off unless
  the screen mode is Widescreen, matching the `cmbMenuBGImage.Enabled` guard in
  `SetUI`.

## Not ported yet

The Delphi tree is a suite; this port covers the launcher's settings surface and
the launch path. Still outstanding, roughly in order of how much they change the
architecture:

* **`--vsync=1`** — `getCLI` adds it when the monitor is not 60 Hz. The refresh
  rate is already available from `Window::enumerate_displays()`; this is a
  few lines and should be done next.
* **Team database tab** (`lvwTeamDB`, `btnCheckDBVersion`, `dlc_info_u.pas`) —
  needs HTTP, which the port has no dependency for yet.
* **DLC tab** (`lvwDLC`, pitch / commentary / SFX / sprite packs) — the biggest
  chunk. `runSWOS2020` applies each pack by copying files into the game
  directory before launch; none of that is here.
* **Online version checks and the self-updater** (`timUpdater`, `updater_u.pas`).
* **Sibling applications**: `contents_downloader_u.pas` (DLC Manager),
  `database_browser_u.pas`, `advanced_tactics_editor_u.pas`.
* **Season-year combo** — the Delphi form listed 1994–2100; the port uses a
  plain numeric field. Functionally equivalent, visually different.
* **Custom pitch, `altImages`, `scaleQuality`, `monitorID2`, `customPitch`** —
  round-tripped in the ini but not yet surfaced in the UI.

## Resolution lists

The Delphi launcher hard-coded one resolution list per screen mode. Those lists
are kept verbatim as the leading entries so existing ini indices still mean the
same thing, and every mode the selected monitor actually reports is appended
(`resolution_labels` in `src/main.cpp`). That is what "support all resolutions"
needs without breaking the file format.

## Game executable

The Delphi launcher ran `swos-port-Release.exe`. This port looks for
`SWOS.exe` / `SWOS`, which is what SWOS-United's Makefile produces. See
`swos::game_executable_name()`.
