#!/usr/bin/env python3
"""Generate the per-platform executable icons from assets/icon/swos.svg.

    python3 scripts/generate_icons.py

Writes:
    assets/icon/swos.ico                 Windows, 7 sizes in one file
    assets/icon/swos.iconset/*.png       macOS, input for iconutil
    assets/icon/swos.icns                macOS (only when iconutil is present)
    assets/icon/hicolor/<size>/swos.png  Linux, freedesktop icon theme layout

The source is 41x41 pixel art traced to vector (VTracer). Each size is
rendered straight from the SVG rather than downscaled from one master: the
artwork is high-contrast blocks, and resampling a master with LANCZOS rings
visibly around every edge at 16-48px, which is exactly where an icon can least
afford it. cairosvg's own antialiasing at the target size is cleaner. Compared
both ways before settling on this.

Full-bleed is deliberate: SWOS-United's SWOS.icns has the same ~1% margin, and
the two projects should look identical in a dock or task bar.

Requires cairosvg (pip install cairosvg). iconutil is macOS-only; on other
hosts the .iconset is still written so a Mac can finish the job, and the CMake
build falls back to the .iconset if the .icns is absent.
"""

import io
import os
import shutil
import subprocess
import sys

try:
    import cairosvg
except ImportError:
    sys.exit("cairosvg is required: pip3 install --user cairosvg")

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG = os.path.join(ROOT, "assets", "icon", "swos.svg")
OUT = os.path.join(ROOT, "assets", "icon")

ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
LINUX_SIZES = [16, 22, 24, 32, 48, 64, 128, 256, 512]
# (filename, pixel size) -- Apple's required iconset contents.
ICONSET = [
    ("icon_16x16.png", 16),
    ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32),
    ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128),
    ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256),
    ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512),
    ("icon_512x512@2x.png", 1024),
]


def render(size):
    """Rasterise the SVG at exactly `size` x `size`."""
    png = cairosvg.svg2png(url=SVG, output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def main():
    if not os.path.exists(SVG):
        sys.exit(f"missing {SVG}")

    # Every distinct size any output needs, rendered once.
    wanted = sorted({*ICO_SIZES, *LINUX_SIZES, *(size for _, size in ICONSET)})
    images = {size: render(size) for size in wanted}
    print(f"rendered {len(images)} sizes: {wanted}")

    # --- Windows ---------------------------------------------------------
    ico_path = os.path.join(OUT, "swos.ico")
    # Pillow matches each requested size against the images it is given, so
    # passing them all means the file holds the direct renders rather than
    # Pillow's own rescales of the largest one.
    largest = images[max(ICO_SIZES)]
    largest.save(ico_path, format="ICO",
                 sizes=[(s, s) for s in ICO_SIZES],
                 append_images=[images[s] for s in ICO_SIZES if s != max(ICO_SIZES)])
    print(f"wrote {os.path.relpath(ico_path, ROOT)} ({len(ICO_SIZES)} sizes)")

    # --- Linux -----------------------------------------------------------
    for size in LINUX_SIZES:
        directory = os.path.join(OUT, "hicolor", f"{size}x{size}", "apps")
        os.makedirs(directory, exist_ok=True)
        images[size].save(os.path.join(directory, "swos.png"))
    # Scalable entry: the original vector, which is what the spec prefers.
    scalable = os.path.join(OUT, "hicolor", "scalable", "apps")
    os.makedirs(scalable, exist_ok=True)
    shutil.copyfile(SVG, os.path.join(scalable, "swos.svg"))
    print(f"wrote hicolor/ ({len(LINUX_SIZES)} sizes + scalable)")

    # --- macOS -----------------------------------------------------------
    iconset = os.path.join(OUT, "swos.iconset")
    os.makedirs(iconset, exist_ok=True)
    for name, size in ICONSET:
        images[size].save(os.path.join(iconset, name))
    print(f"wrote swos.iconset ({len(ICONSET)} images)")

    if shutil.which("iconutil"):
        icns = os.path.join(OUT, "swos.icns")
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o", icns], check=True)
        print(f"wrote {os.path.relpath(icns, ROOT)}")
    else:
        print("iconutil not found (not macOS) -- run this script on a Mac, or "
              "`iconutil -c icns assets/icon/swos.iconset`, to produce swos.icns")


if __name__ == "__main__":
    main()
