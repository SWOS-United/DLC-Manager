# SWOS Tools

A cross-platform C++ port of the Delphi **SWOS 2020** suite, built on SDL2,
OpenGL 3.3 core and [Slint](https://slint.dev). Targets Windows, macOS and
Linux, and is intended to fold into
[SWOS-United](https://github.com/benbaker76/SWOS-United) once it settles.

| binary | replaces | what it does |
|---|---|---|
| `swos-launcher` | `gameLauncher.dpr` | game settings and launching |
| `swos-dlc-manager` | `DLCManager.dpr` | browse, download and install DLC |

Both share `swos-core` and the same `ini/gameLauncher.ini`, so a pack selected
in the manager is the pack the launcher applies.

![General tab](docs/images/general-light.png)

## The launcher

* Reads and writes `ini/gameLauncher.ini` with the **exact** sections, keys and
  defaults the Delphi launcher used, so an existing install keeps working.
* Builds the game's command line (a flag-for-flag port of `getCLI`) and launches
  it, waiting for it to exit.
* Rewrites the game's `swos.ini` `[video]` section before launching.
* Enumerates real display modes from SDL, so every resolution the monitor
  supports is offered — not just the hard-coded list the Delphi build shipped.
* Windowed, borderless-fullscreen and exclusive-fullscreen, with Alt+Enter to
  toggle and a 100%–200% interface scale.
* Light / dark themes, defaulting to the desktop setting.

## The DLC manager

![DLC manager](docs/images/dlc-manager.png)

* Fetches all 17 DLC catalogues from `sensiblesoccer.de/swos2020`, merging them
  with what is installed to mark each pack New / Update / Installed / Removed.
* Downloads and extracts `.7z` packs in-process (libcurl + libarchive), on a
  worker thread so the UI keeps painting, with progress and cancel.
* Image previews for pitches, cups, sprites and backgrounds; audio previews for
  commentary, sound effects and music.
* Records the selection the launcher then applies.

See [docs/PORTING.md](docs/PORTING.md) for what is not ported yet — chiefly
applying an installed pack to the game directory, and the cup/DIY editor.

## Build

```bash
./scripts/fetch_slint.sh          # scripts\fetch_slint.ps1 on Windows
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Full per-platform instructions, including building Slint from source, are in
[docs/BUILDING.md](docs/BUILDING.md).

## Run

```
swos-launcher [options]

  --fullscreen[=desktop|exclusive]  start full screen (default: desktop)
  --windowed                        start windowed (overrides the saved setting)
  --display=N                       open on monitor N (0 = primary)
  --scale=F                         interface scale, e.g. 1.25
  --help
```

The launcher looks for the game in `swos-path.txt` next to the executable, or in
`./SWOS` if that file is absent.

## Layout

```
core/                shared library
  window.*             SDL2 window + GL 3.3 core context (ported from SWOS-United)
  slint_gl_platform.*  Slint custom platform -> software render -> GL texture
  settings.*           the Delphi TSetting record and its ini format
  game_launch.*        command-line construction and process spawn
  dlc_catalog.*        the DLC model, shared by both apps
  http.*               libcurl downloads, sync and async
  archive_extract.*    libarchive 7z extraction
  system_theme.*       desktop light/dark detection
apps/launcher/       swos-launcher + launcher.slint
apps/dlc_manager/    swos-dlc-manager + dlc_manager.slint + audio preview
assets/icons/        Tabler icons (MIT), vendored
docs/                architecture, porting and build notes
```

## Licence

The Slint runtime is used under its royalty-free desktop licence; see
`third_party/slint/licenses` after fetching. Icons are
[Tabler Icons](https://tabler.io/icons) (MIT).
