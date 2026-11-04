# SWOS United DLC Manager — Amiga (demo)

An Intuition + GadTools take on `swos-dlc-manager` for AmigaOS 2.04 or later,
on the Workbench screen. **This is a demo**: the UI and catalogue model are
real, and so is fetching the catalogues over the network. Downloading,
installing and applying are placeholders.

```
+- SWOS United DLC Manager -------------------------------------------+
| Category           Packs                                            |
| +----------------+ +----------------------------------------------+ |
| |Team Database(1)| |[NEW] 1994 - Moon Soccer                      | |
| |Pitch (3)       | |[ OK] 1994 - SWOS 1.1 (Amiga)                 | |
| |Cup (2)         | |[UPD] 2020 - Official 2021                    | |
| |...             | |[REM] Placeholder - Withdrawn Pitch           | |
| +----------------+ +----------------------------------------------+ |
|  Author [Sensible Software and starwindz                          ] |
| Version [1.0       ]  Status [New                    ]              |
|    Info [1994 - Moon Soccer                                       ] |
| Applied [1994 - SWOS 1.1 (Amiga)                                  ] |
| [Refresh] [Download] [Remove] [Apply] [Quit]                        |
| [Roadshow ready - press Refresh...                 ] [#####       ]  |
+---------------------------------------------------------------------+
```

## What works

| | |
|---|---|
| **Refresh** (and *Project → Refresh all*) | Real. `GET http://www.sensiblesoccer.de/swos2020/dlc/<type>/<type>-info.csv` over `bsdsocket.library`, the same catalogue the PC manager reads (see `core/include/dlc_catalog.h`). |
| Status merge | Real. New / Installed / Update / Removed, computed the same way as `DlcCatalog::merge`. |
| No TCP/IP stack | Opens anyway, with placeholder lists, and says so in the status line. |
| **Download** | *Placeholder.* A progress bar that runs for about 2.5 seconds, then marks the pack installed. Esc cancels. |
| **Remove / Apply** | *Placeholder.* They change the in-memory state only. |

Grep for `PLACEHOLDER` to find every stub.

## Networking

Built against the **Roadshow SDK** (`C:\Projects\Amiga\Roadshow\Roadshow-SDK-1.8`).
At run time it only needs *a* `bsdsocket.library`, so Roadshow, AmiTCP and
Miami all work. The DLC server is plain `http://`, so no TLS library is
needed. That matters on an Amiga: TLS would mean AmiSSL.

Requests block, with a 15-second receive timeout. The window does not repaint
during a fetch, and on OS 3.0+ it shows the busy pointer. Each catalogue is
under 8 KB, so a fetch takes a moment.

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

## Next steps

1. **Real downloads.** Stream `<name>.7z` to disk in chunks instead of
   buffering it, with a timer.device-paced progress bar. Fetch it on a
   separate process, or with non-blocking sockets and `WaitSelect()`, so the
   window keeps repainting.
2. **Unpacking.** The packs are `.7z`. That means porting a 7z decoder (LZMA
   is feasible but slow on a 68000), or asking for the server to publish LhA
   copies too.
3. **What the Amiga game can actually use.** The packs are made for the PC
   SWOS 2020 build (DOS HD art, PC audio). Which categories mean anything to
   the Amiga game, or to OpenSWOS on the Amiga, and whether they need
   converting, decides which of the 17 categories this tool should list at all.
4. Store the installed state on disk as `<type>-info.csv`, as the PC version
   does, and write the selection somewhere the game reads it.
