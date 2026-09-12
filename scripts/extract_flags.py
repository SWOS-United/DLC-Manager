#!/usr/bin/env python3
"""Extract the flag strip from the Delphi DLC manager's form.

    python3 scripts/extract_flags.py [path to swos-game-launcher]

Writes assets/icons/flags.png -- a 4x13 grid of 16x16 cells, indexed exactly as
the original's imgFlags TImageList was, so the ImageIndex values hard-coded in
contents_downloader_u.pas carry over unchanged (0 Germany, 1 United Kingdom,
2 Spain, ... 12 the "unknown" question mark, 33-50 the various competition and
console badges).

The images live in contents_downloader_u.dfm as a Delphi TImageList blob: a
short header, then a 24-bit BMP holding the colour, then a second 1-bit BMP
holding the mask. Delphi's mask is inverted relative to what an alpha channel
wants -- set means transparent -- which is the one thing here that is easy to
get backwards.

The output is committed, so this only needs running if the original's flag strip
ever changes.
"""

import binascii
import io
import os
import re
import struct
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SRC = "/mnt/c/Projects/GitHub/swos-game-launcher"
DFM = "contents_downloader_u.dfm"
OUT = os.path.join(ROOT, "assets", "icons", "flags.png")


def main():
    src_dir = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC
    dfm_path = os.path.join(src_dir, DFM)
    if not os.path.exists(dfm_path):
        sys.exit(f"cannot find {dfm_path}\nusage: {os.path.basename(sys.argv[0])} "
                 f"[path to swos-game-launcher]")

    text = open(dfm_path, encoding="latin-1").read()
    start = text.index("object imgFlags: TImageList")
    blob_start = text.index("Bitmap = {", start) + len("Bitmap = {")
    blob_end = text.index("}", blob_start)
    data = binascii.unhexlify(re.sub(r"[^0-9A-Fa-f]", "", text[blob_start:blob_end]))

    if data[:2] != b"IL":
        sys.exit("not a TImageList blob")
    count = struct.unpack("<H", data[4:6])[0]
    cell_w = struct.unpack("<H", data[10:12])[0]
    cell_h = struct.unpack("<H", data[12:14])[0]

    first = data.index(b"BM")
    second = data.find(b"BM", first + 2)
    if second < 0:
        sys.exit("no mask bitmap in the blob")

    colour = Image.open(io.BytesIO(data[first:second])).convert("RGB")
    mask = Image.open(io.BytesIO(data[second:])).convert("L")

    rgba = colour.convert("RGBA")
    # Delphi's mask marks transparent pixels, so invert it into an alpha channel.
    rgba.putalpha(mask.point(lambda v: 0 if v > 127 else 255))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    rgba.save(OUT)
    print(f"wrote {os.path.relpath(OUT, ROOT)}: {rgba.size[0]}x{rgba.size[1]}, "
          f"{count} cells of {cell_w}x{cell_h} "
          f"({colour.width // cell_w} per row)")


if __name__ == "__main__":
    main()
