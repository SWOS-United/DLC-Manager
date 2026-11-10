#!/usr/bin/env python3
"""dlc2020_to_amiga.py -- SWOS 2020's pitches and team databases as SWOS United Amiga mods.

    scripts/dlc2020_to_amiga.py --out dist/amiga --cache dist/mods/.cache
    scripts/dlc2020_to_amiga.py --out dist/amiga --types pitch --limit 2

The Amiga game reads only binaries, so these are their own mods (own uuids, own slugs,
"targets": ["amiga"]), in their own index, from the same SWOS 2020 packs as the PC mods
(dlc2020_to_mods.py, whose download cache this shares). Each zip holds what the Amiga
reads through its overlay: a file in PROGDIR:mods/<slug>/ is read in place of the game's
own (OpenSWOS src/resload.c make_read_path), for the mods data/MODS names.

    pitch    PITCH1-6.DAT/.BLK  -> grafs_aga/P2020_1-6.MAP, the AGA build's 2020 pitch
                                   (OpenSWOS src/pitch.c loadPitchMap2020), unpacked, as
                                   swosgfx's Sg_PitchPcToAga makes them; played with
                                   OPTIONS -> PITCHES = 2020 on an AGA Amiga
    teamdb   TEAM.NNN, POOLPLYR -> data/, as they are (the PC's files are the Amiga's,
             .DAT, *.TMD           RNC-packed or not). Squads only: the season year,
                                   renames and league fixes are the PC game's code. Only a
                                   database whose every country has 96/97's clubs, in 96/97's
                                   divisions, is made: the Amiga has no code that resizes a
                                   country's team numbers or leagues (the PC's
                                   DataJson_FitModTeamFiles), so a bigger country would run
                                   into the next one's numbers. --base-data is 96/97's data/.

Slugs are at most 30 characters (an AmigaDOS name), uuids and slugs are kept in
scripts/dlc2020_amiga_uuids.json -- commit it after a conversion.
"""
import argparse
import collections
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import urllib.parse
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dlc2020_to_mods as pc   # noqa: E402  the catalogue, downloads, 7z and zip helpers

UUIDS = os.path.join(HERE, "dlc2020_amiga_uuids.json")
SLUG_MAX = 30                       # an AmigaDOS file name
PITCH_CELLS = 42 * 55
PITCH_AGA_MAX = 544 * 1024          # PITCHFILE_MAX_SIZE on the AGA Amiga
TEAM_REC = 684
FRONTBUFFER_MAX = 104832            # a raw file is staged there (file_io.c LoadFileEx)

TYPES = {
    #          prefix  label            slot      changes
    "pitch": ("apt", "Pitch",         "pitch",  "cosmetic"),
    "teamdb": ("atd", "Team database", "teamdb", "data"),
}
ABOUT = {
    "pitch": "SWOS 2020's pitch, for an AGA Amiga: set OPTIONS > PITCHES to 2020.",
    "teamdb": "SWOS 2020's team database, squads only: the Amiga plays it with 96/97's "
              "leagues and season years.",
}


class SkipPack(Exception):
    pass


def identity(registry, key, type_id, title):
    have = registry.get(key)
    if have:
        return have["uuid"], have["slug"]
    taken = {v["slug"] for v in registry.values()}
    body = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")
    slug = "%s-%s" % (TYPES[type_id][0], body)
    if len(slug) > SLUG_MAX or slug in taken:
        tag = hashlib.sha1(key.encode("utf-8")).hexdigest()[:5]
        slug = slug[:SLUG_MAX - len(tag) - 1].rstrip("-") + "-" + tag
    registry[key] = {"uuid": str(uuid.uuid4()), "slug": slug}
    return registry[key]["uuid"], slug


def find_nocase(root, name):
    for dp, _, fs in os.walk(root):
        for f in fs:
            if f.lower() == name.lower():
                return os.path.join(dp, f)
    return None


# ---- pitches --------------------------------------------------------------------------

def pitch_to_aga(dat, blk):
    """PITCHn.DAT + PITCHn.BLK -> the AGA map (swosgfx dos_pitch.c Sg_PitchPcToAga): the
    2310 tile offsets big-endian, then each 256-byte chunky tile as 8 interleaved
    bitplanes, a row at a time (plane 0's word, plane 1's ... plane 7's)."""
    if len(dat) != PITCH_CELLS * 4 or len(blk) % 256:
        raise ValueError("not a PC pitch: DAT %d bytes, BLK %d" % (len(dat), len(blk)))
    offs = [int.from_bytes(dat[i * 4:i * 4 + 4], "little") for i in range(PITCH_CELLS)]
    if max(offs) + 256 > len(blk):
        raise ValueError("the map points past the tile bank")
    out = bytearray()
    for o in offs:
        out += o.to_bytes(4, "big")
    for t in range(0, len(blk), 256):
        tile = blk[t:t + 256]
        for row in range(16):
            px = tile[row * 16:row * 16 + 16]
            for plane in range(8):
                w = 0
                for x in range(16):
                    w |= ((px[x] >> plane) & 1) << (15 - x)
                out += w.to_bytes(2, "big")
    return bytes(out)


def convert_pitch(raw, mod):
    made = 0
    for n in range(1, 7):
        dat, blk = find_nocase(raw, "PITCH%d.DAT" % n), find_nocase(raw, "PITCH%d.BLK" % n)
        if not (dat and blk):
            continue
        m = pitch_to_aga(open(dat, "rb").read(), open(blk, "rb").read())
        if len(m) > PITCH_AGA_MAX:
            raise SkipPack("pitch %d's map is %d bytes, more than the Amiga's %d" % (n, len(m), PITCH_AGA_MAX))
        dst = os.path.join(mod, "grafs_aga", "P2020_%d.MAP" % n)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, "wb").write(m)
        made += 1
    if not made:
        raise SkipPack("no PITCHn.DAT/.BLK in the pack")
    return made, []


# ---- team databases -------------------------------------------------------------------

def _rnc():
    sys.path.insert(0, os.path.join(OPENSWOS, "scripts"))
    import swos_team_file
    return swos_team_file


def team_bytes(path):
    b = open(path, "rb").read()
    return _rnc().rnc_unpack(b) if b[:3] == b"RNC" else b


def league_shape(b):
    """(clubs, ((division, clubs), ...)) of a team file."""
    count = (b[0] << 8) | b[1]
    divs = collections.Counter(b[2 + i * TEAM_REC + 25] for i in range(count)
                               if 2 + (i + 1) * TEAM_REC <= len(b))
    return count, tuple(sorted(divs.items()))


BASE_SHAPES = {}


def base_shapes(base_data):
    if not BASE_SHAPES:
        for f in os.listdir(base_data):
            m = re.fullmatch(r"TEAM\.(\d{3})", f.upper())
            if m:
                BASE_SHAPES[int(m.group(1))] = league_shape(team_bytes(os.path.join(base_data, f)))
    return BASE_SHAPES


def convert_teamdb(raw, mod, base_data):
    data = None
    for dp, _, fs in os.walk(raw):
        if any(f.upper().startswith("TEAM.") for f in fs):
            data = dp
            break
    if not data:
        raise SkipPack("no TEAM.NNN files in the pack")
    base = base_shapes(base_data)
    out = os.path.join(mod, "data")
    os.makedirs(out)
    differ, left_out, placed = [], [], 0
    for f in sorted(os.listdir(data)):
        p = os.path.join(data, f)
        up = f.upper()
        m = re.fullmatch(r"TEAM\.(\d{3})", up)
        if m:
            n = int(m.group(1))
            if os.path.getsize(p) == 0:
                continue
            if n not in base:            # a country 96/97 has not got: the Amiga never reads it
                left_out.append(up)
                continue
            b = team_bytes(p)
            if league_shape(b) != base[n]:
                differ.append(n)
                continue
        elif up not in ("POOLPLYR.DAT", "EUROCUP.TMD", "EUROCWC.TMD", "UEFACUP.TMD"):
            continue
        raw_size = len(team_bytes(p))
        packed = open(p, "rb").read()[:3] == b"RNC"
        if not packed and raw_size > FRONTBUFFER_MAX:
            raise SkipPack("%s is %d bytes unpacked, more than the Amiga loads raw" % (up, raw_size))
        shutil.copyfile(p, os.path.join(out, up))
        placed += 1
    if differ:
        raise SkipPack("its leagues are not 96/97's (team files %s), which the Amiga cannot fit"
                       % ", ".join("%03d" % n for n in differ[:8]) + (" ..." if len(differ) > 8 else ""))
    notes = ["left out (no such country in 96/97): " + ", ".join(left_out)] if left_out else []
    return placed, notes


# ---- one pack -> one mod --------------------------------------------------------------

def convert(type_id, row, archive, out_dir, registry, seven, base_url, base_data):
    prefix, label, slot, changes = TYPES[type_id]
    key = "%s/%s" % (type_id, row["title"])
    work = tempfile.mkdtemp(prefix="dlc2020-amiga-")
    try:
        raw = os.path.join(work, "raw")
        os.makedirs(raw)
        pc.extract(archive, raw, seven)
        new = key not in registry
        mod_uuid, slug = identity(registry, key, type_id, row["title"])
        mod = os.path.join(work, slug)
        os.makedirs(mod)
        try:
            if type_id == "pitch":
                placed, notes = convert_pitch(raw, mod)
            else:
                placed, notes = convert_teamdb(raw, mod, base_data)
        except Exception:
            if new:                     # a pack left out keeps no identity
                registry.pop(key, None)
            raise
        for dp, _, fs in os.walk(raw):          # the authors' notes travel with their data
            for f in fs:
                if f.lower() == "readme.txt":
                    shutil.copyfile(os.path.join(dp, f), os.path.join(mod, "readme.txt"))
        manifest = {
            "uuid": mod_uuid,
            "id": slug,
            "name": "%s: %s" % (label, row["title"]),
            "version": row["version"] or "1.0",
            "author": row["author"],
            "about": ABOUT[type_id],
            "targets": ["amiga"],
            "changes": changes,
            "slot": slot,
            "title": row["title"],
            "category": type_id,
            "swos2020": {"type": type_id, "title": row["title"]},
        }
        if type_id == "teamdb":
            manifest["online"] = False
        return finish(mod, manifest, type_id, out_dir, base_url), placed, notes
    finally:
        shutil.rmtree(work, ignore_errors=True)


def finish(mod, manifest, type_id, out_dir, base_url):
    slug = manifest["id"]
    with open(os.path.join(mod, "mod.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
        f.write("\n")
    for dp, _, fs in os.walk(mod):
        for f in fs:
            if len(f) > SLUG_MAX:
                raise ValueError("%s: a name longer than AmigaDOS keeps" % f)
    zip_dir = os.path.join(out_dir, type_id)
    os.makedirs(zip_dir, exist_ok=True)
    zip_path = os.path.join(zip_dir, slug + ".zip")
    pc._zip(mod, zip_path)
    entry = {k: manifest[k] for k in ("uuid", "name", "version", "author", "changes", "targets",
                                     "about", "slot", "category", "title")}
    entry["slug"] = slug
    if "online" in manifest:
        entry["online"] = manifest["online"]
    entry["url"] = "%s/%s/%s.zip" % (base_url.rstrip("/"), type_id, urllib.parse.quote(slug))
    entry["sha256"] = hashlib.sha256(open(zip_path, "rb").read()).hexdigest()
    entry["size"] = os.path.getsize(zip_path)
    return entry


def main():
    global OPENSWOS
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="where the zips and index.json go")
    ap.add_argument("--cache", help="SWOS 2020's packs, shared with dlc2020_to_mods.py "
                                    "(default: <out>/.cache)")
    ap.add_argument("--types", default="pitch,teamdb", help="comma-separated: pitch, teamdb")
    ap.add_argument("--only", help="only packs whose title contains this (case-insensitive)")
    ap.add_argument("--limit", type=int, default=0, help="at most this many packs a type")
    ap.add_argument("--openswos", default=os.path.join(HERE, "..", "..", "OpenSWOS-main"),
                    help="an OpenSWOS checkout, for its RNC unpacker (scripts/swos_team_file.py)")
    ap.add_argument("--base-data", help="96/97's data/ with its TEAM.NNN files, to compare "
                                        "team databases with (needed for teamdb)")
    ap.add_argument("--base-url", default="http://www.sensiblesoccer.de/swos2020/dlc/swosutd/amiga",
                    help="where the zips are hosted; each url is <base-url>/<type>/<slug>.zip")
    args = ap.parse_args()
    OPENSWOS = os.path.abspath(args.openswos)

    types = [t.strip() for t in args.types.split(",") if t.strip()]
    for t in types:
        if t not in TYPES:
            sys.exit("dlc2020_to_amiga: %s is not a supported type (%s)" % (t, ", ".join(TYPES)))
    if "teamdb" in types and not args.base_data:
        sys.exit("dlc2020_to_amiga: team databases need --base-data (96/97's data/)")
    os.makedirs(args.out, exist_ok=True)
    cache = args.cache or os.path.join(args.out, ".cache")
    seven = pc.find_7z()
    registry = json.load(open(UUIDS, encoding="utf-8")) if os.path.exists(UUIDS) else {}

    index_path = os.path.join(args.out, "index.json")
    index = {"format": "openswos-mod-index", "version": 1, "mods": []}
    if os.path.exists(index_path):
        index = json.load(open(index_path, encoding="utf-8"))
    by_uuid = {m["uuid"]: m for m in index["mods"]}

    failures = 0
    for t in types:
        rows = pc.catalogue(t, cache)
        if args.only:
            rows = [r for r in rows if args.only.lower() in r["title"].lower()]
        if args.limit:
            rows = rows[:args.limit]
        print("%s: %d pack(s)" % (t, len(rows)))
        for row in rows:
            try:
                archive = pc.pack_file(t, row["title"], cache)
                entry, placed, notes = convert(t, row, archive, args.out, registry, seven,
                                               args.base_url, args.base_data)
            except SkipPack as e:
                print("  left out %s: %s" % (row["title"], e))
                continue
            except Exception as e:     # one bad pack does not stop the rest
                failures += 1
                print("  FAILED %s: %s" % (row["title"], e))
                continue
            by_uuid[entry["uuid"]] = entry
            print("  %-56s %3d file(s), %6.1f KB%s" % (row["title"][:56], placed, entry["size"] / 1024,
                                                      (", " + "; ".join(notes)) if notes else ""))
            with open(UUIDS, "w", encoding="utf-8") as f:
                json.dump(dict(sorted(registry.items())), f, indent=1, ensure_ascii=False)
                f.write("\n")

    index["mods"] = sorted(by_uuid.values(), key=lambda m: (m["category"], m["name"].lower()))
    with open(index_path, "w", encoding="utf-8") as f:
        json.dump(index, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("index: %s (%d mods)%s" % (index_path, len(index["mods"]),
                                     ", %d FAILED" % failures if failures else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
