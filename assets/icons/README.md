# Icons

[Tabler Icons](https://tabler.io/icons) (filled set), MIT licensed.

Only the icons the launcher actually uses are vendored here, so the build does
not depend on a local copy of the full set. The SVGs are unmodified: each is
`fill="currentColor"` on a 24x24 viewBox, which is why the UI tints them with
Slint's `colorize` rather than shipping a light and a dark copy of each.

| file | used for |
|---|---|
| `device-gamepad-2.svg` | General tab |
| `device-desktop.svg`   | Display tab |
| `adjustments.svg`      | Advanced tab |
| `info-circle.svg`      | About tab |
| `player-play.svg`      | Run SWOS button |
| `x.svg`                | Exit button |
| `folder-open.svg`      | Reveal game folder |
| `sun.svg`              | Light theme |
| `moon.svg`             | Dark theme |
| `brightness-auto.svg`  | Follow the system theme |
