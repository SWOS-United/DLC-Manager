# SWOS United DLC Manager

Browse, download and turn on DLC for
[SWOS United](https://github.com/benbaker76/SWOS-United): pitches, commentary,
sound effects, music, team databases and team packs, including everything SWOS
2020 offered. A cross-platform C++ port of the Delphi **SWOS 2020** DLC Manager
(`DLCManager.dpr`), built on SDL2, OpenGL 3.3 core and
[Slint](https://slint.dev), for Windows, macOS and Linux.

SWOS 2020's game launcher was ported here too and has since been retired: SWOS
United's own menus and `SWOS.ini` cover its options. See
[docs/PORTING.md](docs/PORTING.md).

## The DLC manager

![SWOS United DLC Manager](docs/images/dlc-manager.png)

<sub>Light and dark follow the desktop by default.</sub>

![SWOS United DLC Manager in dark mode](docs/images/dlc-manager-dark.png)

* Fetches all 17 DLC catalogues from `sensiblesoccer.de/swos2020`, merging them
  with what is installed to mark each pack New / Update / Installed / Removed.
* Downloads and extracts `.7z` packs in-process (libcurl + libarchive), on a
  worker thread so the UI keeps painting, with progress and cancel.
* Image previews for pitches, cups, sprites and backgrounds; audio previews for
  commentary, sound effects and music. The artwork comes from the pack's own
  `_preview` archive, fetched alongside it.
* Country flags in the Commentary and Cup lists, from the original's own icons.

### SWOS United mods

The manager lists SWOS United's mods, from
`http://www.sensiblesoccer.de/swos2020/dlc/swosutd/index.json` unless
`[DLC] modIndexUrl` in `ini/gameLauncher.ini` (or `$SWOS_MOD_INDEX` for one run;
`file://` works for testing) names another. `modIndexUrl = off` shows SWOS
2020's own packs instead. The index is OpenSWOS's
`index.json` (its `docs/MODS.md` §10.1). Each mod is downloaded as a zip,
checked against the index's sha256, and unpacked into the user folder's `mods/`.
**Turn on** adds it to `SWOS.ini`'s `[mods] enabled`, turns off the other mods of
its slot (one pitch, one commentary, …), and makes the setting the game needs
to use it: `[graphics] style = 2020` for a pitch, `[audio] style = pc` for
commentary and effects, `[audio] music = pc` for music, with `title_music` / `menu_music` set to `mp3`, or
`midi` for a PC DOS song.

### The Amiga

`amiga/dlc_manager` is the same manager for SWOS United on the Amiga (AmigaOS
2.04+, any `bsdsocket.library` stack): it reads
`http://www.sensiblesoccer.de/swos2020/dlc/swosutd/amiga/index.json`, installs into
the game drawer's `mods/` and turns mods on in `data/MODS`. Its mods are made by
`scripts/dlc2020_to_amiga.py`: SWOS 2020's pitches as AGA maps, and the team
databases with 96/97's leagues. See [amiga/dlc_manager/README.md](amiga/dlc_manager/README.md).

`scripts/dlc2020_to_mods.py` turns SWOS 2020's server packs into those mods and
writes the index. Each mod's uuid and folder name are kept in
`scripts/dlc2020_uuids.json`, so commit it after a conversion.

See [docs/PORTING.md](docs/PORTING.md) for what of the Delphi suite is not
ported, such as the cup/DIY editor.

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
swos-dlc-manager
```

It has no options. Mods go into the user folder below, where SWOS United finds
them.

## Where your files live

Settings and downloaded mods go in SWOS United's player folder, so the game
reads what the manager installs:

| | |
|---|---|
| Windows | `%USERPROFILE%\Documents\SWOSUnited` |
| macOS | `~/Library/Application Support/SWOSUnited` |
| Linux | `~/.swosunited` |

For OpenSWOS's own game (`SWOS` / `.swos`), configure with
`-DSWOS_USER_DIR=SWOS -DSWOS_USER_DOT_DIR=.swos`.

Inside it, `ini/`, `dlc/` and `temp/` keep the names the Delphi build used.

## Layout

```
core/                swos-core library
  window.*             SDL2 window + GL 3.3 core context (ported from SWOS-United)
  slint_gl_platform.*  Slint custom platform -> software render -> GL texture
  settings.*           the Delphi TSetting record and its ini format
  dlc_catalog.*        the DLC model
  mod_store.*          SWOS United mods: the index, mods/, SWOS.ini [mods]
  json.*, sha256.*     small readers for the index and download checks
  http.*               libcurl downloads, sync and async
  archive_extract.*    libarchive 7z extraction
  system_theme.*       desktop light/dark detection
apps/dlc_manager/    swos-dlc-manager + dlc_manager.slint + audio preview
assets/icons/        Tabler icons (MIT), vendored
docs/                architecture, porting and build notes
```

## Licence

The Slint runtime is used under its royalty-free desktop licence; see
`third_party/slint/licenses` after fetching. Icons are
[Tabler Icons](https://tabler.io/icons) (MIT).
