# Icons

## Application chrome — `*.svg` here

[Tabler Icons](https://tabler.io/icons) (filled set), MIT licensed. Only the
icons actually used are vendored, so the build does not depend on a local copy
of the full set. Each is `fill="currentColor"` on a 24x24 viewBox, which is why
the UI tints them with Slint's `colorize` rather than shipping a light and a
dark copy.

| file | used for |
|---|---|
| `device-gamepad-2.svg` | launcher: General tab |
| `device-desktop.svg`   | launcher: Display tab |
| `adjustments.svg`      | launcher: Advanced tab |
| `info-circle.svg`      | launcher: About tab |
| `player-play.svg`      | Run / Use this pack |
| `x.svg`                | Exit / Remove |
| `folder-open.svg`      | Reveal game folder |
| `sun.svg`              | Light theme |
| `moon.svg`             | Dark theme |
| `brightness-auto.svg`  | Follow the system theme |

## DLC categories — `dlc/*.svg`

Named after the DLC type id they belong to, so the mapping from artwork to
category needs no lookup table.

**Full colour** — the game's own sprite artwork, supplied for this purpose.
These are drawn as-is; tinting them would destroy them, which is what
`DlcCategory.tint-icon` is for.

| file | source |
|---|---|
| `pitch.svg` `cup.svg` | `Pitch.svg` `Cup.svg` |
| `sprite_ball.svg` `sprite_flag.svg` | `Ball.svg` `Flag.svg` |
| `sprite_gk.svg` `sprite_manager.svg` `sprite_referee.svg` | `GoalKeeper.svg` `Manager.svg` `Referee.svg` |
| `sprite_goalnet.svg` | `Goal.svg` |
| `sprite_team1.svg` `sprite_team2.svg` `sprite_team3.svg` | `Team1.svg` `Team2.svg` `Team3.svg` |

They are not copied verbatim: `scripts/normalize_dlc_icons.py` rewrites each
root element to a tight square viewBox around the actual drawing, plus a small
margin. The originals carry the Inkscape canvas size (`width="3440"
height="1369"`) which letterboxes the icon into a sliver, and their content
fills anywhere from 50% to 100% of the viewBox, so untouched they render at
wildly different sizes side by side. Only the root is changed; the paths are
exactly as supplied.

`Sprite.svg` was supplied but is byte-identical to `Team1.svg`, so it is not
vendored — using it for Goal Net would have put the same picture on two rows.

**Monochrome** — glyphs for the categories with no sprite artwork, tinted to the
palette like the chrome above.

| file | category | source |
|---|---|---|
| `teamdb.svg` | Team Database | Tabler `database` |
| `bgi2.svg` | Background Image | Tabler `photo` |
| `commentary.svg` | Commentary | Tabler `microphone` |
| `mmusic.svg` | Menu Music | Tabler `playlist` |
| `fx.svg` | Sound Effects | [Iconoir](https://iconoir.com) solid `sound-high` |
| `tmusic.svg` | Title Music | Iconoir solid `music-note` |

## List flags — `flags.png`

A 4x13 grid of 16x16 cells, extracted from the Delphi DLC manager's `imgFlags`
image list by `scripts/extract_flags.py` and shipped as one sheet that the UI
clips per cell rather than as 51 separate files.

The indices are the original's, unchanged, because the code that picks a flag is
a direct port of the `ImageIndex` chains in `lvwCommentaryInfoCustomDrawItem` and
`lvwCupInfoCustomDrawItem` (see `swos::dlc_flag_index`): 0 Germany, 1 United
Kingdom, 2 Spain, … 12 the "unknown" question mark, 30 Denmark, 33 UEFA, 34
penalty shootout, 36-38 player portraits, 39-41 NBA/Sega/SNES, 42-43 trophies,
44 England, 50 Turkey.

Only Commentary and Cup show flags — they are the two lists that set
`SmallImages = imgFlags` in the original form.

## Executable icons

See `icon/` and `scripts/generate_icons.py`.
