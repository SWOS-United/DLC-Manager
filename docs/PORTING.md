# Porting notes: Delphi suite -> C++

Source: `C:\Projects\GitHub\swos-game-launcher`, the Delphi/VCL SWOS 2020 suite.
It builds several executables from one tree over a set of shared units, and the
port keeps that shape: one repo, one CMake project, a shared `swos-core`, and an
app per executable.

| Delphi program | entry point | ported to |
|---|---|---|
| SWOS 2020 Game Launcher | `gameLauncher.dpr` + `miniLauncher_u.pas` (~8000 lines) | `swos-launcher`, since retired (below) |
| DLC Manager | `DLCManager.dpr` + `contents_downloader_u.pas` (~7800 lines) | `swos-dlc-manager` |
| Database Browser | `databaseBrowser.dpr` + `database_browser_u.pas` | not yet |
| Advanced Tactics Creator | `advanced_tactics_editor_p.dpr` | not yet |

`dlc_info_u.pas` is shared by the first two in the Delphi build — the launcher
applies the selected DLC before starting the game — so its port
(`core/dlc_catalog`) lives in the shared core rather than in either app.

### The launcher, retired

`swos-launcher` was ported first and then removed. Its options were SWOS 2020
command-line flags, which SWOS United does not read: SWOS United keeps its
settings in `SWOS.ini` and changes them from its own menus, which now cover
everything the launcher offered except two things SWOS United cannot take:
the HQx and xBRZ scalers (their licences) and super power CPU mode (switched
off in SWOS 2020's own code). The DLC Manager stays, because SWOS United has
no downloader of its own. The launcher's code is in the git history.

## Mapping

| Delphi | C++ |
|---|---|
| `TSetting` record | `swos::Settings` (`core/include/settings.h`) |
| `LoadIni` / `SaveIni` | `Settings::load` / `Settings::save` |
| `TIniFile` | `swos::IniFile` (`core/include/ini_file.h`) |
| `getSWOSDir` | `swos::swos_dir` (`core/src/paths.cpp`) |
| `gCurDir` | `swos::executable_dir()` |
| VCL form + `.dfm` | `apps/dlc_manager/dlc_manager.slint` |

## File format compatibility

`ini/gameLauncher.ini` is read and written with the **same sections, key
spellings and defaults** as the Delphi build, so an existing install keeps
working and the Delphi suite can share the file. Deliberate oddities inherited
from the original, which must not be "tidied up":

* `aspectRatio` is stored under the key **`aspectRatioEx`**.
* `careerFixOpt` has seven slots; `LoadIni` reads all seven but `SaveIni` only
  writes the first four, so the last three keep whatever is already in the file.
* A legacy `ini/miniLauncher.ini` is renamed to `gameLauncher.ini` on first run.
* The `theme` key named a VCL style (`Smokey Quartz Kamri`). It has no meaning
  here but is round-tripped so a shared file survives.

New keys added by this port, all under `[Launcher]`: `themeMode` (0 system,
1 light, 2 dark) and `activeTab`.

## DLC Manager

| Delphi | C++ |
|---|---|
| `TDLCInfoManager` | `swos::DlcCatalog` (`core/include/dlc_catalog.h`) |
| `loadEx` (CSV parse) | `DlcCatalog::parse_csv` |
| `addServerVersions` (status merge) | `DlcCatalog::merge` |
| `download` (Indy / WinINet / `TDownloadURL`) | `core/src/http.cpp` (libcurl) |
| `downloadDLC` shelling out to `temp/7z.exe` | `core/src/archive_extract.cpp` (libarchive) |
| `downloadDLC('_preview')` | the preview leg of `InstallJob` |
| `lvw*CustomDrawItem` `ImageIndex` chains | `swos::dlc_flag_index` |
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
`[DLC]` section of `ini/gameLauncher.ini` the Delphi launcher read. Note `bgi2`, not
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

The rest of the Delphi suite:

* **Cup/DIY editing** (`diy_edit_u.pas`, `tabCupTeams`, `tabCupDIYSetting`):
  reading and rewriting `.diy` competition files and `customs.edt`.
* **Custom SFX builder** (`custom_sfx_main_u.pas`, `custom_sfx_selector_u.pas`,
  `tabPreviewSFX`) — assembling a sound pack from individual samples.
* **Online version checks and the self-updater** (`timUpdater`, `updater_u.pas`).
* **Sibling applications**: `database_browser_u.pas`,
  `advanced_tactics_editor_u.pas`.
