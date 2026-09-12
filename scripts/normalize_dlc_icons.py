#!/usr/bin/env python3
"""Normalise the DLC category artwork into assets/icons/dlc/.

    python3 scripts/normalize_dlc_icons.py ~/Downloads

The SWOS sprite art arrives as VTracer/Inkscape SVGs that are not usable as
icons as-is, for two reasons:

  * The root carries the Inkscape canvas size (width="3440" height="1369"),
    which is both huge and non-square. A toolkit that honours the intrinsic
    size letterboxes the icon into a sliver.
  * The drawing occupies anywhere from 50% (the ball) to 100% (the pitch) of
    its 64x64 viewBox, so side by side the icons render at wildly different
    visual sizes.

So each file gets a tight square viewBox around its actual content plus a small
margin, and a square intrinsic size. The paths are untouched -- only the root
element's viewBox/width/height are rewritten -- so the artwork itself is exactly
what was supplied.

Monochrome glyphs (Tabler, Iconoir) are already square and tight, and are copied
by scripts/... no, they are vendored by hand; this script only touches the
colour artwork listed in SOURCES.

Requires cairosvg (pip install cairosvg) and Pillow.
"""

import io
import os
import re
import sys

try:
    import cairosvg
except ImportError:
    sys.exit("cairosvg is required: pip3 install --user cairosvg")

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "icon" + "s", "dlc")

# Supplied filename -> DLC type id (which is also the output filename, so the
# mapping from artwork to category is visible in one place).
SOURCES = {
    "Pitch.svg": "pitch",
    "Cup.svg": "cup",
    "Ball.svg": "sprite_ball",
    "GoalKeeper.svg": "sprite_gk",
    "Manager.svg": "sprite_manager",
    "Referee.svg": "sprite_referee",
    "Team1.svg": "sprite_team1",
    "Team2.svg": "sprite_team2",
    "Team3.svg": "sprite_team3",
    "Flag.svg": "sprite_flag",
    "Goal.svg": "sprite_goalnet",
}

# Breathing room around the content, as a fraction of the square side.
MARGIN = 0.06
# Resolution used to find the content box. Higher is more precise; 512 puts the
# error well under a viewBox unit for a 64-unit canvas.
PROBE = 512


def content_box(svg_path):
    """Content bounding box in viewBox units, or None if the file is empty."""
    png = cairosvg.svg2png(url=svg_path, output_width=PROBE, output_height=PROBE)
    image = Image.open(io.BytesIO(png)).convert("RGBA")
    box = image.getbbox()
    if not box:
        return None
    return box


def view_box(svg_text):
    match = re.search(r'viewBox\s*=\s*"([^"]+)"', svg_text)
    if not match:
        return None
    parts = [float(v) for v in match.group(1).replace(",", " ").split()]
    return parts if len(parts) == 4 else None


def normalise(src_path, dst_path):
    text = open(src_path, encoding="utf-8").read()

    vb = view_box(text)
    if not vb:
        sys.exit(f"{src_path}: no viewBox to work from")
    vx, vy, vw, vh = vb

    box = content_box(src_path)
    if not box:
        sys.exit(f"{src_path}: renders to nothing")

    # Probe pixels -> viewBox units.
    x0 = vx + box[0] * vw / PROBE
    y0 = vy + box[1] * vh / PROBE
    x1 = vx + box[2] * vw / PROBE
    y1 = vy + box[3] * vh / PROBE

    # Square it around the content's centre so nothing is stretched, then add
    # the margin.
    side = max(x1 - x0, y1 - y0)
    side *= 1.0 + 2.0 * MARGIN
    cx = (x0 + x1) / 2.0
    cy = (y0 + y1) / 2.0
    nx = cx - side / 2.0
    ny = cy - side / 2.0

    new_view = f'viewBox="{nx:.4f} {ny:.4f} {side:.4f} {side:.4f}"'
    text = re.sub(r'viewBox\s*=\s*"[^"]+"', new_view, text, count=1)

    # A square intrinsic size, so a toolkit that reads width/height agrees with
    # the viewBox instead of letterboxing.
    root_end = text.index(">", text.index("<svg"))
    root = text[:root_end]
    root = re.sub(r'\swidth\s*=\s*"[^"]*"', "", root, count=1)
    root = re.sub(r'\sheight\s*=\s*"[^"]*"', "", root, count=1)
    root = root.replace("<svg", '<svg width="64" height="64"', 1)
    text = root + text[root_end:]

    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, "w", encoding="utf-8") as out:
        out.write(text)
    fill = (x1 - x0) / side * 100.0
    print(f"  {os.path.basename(src_path):<16} -> {os.path.basename(dst_path):<20} "
          f"viewBox {side:.1f}u, content {fill:.0f}% wide")


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {os.path.basename(sys.argv[0])} <directory with the source SVGs>")
    src_dir = sys.argv[1]

    missing = [n for n in SOURCES if not os.path.exists(os.path.join(src_dir, n))]
    if missing:
        sys.exit(f"missing in {src_dir}: {', '.join(sorted(missing))}")

    print(f"normalising {len(SOURCES)} icons from {src_dir}")
    for name, type_id in sorted(SOURCES.items()):
        normalise(os.path.join(src_dir, name), os.path.join(OUT, f"{type_id}.svg"))


if __name__ == "__main__":
    main()
