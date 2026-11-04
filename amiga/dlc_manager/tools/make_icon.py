#!/usr/bin/env python3
"""Generate SWOS-DLC.info, the Workbench icon for the Amiga DLC manager.

A classic (OS 2.x/3.x) DiskObject: two 2-bitplane images in the standard
four-colour Workbench palette, so it looks right on any Workbench from 2.04
on. 3.5+ Workbenches show it as is too.

The picture is a football above a tray, with a blue arrow coming down into
the tray. The selected image moves the arrow further down and lights up the
tray.

Pixels are hires (one wide, two tall), so the 44x24 image looks roughly square
on a 640x256 Workbench and the ball is drawn as a 2:1 ellipse.

    python3 tools/make_icon.py                write SWOS-DLC.info
    python3 tools/make_icon.py --preview p.png  also write an enlarged preview
"""

import argparse
import math
import os
import struct
import sys

W, H = 44, 24

# Workbench 2.x/3.x default pens: 0 grey (background), 1 black, 2 white, 3 blue.
BG, BLACK, WHITE, BLUE = 0, 1, 2, 3
PALETTE = [(0xAA, 0xAA, 0xAA), (0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x66, 0x88, 0xBB)]


def blank():
    return [[BG] * W for _ in range(H)]


def in_pentagon(px, py, cx, cy, r, rot):
    """Is (px, py) inside a regular pentagon of circumradius r, rotated rot?"""
    verts = [(cx + r * math.cos(rot + k * 2 * math.pi / 5),
              cy + r * math.sin(rot + k * 2 * math.pi / 5)) for k in range(5)]
    for i in range(5):
        ax, ay = verts[i]
        bx, by = verts[(i + 1) % 5]
        if (bx - ax) * (py - ay) - (by - ay) * (px - ax) < 0:
            return False
    return True


def draw_ball(img, cx, cy, rx, ry):
    """A football: white with black pentagons, blue shading on the lower right."""
    up = -math.pi / 2
    # Centre pentagon pointing up; five around it pointing outwards, centred
    # near the rim so the outline clips them into the familiar partial patches.
    patches = [(0.0, 0.0, 0.36, up)]
    for k in range(5):
        a = up + math.pi / 5 + k * 2 * math.pi / 5
        patches.append((math.cos(a) * 0.95, math.sin(a) * 0.95, 0.34, a))

    def inside(x, y):
        return math.hypot((x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry) <= 1.0

    for y in range(H):
        for x in range(W):
            if not inside(x, y):
                continue
            # One-pixel outline: any inside pixel with an outside neighbour.
            if not all(inside(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                img[y][x] = BLACK
                continue
            nx = (x + 0.5 - cx) / rx
            ny = (y + 0.5 - cy) / ry
            colour = WHITE
            for px, py, pr, rot in patches:
                if in_pentagon(nx, ny, px, py, pr, rot):
                    colour = BLACK
                    break
            if colour == WHITE and nx * 0.6 + ny * 0.8 > 0.6:
                colour = BLUE
            img[y][x] = colour


def draw_arrow(img, x0, top, shaft_w, head_w, length, head_h):
    """A blue down-arrow with a black outline. x0 is the arrow's centre column."""
    shape = blank()
    shaft_bottom = top + length - head_h
    for y in range(top, top + length):
        if y >= H:
            break
        if y < shaft_bottom:
            half = shaft_w / 2
        else:
            # The head narrows to a point over head_h rows.
            t = (y - shaft_bottom) / max(1, head_h - 1)
            half = (head_w / 2) * (1 - t) + 0.5
        for x in range(W):
            if abs(x + 0.5 - x0) <= half:
                shape[y][x] = 1
    # Outline: shape pixels next to a non-shape pixel turn black.
    for y in range(H):
        for x in range(W):
            if not shape[y][x]:
                continue
            edge = False
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if not (0 <= xx < W and 0 <= yy < H) or not shape[yy][xx]:
                    edge = True
                    break
            img[y][x] = BLACK if edge else BLUE


def draw_tray(img, x0, x1, top, bottom, lit):
    """An open-topped tray: black outline, a blue shadow line inside, or all blue when lit."""
    for y in range(top, bottom + 1):
        for x in range(x0, x1 + 1):
            if y == bottom or x in (x0, x1):
                img[y][x] = BLACK
            elif y == bottom - 1 or lit:
                img[y][x] = BLUE
    # Rim lips sticking out at the top corners.
    img[top][x0 - 1] = BLACK
    img[top][x1 + 1] = BLACK


def make_images():
    normal = blank()
    draw_tray(normal, 3, 40, 16, 23, lit=False)
    draw_ball(normal, 14.0, 8.0, 12.5, 7.0)
    draw_arrow(normal, 34.5, 0, 6, 13, 19, 7)

    selected = blank()
    draw_tray(selected, 3, 40, 16, 23, lit=True)
    draw_ball(selected, 14.0, 8.0, 12.5, 7.0)
    draw_arrow(selected, 34.5, 3, 6, 13, 19, 7)
    return normal, selected


# --- .info writer ---------------------------------------------------------

WB_DISKMAGIC = 0xE310
WB_DISKVERSION = 1
WB_DISKREVISION = 1          # low byte of gg_UserData: OS 2.x icon
WBTOOL = 3
NO_ICON_POSITION = 0x80000000
GFLG_GADGIMAGE = 0x0004
GFLG_GADGHIMAGE = 0x0002
GACT_RELVERIFY = 0x0001
GACT_IMMEDIATE = 0x0002
GTYP_BOOLGADGET = 0x0001
STACK_SIZE = 16384


def planar(img):
    """Two bitplanes, rows padded to 16 bits, plane 0 first."""
    row_words = (W + 15) // 16
    out = bytearray()
    for plane in range(2):
        for y in range(H):
            for wi in range(row_words):
                word = 0
                for b in range(16):
                    x = wi * 16 + b
                    if x < W and (img[y][x] >> plane) & 1:
                        word |= 0x8000 >> b
                out += struct.pack(">H", word)
    return bytes(out)


def image_struct():
    # LeftEdge, TopEdge, Width, Height, Depth, ImageData (non-zero = present),
    # PlanePick, PlaneOnOff, NextImage.
    return struct.pack(">hhhhhIBBI", 0, 0, W, H, 2, 1, 0x03, 0x00, 0)


def build_info(normal, selected):
    gadget = struct.pack(
        ">IhhhhHHHIIIIIHI",
        0,                                  # NextGadget
        0, 0, W, H,                         # LeftEdge, TopEdge, Width, Height
        GFLG_GADGIMAGE | GFLG_GADGHIMAGE,   # Flags: image, alternate image
        GACT_RELVERIFY | GACT_IMMEDIATE,    # Activation
        GTYP_BOOLGADGET,                    # GadgetType
        1,                                  # GadgetRender (present)
        1,                                  # SelectRender (present)
        0, 0, 0,                            # GadgetText, MutualExclude, SpecialInfo
        0,                                  # GadgetID
        WB_DISKREVISION,                    # UserData
    )
    disk_object = struct.pack(">HH", WB_DISKMAGIC, WB_DISKVERSION) + gadget + struct.pack(
        ">BBIIIIIII",
        WBTOOL, 0,
        0,                                  # DefaultTool
        0,                                  # ToolTypes
        NO_ICON_POSITION, NO_ICON_POSITION, # CurrentX, CurrentY
        0,                                  # DrawerData
        0,                                  # ToolWindow
        STACK_SIZE,
    )
    assert len(disk_object) == 78, len(disk_object)
    return (disk_object + image_struct() + planar(normal) + image_struct() + planar(selected))


def write_preview(path, normal, selected):
    from PIL import Image

    sx, sy, gap = 4, 8, 16           # hires pixels: twice as tall as wide
    out = Image.new("RGB", (W * sx * 2 + gap * 3, H * sy + gap * 2), PALETTE[BG])
    for i, img in enumerate((normal, selected)):
        ox = gap + i * (W * sx + gap)
        for y in range(H):
            for x in range(W):
                c = PALETTE[img[y][x]]
                for yy in range(sy):
                    for xx in range(sx):
                        out.putpixel((ox + x * sx + xx, gap + y * sy + yy), c)
    out.save(path)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-o", "--output", default=os.path.join(here, "..", "SWOS-DLC.info"))
    ap.add_argument("--preview", help="also write a PNG preview (normal and selected)")
    args = ap.parse_args()

    normal, selected = make_images()
    with open(args.output, "wb") as f:
        f.write(build_info(normal, selected))
    if args.preview:
        write_preview(args.preview, normal, selected)
    return 0


if __name__ == "__main__":
    sys.exit(main())
