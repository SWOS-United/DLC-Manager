# Porting notes: Delphi suite -> C++

Source: `C:\Projects\GitHub\swos-game-launcher`, the Delphi/VCL SWOS 2020 suite.
It builds several executables from one tree over a set of shared units, and the
port keeps that shape: one repo, one CMake project, a shared `swos-core`, and an
app per executable.

| Delphi program | entry point | ported to |
|---|---|---|
| SWOS 2020 Game Launcher | `gameLauncher.dpr` + `miniLauncher_u.pas` (~8000 lines) | `swos-launcher` |
| DLC Manager | `DLCManager.dpr` + `contents_downloader_u.pas` (~7800 lines) | `swos-dlc-manager` |
| Database Browser | `databaseBrowser.dpr` + `database_browser_u.pas` | not yet |
| Advanced Tactics Creator | `advanced_tactics_editor_p.dpr` | not yet |

`dlc_info_u.pas` is shared by the first two in the Delphi build — the launcher
applies the selected DLC before starting the game — so its port
(`core/dlc_catalog`) lives in the shared core rather than in either app.

## Mapping

| Delphi | C++ |
|---|---|
| `TSetting` record | `swos::Settings` (`core/include/settings.h`) |
| `LoadIni` / `SaveIni` | `Settings::load` / `Settings::save` |
| `TIniFile` | `swos::IniFile` (`core/include/ini_file.h`) |
| `getCLI` | `swos::build_arguments` (`core/src/game_launch.cpp`) |
| `SetupSWOSIni` | `swos::write_game_ini` |
| `WinExecAndWait32` | `swos::run_game` (CreateProcess / posix_spawn) |
| `getSWOSDir` | `swos::swos_dir` (`core/src/paths.cpp`) |
| `SetUI` enable/visible rules | `if` / `enabled:` bindings in `ui/launcher.slint` |
| `gCurDir` | `swos::executable_dir()` |
| VCL form + `.dfm` | `apps/launcher/launcher.slint` |

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

## DLC Manager

| Delphi | C++ |
|---|---|
| `TDLCInfoManager` | `swos::DlcCatalog` (`core/include/dlc_catalog.h`) |
| `loadEx` (CSV parse) | `DlcCatalog::parse_csv` |
| `addServerVersions` (status merge) | `DlcCatalog::merge` |
| `download` (Indy / WinINet / `TDownloadURL`) | `core/src/http.cpp` (libcurl) |
| `downloadDLC` shelling out to `temp/7z.exe` | `core/src/archive_extract.cpp` (libarchive) |
| `loadCurrentFromIni` / `saveCurrentToIni` | `DlcCatalog::load_selection` / `save_selection` |
| `preview_audio_commentary_u` / `preview_audio_fx_u` (MCI) | `apps/dlc_manager/audio_preview.cpp` (SDL2_mixer) |
| 17 near-identical tabs | one list/preview layout, type chosen in a sidebar |

### Protocol and layout, unchanged

```
http://www.sensiblesoccer.de/swos2020/dlc/<type>/<type>-info.csv
http://www.sensiblesoccer.de/swos2020/dlc/<type>/<name>.7z
<base>/temp/<type>-info.csv         the catalogue just fetched
<base>/dlc/<type>/<type>-info.csv   what is installed
<base>/dlc/<type>/<name>/           its extracted contents
```

The 17 type ids, the per-type default selections and the `<type>_Enabled` flags
are exactly what `loadCurrentFromIni` used, and the selections live in the same
`[DLC]` section of `ini/gameLauncher.ini` the launcher reads. Note `bgi2`, not
`bgi`: the Delphi code moved background images to a second-generation layout and
the directory name went with it.

### Things that changed on purpose

* **Catalogue payloads are validated.** The server answers a missing file with a
  301 to its homepage, which then returns **200 and 247 KB of HTML**. Every
  `sprite_*` catalogue does this today. `CURLOPT_FAILONERROR` cannot see it, and
  the Delphi guard (is the file non-empty?) passes happily — so that page parsed
  into 4363 nonsense "packs" per sprite type before `DlcCatalog::looks_like_catalog`
  was added. Types whose catalogue fails are now named in the status bar and keep
  whatever is installed locally.
* **Archive entry paths are sanitised.** `extract_archive` rejects absolute
  paths and any component that would escape the destination. The Delphi build
  handed the archive to `7z.exe` with no such guard.
* **Downloads are atomic.** The body goes to `<file>.part` and is renamed on
  success, so an interrupted transfer cannot be mistaken for a complete file.
  The Delphi code checked for a zero-length result after the fact instead.
* **Extraction failure is reported.** `WinExecAndWait32`'s exit code was
  discarded, so a corrupt download extracted to nothing and the manager carried
  on as if it had worked.
* **Packs the server has dropped are listed** as `Removed` rather than being
  invisible. The Delphi merge walked the server list only, so an installed pack
  that vanished upstream could not be removed from the UI — even though the code
  already had a `Removed` status for exactly that case.

### Known quirk, kept

The default FX selection is `Original 96/97` (with a slash) but the server
publishes that pack as `Original 96_97` (with an underscore), so on a fresh
install the "Applied" line names something not in the list. That mismatch is in
the Delphi defaults too. It is left alone because the string is what reaches the
game, and changing it is a gameplay decision rather than a porting one.

## Not ported yet

The Delphi tree is a suite; this port covers the launcher's settings surface and
the launch path. Still outstanding, roughly in order of how much they change the
architecture:

* **`--vsync=1`** — `getCLI` adds it when the monitor is not 60 Hz. The refresh
  rate is already available from `Window::enumerate_displays()`; this is a
  few lines and should be done next.
* **Applying a pack to the game directory.** The DLC manager installs packs and
  records the selection, but `runSWOS2020`'s `applyDLC` step — copying the
  chosen pitch, commentary, SFX, music and sprite files into the SWOS folder
  before launch — is not ported. Until it is, the launcher's `--teamdb-dir` and
  friends are not passed either. **This is the next thing to do.**
* **Cup/DIY editing** (`diy_edit_u.pas`, `tabCupTeams`, `tabCupDIYSetting`):
  reading and rewriting `.diy` competition files and `customs.edt`.
* **Custom SFX builder** (`custom_sfx_main_u.pas`, `custom_sfx_selector_u.pas`,
  `tabPreviewSFX`) — assembling a sound pack from individual samples.
* **Random pitch selection** (`pitch_MakeRandom*` keys are round-tripped in the
  ini but nothing writes them yet).
* **Online version checks and the self-updater** (`timUpdater`, `updater_u.pas`).
* **Sibling applications**: `database_browser_u.pas`,
  `advanced_tactics_editor_u.pas`.
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
