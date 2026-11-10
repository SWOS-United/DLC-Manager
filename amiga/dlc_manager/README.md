# SWOS United DLC Manager — Amiga

An Intuition + GadTools take on `swos-dlc-manager` for AmigaOS 2.04 or later,
on the Workbench screen. It installs SWOS United Amiga's mods into the game's
drawer and turns them on.

```
+- SWOS United DLC Manager -------------------------------------------+
| Category           Mods                                             |
| +----------------+ +----------------------------------------------+ |
| |Pitch (3 new)   | |[NEW] 1994 - Moon Soccer                      | |
| |Team Database(8)| |[ ON] 2018 - Rugby                            | |
| |                | |[ OK] 2026 - Futsal Retro HD                  | |
| +----------------+ +----------------------------------------------+ |
|  Author [Sensible Software and starwindz                          ] |
| Version [1.0          ]  Status [Installed, on                    ] |
|    Info [SWOS 2020's pitch, for an AGA Amiga: set OPTIONS > PITC  ] |
|  In use [2018 - Rugby                                             ] |
| [Refresh] [Download] [Remove] [Turn on/off] [Quit]                  |
| [Installed 2018 - Rugby - Turn on to use it        ] [#####       ]  |
+---------------------------------------------------------------------+
```

## What it does

| | |
|---|---|
| **Refresh** | Reads `http://www.sensiblesoccer.de/swos2020/dlc/swosutd/amiga/index.json` (OpenSWOS's `openswos-mod-index` v1; only mods whose `targets` include `amiga`) over `bsdsocket.library`, and merges it with what is installed: New / Installed / Update / No longer offered, and whether each is on. |
| **Download** | Streams the mod's zip into `mods/`, checks its size and SHA-256 against the index, unpacks it (stored or deflated entries, each CRC-checked; no name may leave the drawer or be longer than AmigaDOS's 30 characters) into `mods/.<slug>`, checks its `mod.json` uuid, then puts it in place of any older copy. Esc cancels. |
| **Turn on/off** | Edits `data/MODS`, which the game reads at its first file read: the mod is added (with `*` when it is not cosmetic, as OpenSWOS Setup writes it) and the others of its slot taken out. Lines it did not write, such as the setup's built-in mods, stay where they are. |
| **Remove** | Turns it off and deletes `mods/<slug>`. |
| **About** | Credits, the game drawer and the network stack. |

The game drawer is this program's own drawer when it has a `data/` drawer, or
the `data/` of a WHDLoad package (where the game's `mods/` and `data/MODS`
are). Otherwise *Project → Game drawer...* asks, and the choice is kept in
`PROGDIR:SWOS-DLC.prefs`.

The mods are made by `scripts/dlc2020_to_amiga.py` from SWOS 2020's packs:
its pitches as AGA maps (`grafs_aga/P2020_n.MAP`; set OPTIONS → PITCHES to 2020),
and the team databases that have 96/97's leagues exactly, as their binary team
files.

From the Shell, `SWOS-DLC INDEX=<url>` reads another index, and `TEST` refreshes,
installs and turns on the first new mod of each category, writes each status
line to `PROGDIR:SWOS-DLC.test`, and quits (how it is tested in FS-UAE).

Memory stays small for a 1 MB machine: the index is sized exactly after a
counting pass, and unpacking needs a 32 KB window and a few KB besides.

## Networking

Built against the **Roadshow SDK** (`C:\Projects\Amiga\Roadshow\Roadshow-SDK-1.8`).
At run time it only needs *a* `bsdsocket.library`, so Roadshow, AmiTCP and
Miami all work. The DLC server is plain `http://`, so no TLS library is
needed. That matters on an Amiga: TLS would mean AmiSSL.

Requests have a 15-second receive timeout. A download repaints the window and
its progress bar between pieces and takes Esc as cancel; on OS 3.0+ it shows the
busy pointer. Tested in FS-UAE (A1200, Kickstart 3.1) with its emulated
`bsdsocket.library`: index, download, checks, unpacking and `data/MODS`.

## Build

It uses the same toolchain as SWOS-United: Bartman's **amiga-debug** VS Code
extension (`m68k-amiga-elf-gcc` 15 + `elf2hunk`). There is no libc:
`src/startup.c` is the entry point (it handles Workbench start-up too), and
`src/util.c` has the string helpers plus the 68000 multiply/divide routines.

```
build.cmd                 :: Windows, from cmd or by double-click
build.cmd CPU=68020       :: 68020+ code instead of plain 68000
build.cmd clean
```

The output is `out/SWOS-DLC` with its Workbench icon, `out/SWOS-DLC.info`.
Copy both to the Amiga, or to a drawer your emulator mounts, and run it from
the Shell or Workbench.

The icon is a classic four-colour OS 2.x/3.x icon (a football, a tray, and an
arrow coming down into it), made by `tools/make_icon.py`. Edit the drawing
there and run `python3 tools/make_icon.py --preview icon.png` to regenerate
`SWOS-DLC.info` and see it enlarged. It also sets the stack to 16 KB for
Workbench starts. To use a Roadshow SDK
somewhere else, pass `ROADSHOW_SDK=<path>`.

## Code

| | |
|---|---|
| `main.c` | the window, actions and event loop |
| `catalog.c` | the index merged with what is installed |
| `mods.c` | `mods/` and `data/MODS` |
| `net.c` | HTTP/1.0 GET, streamed |
| `unzip.c`, `inflate.c` | zip entries, DEFLATE (RFC 1951) |
| `sha256.c`, `json.c`, `fio.c` | download checks, the JSON reader, dos.library files |

`json`, `sha256`, `inflate`, `unzip`, `fio` and `mods` also build on a PC
(`tests/host_test.c`, see its top) and are tested there against the real mod
zips.
