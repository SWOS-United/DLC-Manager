#!/usr/bin/env python3
"""dlc2020_to_mods.py -- turn SWOS 2020's server DLC into OpenSWOS / SWOS United DLC mods.

    scripts/dlc2020_to_mods.py --out dist/mods                    every supported type
    scripts/dlc2020_to_mods.py --out dist/mods --types pitch,fx --limit 2
    scripts/dlc2020_to_mods.py --out dist/mods --base-url https://example.org/swos/mods

For each pack in http://www.sensiblesoccer.de/swos2020/dlc/<type>/<type>-info.csv this
downloads <name>.7z (cached), lays its files out where the game's mod overlay reads them
(swos_data_path looks in every enabled mod, at the same relative path, before the game's
own files), writes a mod.json, zips the folder, and adds the mod to an index.json in the
format agreed with OpenSWOS (docs/MODS.md §10 there):

    {"format": "openswos-mod-index", "version": 1,
     "mods": [{uuid, slug, name, version, author, changes, slot, targets, url, sha256, size, about}]}

Layouts (checked against OpenSWOS's readers, 2026-11-09):

    commentary  audio/commentary/<event>/*  -> sound_dos/commentary/<event>/*     slot commentary
    fx          audio/fx/*.{raw,ogg,wav,    -> sound_dos/fx/ or sound_dos/hard/,    slot fx
                mp3,aiff}                      lower case, as the PC sound table
                                               (src/dos_sound_tables.h) names each;
                                               the engine plays all five, mixed to mono
    tmusic      <title>.mp3                 -> sound_dos/title.mp3                  slot title_music
    mmusic      <title>.mp3                 -> sound_dos/menu.mp3                   slot menu_music
    pitch       PITCH1-6.BLK/.DAT, SPRITE,  -> grafs_2020/... (the whole set:       slot pitch
                CHARSET, SCORE, TEAM1-3 ...    the 2020 graphics tier reads them
                                               all from one folder)
    bgi2        <title>.bmp                 -> images/<title>.png, several          no slot
                                               pictures a mod ("galleries", by author)

All are "cosmetic". Each needs a setting the DLC manager makes when it turns the mod on:
commentary and fx [audio] style = pc; music [audio] music = pc (menu music also
menu_music = mp3); a pitch [graphics] style = 2020; a gallery's pictures are chosen on the
backgrounds mod's page.

    teamdb      data/TEAM.NNN, *.TMD,       -> data_json/teams/<COUNTRY>.json (OpenSWOS's slot teamdb
                POOLPLYR.DAT, career.opt       decode_data.py); data/*.TMD, POOLPLYR.DAT "data",
                                               and any TEAM.NNN 96/97 has no JSON for,   online false
                                               as binaries; data_json/teamdb.json and
                                               data_json/translations/<lang>.json
                                               (OpenSWOS docs/MODS.md §10.4)

A team database needs an OpenSWOS checkout (--openswos): its decoder writes the team files,
and the shipped 96/97 teams tell which of a database's teams are unchanged (they keep the
original's ids; any other team gets ids of its own, derived from the mod's uuid so that a
re-conversion gives the same ones). Its renames -- what SWOS 2020's exe patched per database
in options.cpp changeLeagueNames() -- come from scripts/dlc2020_teamdb_renames.json, chosen
by the database's season year (the title's first year) and, for two, its folder name. The
league rules SWOS 2020 applied at a career's start (fixupLeague) are the engine's, driven by
teamdb.json.

Cups are not converted here: the tournaments are in OpenSWOS's canonical classics mod, and
the team-only collections become team packs (150..249).

Each mod's uuid (a uuid4) and slug are made the first time it is converted and kept in
scripts/dlc2020_uuids.json, so converting again gives the same mod the same identity and
folder. Commit that file whenever it changes.
"""
import argparse
import csv
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
import uuid
import zipfile

SERVER = "http://www.sensiblesoccer.de/swos2020/dlc"
HERE = os.path.dirname(os.path.abspath(__file__))
UUIDS = os.path.join(HERE, "dlc2020_uuids.json")


def map_commentary(rel):
    if rel.lower().startswith("audio/commentary/"):
        return "sound_dos/commentary/" + rel[len("audio/commentary/"):]
    return None


# The PC sound table's files (OpenSWOS src/dos_sound_tables.h), by the folder each is read
# from. A pack's RAW with any other name (awaysubl, homesubl, goalkick, ref_wh) is never read.
FX_DIR = {n: "fx" for n in (
    "bgcrd3l bouncex chant10l chant4l chant8l comblue combrown comgreen comred comwhite comyelo "
    "endgamew foul homegoal kickx missgoal whistle").split()}
FX_DIR.update({n: "hard" for n in (
    "boowhisl cheer easy erewego fivenil fournil homewinl notsing onenil threenil twonil").split()})


FX_EXTENSIONS = (".raw", ".ogg", ".wav", ".mp3", ".aiff")   # what the engine plays (a3b8f42a)


def map_fx(rel):
    low = rel.lower()
    if not low.startswith("audio/fx/"):
        return None
    name, ext = os.path.splitext(low[len("audio/fx/"):])
    if "/" in name or name not in FX_DIR or ext not in FX_EXTENSIONS:
        return None
    return "sound_dos/%s/%s%s" % (FX_DIR[name], name, ext)


def map_music(target):
    def m(rel):
        return target if rel.lower().endswith(".mp3") else None
    return m


def map_pitch(rel):
    # Flat in the archive; anything in a folder is kept beneath grafs_2020/ as it was.
    if rel.lower().endswith((".txt", ".nfo")):
        return None
    return "grafs_2020/" + rel


# type id -> (slot, changes, mapper, label used in the mod's name, slug prefix)
TYPES = {
    "commentary": ("commentary", "cosmetic", map_commentary, "Commentary", "cm"),
    "fx": ("fx", "cosmetic", map_fx, "Sound effects", "fx"),
    "tmusic": ("title_music", "cosmetic", map_music("sound_dos/title.mp3"), "Title music", "tm"),
    "mmusic": ("menu_music", "cosmetic", map_music("sound_dos/menu.mp3"), "Menu music", "mm"),
    "pitch": ("pitch", "cosmetic", map_pitch, "Pitch", "pt"),
    "bgi2": ("", "cosmetic", None, "Backgrounds", "bg"),     # galleries: convert_gallery()
    "teamdb": ("teamdb", "data", None, "Team database", "td"),  # convert_teamdb()
}
SLUG_MAX = 31          # OpenSWOS MODS_ID_MAX - 1: a longer folder name is left out
IMAGE_NAME_MAX = 63    # a background's file name, as the backgrounds page lists it
GALLERY_MIN = 5        # an author with fewer pictures goes in the "various" gallery

ABOUT_NOTE = {
    "commentary": "Heard with the PC sound ([audio] style = pc).",
    "fx": "Heard with the PC sound ([audio] style = pc).",
    "tmusic": "Played in the intro with the PC music ([audio] music = pc).",
    "mmusic": "Played in the menus with the PC music ([audio] music = pc, menu_music = mp3).",
    "pitch": "Seen with SWOS 2020's graphics ([graphics] style = 2020).",
    "bgi2": "Menu backgrounds, chosen on the backgrounds mod's page.",
    "teamdb": "Offline only: careers, seasons and friendlies, not online play.",
}


# ---- the server -----------------------------------------------------------------------

def fetch(url, dest):
    tmp = dest + ".part"
    with urllib.request.urlopen(url, timeout=60) as r, open(tmp, "wb") as f:
        if r.status != 200:
            raise IOError("%s: HTTP %d" % (url, r.status))
        shutil.copyfileobj(r, f, 1 << 20)
    os.replace(tmp, dest)


def catalogue(type_id, cache):
    path = os.path.join(cache, type_id, type_id + "-info.csv")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fetch("%s/%s/%s-info.csv" % (SERVER, type_id, type_id), path)
    raw = open(path, "rb").read()
    if raw.lstrip().startswith(b"<"):
        raise IOError("%s: the server sent a web page, not a catalogue" % type_id)
    text = raw.decode("utf-8") if _is_utf8(raw) else raw.decode("latin-1")
    rows = []
    for i, row in enumerate(csv.reader(io.StringIO(text))):
        if i == 0 or not row or row[0].startswith("#"):
            continue
        row += [""] * (4 - len(row))
        rows.append({"title": row[0].strip(), "author": row[1].strip(),
                     "version": row[2].strip(), "desc": row[3].strip()})
    return rows


def _is_utf8(b):
    try:
        b.decode("utf-8")
        return True
    except UnicodeDecodeError:
        return False


def pack_file(type_id, title, cache):
    name = title.replace("/", "_")            # TUtil.replaceSlashToUnderbar
    path = os.path.join(cache, type_id, name + ".7z")
    if not os.path.exists(path):
        fetch("%s/%s/%s.7z" % (SERVER, type_id, urllib.parse.quote(name)), path)
    return path


# ---- 7z ---------------------------------------------------------------------------------

def find_7z():
    for exe in ("7z", "7za", "7zz"):
        p = shutil.which(exe)
        if p:
            return p, False
    for p in ("/mnt/c/Program Files/7-Zip/7z.exe", r"C:\Program Files\7-Zip\7z.exe"):
        if os.path.exists(p):
            return p, p.startswith("/mnt/")      # a Windows 7z.exe run from WSL needs Windows paths
    return None, False


def extract(archive, dest, seven):
    exe, wsl = seven
    if exe is None:
        try:
            import py7zr
        except ImportError:
            sys.exit("dlc2020_to_mods: needs 7-Zip (7z on the PATH) or the py7zr module")
        with py7zr.SevenZipFile(archive) as z:
            z.extractall(dest)
        return
    a, d = archive, dest
    if wsl:
        a = subprocess.check_output(["wslpath", "-w", archive], text=True).strip()
        d = subprocess.check_output(["wslpath", "-w", dest], text=True).strip()
    subprocess.run([exe, "x", "-y", "-bso0", "-bsp0", "-o" + d, a],
                   check=True, stdin=subprocess.DEVNULL)


# ---- one pack -> one mod ----------------------------------------------------------------

def identity(registry, key, type_id, title):
    """The (uuid, slug) a mod keeps for ever: made once, then read from the registry.

    The slug is "<prefix>-<title>" in lower case, letters, digits and '-', cut to SLUG_MAX;
    when cutting or another mod already has it, the end becomes a short hash of the key."""
    have = registry.get(key)
    if isinstance(have, dict):
        return have["uuid"], have["slug"]
    taken = {v["slug"] for v in registry.values() if isinstance(v, dict)}
    body = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")
    slug = "%s-%s" % (TYPES[type_id][4], body)
    if len(slug) > SLUG_MAX or slug in taken:
        tag = hashlib.sha1(key.encode("utf-8")).hexdigest()[:5]
        slug = slug[:SLUG_MAX - len(tag) - 1].rstrip("-") + "-" + tag
    entry = {"uuid": str(uuid.uuid4()), "slug": slug}
    registry[key] = entry
    return entry["uuid"], entry["slug"]


def convert(type_id, row, archive, out_dir, uuids, seven, base_url):
    slot, changes, mapper, label, _ = TYPES[type_id]
    key = "%s/%s" % (type_id, row["title"])
    mod_uuid, slug = identity(uuids, key, type_id, row["title"])

    work = tempfile.mkdtemp(prefix="dlc2020-")
    try:
        raw = os.path.join(work, "raw")
        mod = os.path.join(work, slug)
        os.makedirs(raw)
        extract(archive, raw, seven)

        placed, skipped = 0, []
        for root, _, files in os.walk(raw):
            for f in files:
                rel = os.path.relpath(os.path.join(root, f), raw).replace(os.sep, "/")
                target = mapper(rel)
                if target is None:
                    skipped.append(rel)
                    continue
                dst = os.path.join(mod, *target.split("/"))
                if os.path.exists(dst):
                    raise ValueError("two files map to %s" % target)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(os.path.join(root, f), dst)
                placed += 1
        if placed == 0:
            raise ValueError("nothing in the pack is a %s file" % type_id)

        manifest = {
            "uuid": mod_uuid,
            "id": slug,
            "name": "%s: %s" % (label, row["title"]),
            "version": row["version"] or "1.0",
            "author": row["author"],
            "about": "%s from SWOS 2020's DLC. %s" % (label, ABOUT_NOTE[type_id]),
            "targets": ["sdl"],
            "changes": changes,
            "slot": slot,
            "title": row["title"],
            "category": type_id,
            "swos2020": {"type": type_id, "title": row["title"]},
        }
        return finish(mod, manifest, type_id, out_dir, base_url), placed, skipped
    finally:
        shutil.rmtree(work, ignore_errors=True)


def finish(mod, manifest, type_id, out_dir, base_url):
    """Writes mod.json, zips the folder, and returns the mod's index entry."""
    slug = manifest["id"]
    if not manifest.get("slot"):
        manifest.pop("slot", None)
    with open(os.path.join(mod, "mod.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
        f.write("\n")
    zip_dir = os.path.join(out_dir, type_id)
    os.makedirs(zip_dir, exist_ok=True)
    zip_path = os.path.join(zip_dir, slug + ".zip")
    _zip(mod, zip_path)
    entry = {k: manifest[k] for k in ("uuid", "name", "version", "author", "changes", "targets",
                                     "about") }
    entry["slot"] = manifest.get("slot", "")
    entry["slug"] = slug
    # Beyond §10.1, for managers that group by SWOS 2020's types: the type, and
    # the pack's own title (the name carries a "Pitch: "-style prefix for the game's
    # MODS screen, which a list already headed "Pitch" does not need).
    entry["category"] = manifest["category"]
    entry["title"] = manifest["title"]
    entry["url"] = "%s/%s/%s.zip" % (base_url.rstrip("/"), type_id, urllib.parse.quote(slug))
    entry["sha256"] = hashlib.sha256(open(zip_path, "rb").read()).hexdigest()
    entry["size"] = os.path.getsize(zip_path)
    return entry


# ---- team databases -----------------------------------------------------------------------

RENAMES = os.path.join(HERE, "dlc2020_teamdb_renames.json")
LANGUAGES = ("en", "de", "fr", "es", "it", "nl")   # most renames are proper nouns: one text for all

# SWOS 2020's El Salvador league (team file 51) for the two databases it keyed by folder
# name (init.cpp fixupLeague), as teamdb.json "leagues" overrides.
EL_SALVADOR = "51"
FOLDER_LEAGUES = {
    "2020_21 - SWOS Taraji's 20_21": {EL_SALVADOR: {"divisions": [
        {"promoted": 0, "promoted_po": 0, "relegated": 0, "relegated_po": 0}] * 4}},
    "2021_22 - European Reserve Teams": {EL_SALVADOR: {"divisions": [
        {"promoted": 0, "promoted_po": 0, "relegated": 1, "relegated_po": 0},
        {"promoted": 1, "promoted_po": 0, "relegated": 1, "relegated_po": 0},
        {"promoted": 1, "promoted_po": 0, "relegated": 1, "relegated_po": 0},
        {"promoted": 1, "promoted_po": 0, "relegated": 0, "relegated_po": 0}]}},
}
# All National Teams turned El Salvador's four divisions into one (options.cpp changeLeagueNames).
FOLDER_LEAGUES["All National Teams"] = {EL_SALVADOR: {"divisions_count": 1}}
DEFAULT_SEASON = 1996        # SWOS 2020's own default, for a database whose title has no year


def season_of(title):
    m = re.match(r"(\d{4})", title)
    return int(m.group(1)) if m else None


def rename_rules(title):
    """The options.cpp rename functions SWOS 2020 ran for this database, in its order
    (changeLeagueNames(year))."""
    folder = title.replace("/", "_")
    if "VIVA SWOS" in folder:
        return []
    if "All National Teams" in folder:
        return ["allNationalTeams"]
    year = season_of(title) or DEFAULT_SEASON
    if year >= 2019:
        rules = ["2019"]
        if "2020_21 - SWOS Taraji's 20_21" in folder:
            rules.append("2019_TarajiMod")
        rules += [r for y, r in ((2021, "2021"), (2022, "2022"), (2023, "2023")) if year >= y]
        if "2021_22 - European Reserve Teams" in folder:
            rules.append("2021_GorzoMod")
        return rules
    return {1990: ["1990"], 1991: ["1991"], 2008: ["2008"]}.get(year, [])


def merged_renames(title):
    table = json.load(open(RENAMES, encoding="utf-8"))
    out = {"strings": {}, "countries": {}, "nationality_codes": {}}
    for rule in rename_rules(title):
        for part in out:
            out[part].update(table[rule][part])
    return out


def convert_teamdb(row, archive, out_dir, registry, seven, base_url, openswos):
    if not openswos:
        raise ValueError("team databases need --openswos (an OpenSWOS checkout with its data)")
    sys.path.insert(0, os.path.join(openswos, "scripts"))
    from assign_team_uuids import checksum, team_uuid, player_uuid   # the game's own

    key = "teamdb/%s" % row["title"]
    mod_uuid, slug = identity(registry, key, "teamdb", row["title"])
    work = tempfile.mkdtemp(prefix="dlc2020-")
    try:
        raw = os.path.join(work, "raw")
        mod = os.path.join(work, slug)
        os.makedirs(raw)
        extract(archive, raw, seven)
        data = None
        for dp, _, fs in os.walk(raw):
            if any(f.upper().startswith("TEAM.") for f in fs):
                data = dp
                break
        if not data:
            raise ValueError("no TEAM.NNN files in the pack")
        empty = [f for f in os.listdir(data) if f.upper().startswith("TEAM.")
                 and os.path.getsize(os.path.join(data, f)) == 0]
        for f in empty:                  # SWOS 2020 skipped an empty file (2011_12 has a 0-byte TEAM.059)
            os.remove(os.path.join(data, f))

        teams_dir = os.path.join(mod, "data_json", "teams")
        os.makedirs(teams_dir)
        r = subprocess.run([sys.executable, "-I", os.path.join(openswos, "scripts", "decode_data.py"),
                            "--data-dir", os.path.abspath(data), "--dump-json", os.path.abspath(teams_dir),
                            "--compact"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise ValueError("decode_data.py: " + (r.stderr or r.stdout).strip()[-300:])

        # The game reads a country as data_json/teams/<COUNTRY>.json, the shipped file's name;
        # the decoder names one it reads from elsewhere COUNTRY_<n>.json. A team file 96/97 has
        # no JSON for is never looked up by that name: it goes as the pack's own binary
        # data/TEAM.NNN, read through the overlay (MODS.md §10.4). The European cups' TMDs and
        # the pool are read as binaries too, so they go in data/ as they are, not as JSON.
        names = SHIPPED_NAMES(openswos)
        extra = []
        bin_dir = os.path.join(mod, "data")
        os.makedirs(bin_dir)
        for name in os.listdir(teams_dir):
            m = re.fullmatch(r"COUNTRY_(\d+)\.json", name)
            if m and int(m.group(1)) in names:
                os.replace(os.path.join(teams_dir, name), os.path.join(teams_dir, names[int(m.group(1))]))
            elif m:
                extra.append(int(m.group(1)))
                os.remove(os.path.join(teams_dir, name))
            elif name.upper() in ("EUROCUP.JSON", "EUROCWC.JSON", "UEFACUP.JSON", "POOLPLYR.JSON"):
                os.remove(os.path.join(teams_dir, name))
        wanted = {"TEAM.%03d" % n for n in extra} | {"EUROCUP.TMD", "EUROCWC.TMD", "UEFACUP.TMD", "POOLPLYR.DAT"}
        for f in os.listdir(data):
            if f.upper() in wanted:
                shutil.copyfile(os.path.join(data, f), os.path.join(bin_dir, f.upper()))

        # Ids: a team the same as 96/97's at its place keeps the original's (derived) ids;
        # any other is a new team, with ids derived from this mod's uuid.
        ns = uuid.UUID(mod_uuid)
        kept = new = 0
        for name in sorted(os.listdir(teams_dir)):
            path = os.path.join(teams_dir, name)
            doc = json.load(open(path, encoding="utf-8"))
            if doc.get("type") != "team":
                continue
            for t in doc["teams"]:
                country, ordinal = t["team"]["country_number"], t["team"]["team_ordinal"]
                cs = checksum(t["players"])
                same = cs == SHIPPED_CHECKSUMS(openswos).get((country, ordinal))
                if same:
                    tid = team_uuid(country, ordinal)
                    pids = [player_uuid(country, ordinal, i) for i in range(len(t["players"]))]
                    kept += 1
                else:
                    tid = str(uuid.uuid5(ns, "team/%d/%d" % (country, ordinal)))
                    pids = [str(uuid.uuid5(ns, "team/%d/%d/player/%d" % (country, ordinal, i)))
                            for i in range(len(t["players"]))]
                    new += 1
                rest = {k: v for k, v in t.items() if k not in ("uuid", "checksum")}
                t.clear()
                t.update({"uuid": tid, "checksum": cs})
                t.update(rest)
                for p, pid in zip(t["players"], pids):
                    others = {k: v for k, v in p.items() if k != "uuid"}
                    p.clear()
                    p["uuid"] = pid
                    p.update(others)
            with open(path, "w", encoding="utf-8") as f:
                json.dump(doc, f, separators=(",", ":"), ensure_ascii=False)

        renames = merged_renames(row["title"])
        if renames["strings"] or renames["countries"]:
            tr = os.path.join(mod, "data_json", "translations")
            os.makedirs(tr)
            body = {"strings": renames["strings"], "countries": renames["countries"]}
            for lang in LANGUAGES:
                with open(os.path.join(tr, lang + ".json"), "w", encoding="utf-8") as f:
                    json.dump(body, f, indent=1, ensure_ascii=False)
                    f.write("\n")

        fixes = "1111111"                  # what SWOS 2020's launcher assumed with no career.opt
        for dp, _, fs in os.walk(raw):
            for f in fs:
                if f.lower() == "career.opt":
                    first = open(os.path.join(dp, f), encoding="latin-1").readline().strip()
                    if re.fullmatch(r"[01]{7}", first):
                        fixes = first
        season = season_of(row["title"])
        teamdb = {"format": "openswos-teamdb", "version": 1, "name": row["title"],
                  "season_year": season or DEFAULT_SEASON, "fixes": fixes,
                  # SWOS 2020's launcher passed --always-points-per-win-3=1 for every database.
                  "points_for_win": 3}
        if renames["nationality_codes"]:
            teamdb["nationality_codes"] = renames["nationality_codes"]
        for folder, leagues in FOLDER_LEAGUES.items():
            if folder in row["title"].replace("/", "_"):
                teamdb["leagues"] = leagues
        with open(os.path.join(mod, "data_json", "teamdb.json"), "w", encoding="utf-8") as f:
            json.dump(teamdb, f, indent=1, ensure_ascii=False)
            f.write("\n")

        for dp, _, fs in os.walk(raw):          # the authors' notes travel with their data
            for f in fs:
                if f.lower() in ("readme.txt", "readme.md"):
                    shutil.copyfile(os.path.join(dp, f), os.path.join(mod, f.lower()))

        manifest = {
            "uuid": mod_uuid,
            "id": slug,
            "name": "Team database: %s" % row["title"],
            "version": row["version"] or "1.0",
            "author": row["author"],
            "about": "%s from SWOS 2020's DLC (%d teams as 96/97's, %d changed). %s" % (
                "Team database", kept, new, ABOUT_NOTE["teamdb"]),
            "targets": ["sdl"],
            "changes": "data",
            "slot": "teamdb",
            "online": False,
            "title": row["title"],
            "category": "teamdb",
            "swos2020": {"type": "teamdb", "title": row["title"], "season_year": season},
        }
        placed = sum(len(fs) for _, _, fs in os.walk(mod))
        notes = ["as binaries (no 96/97 JSON or league): %s" % ", ".join("TEAM.%03d" % n for n in sorted(extra))] if extra else []
        return finish(mod, manifest, "teamdb", out_dir, base_url), placed, notes
    finally:
        shutil.rmtree(work, ignore_errors=True)


_shipped = {}
SHIPPED_TEAMS = None      # --openswos-teams: the 96/97 team files, when not in --openswos


def SHIPPED_NAMES(openswos):
    """{country number: its file name} of the game's own team files."""
    d = SHIPPED_TEAMS or os.path.join(openswos, "bin", "data_json", "teams")
    out = {}
    for name in os.listdir(d):
        doc = json.load(open(os.path.join(d, name), encoding="utf-8"))
        if doc.get("type") == "team":
            out[doc["country"]] = name
    return out


def SHIPPED_CHECKSUMS(openswos):
    """{(country, ordinal): checksum} of the game's own 96/97 teams."""
    if openswos not in _shipped:
        out = {}
        d = SHIPPED_TEAMS or os.path.join(openswos, "bin", "data_json", "teams")
        for name in os.listdir(d):
            doc = json.load(open(os.path.join(d, name), encoding="utf-8"))
            if doc.get("type") == "team":
                for t in doc["teams"]:
                    out[(t["team"]["country_number"], t["team"]["team_ordinal"])] = t.get("checksum")
        if not out:
            raise ValueError("no 96/97 team files in %s" % d)
        _shipped[openswos] = out
    return _shipped[openswos]


# ---- background galleries -------------------------------------------------------------------

def image_name(title, used):
    """<title>.png, safe as a file name everywhere and unique across every gallery (the
    backgrounds page has one choice per file name)."""
    base = re.sub(r'[<>:"/\\|?*\x00-\x1f]+', "_", title).strip(" .") or "background"
    base = base[:IMAGE_NAME_MAX - 4].rstrip(" .")
    name, n = base + ".png", 2
    while name.lower() in used:
        suffix = " %d" % n
        name = base[:IMAGE_NAME_MAX - 4 - len(suffix)].rstrip(" .") + suffix + ".png"
        n += 1
    used.add(name.lower())
    return name


def galleries(rows):
    """Groups the background packs into galleries: one per author with GALLERY_MIN pictures
    or more, the rest together. Returns [(gallery title, [rows])], in a stable order."""
    by_author = {}
    for r in rows:
        by_author.setdefault(r["author"] or "-", []).append(r)
    out, various = [], []
    for author in sorted(by_author, key=str.lower):
        group = by_author[author]
        if len(group) >= GALLERY_MIN and author != "-":
            out.append((author, group))
        else:
            various.extend(group)
    if various:
        out.append(("Various", various))
    return out


def convert_gallery(gtitle, rows, cache, out_dir, registry, seven, base_url, used_names):
    from PIL import Image

    key = "bgi2-gallery/%s" % gtitle
    mod_uuid, slug = identity(registry, key, "bgi2", gtitle)
    # The gallery's version moves whenever what is in it does, so a manager sees an update.
    sig = hashlib.sha1(json.dumps(sorted((r["title"], r["version"]) for r in rows)).encode()).hexdigest()
    rec = registry[key]
    if rec.get("sig") != sig:
        rec["revision"] = rec.get("revision", 0) + 1
        rec["sig"] = sig

    work = tempfile.mkdtemp(prefix="dlc2020-")
    try:
        mod = os.path.join(work, slug)
        os.makedirs(os.path.join(mod, "images"))
        placed, skipped = 0, []
        for r in rows:
            archive = pack_file("bgi2", r["title"], cache)
            raw = os.path.join(work, "raw")
            shutil.rmtree(raw, ignore_errors=True)
            os.makedirs(raw)
            extract(archive, raw, seven)
            pics = [os.path.join(dp, f) for dp, _, fs in os.walk(raw) for f in fs
                    if f.lower().endswith((".bmp", ".png", ".jpg", ".jpeg"))]
            if len(pics) != 1:
                skipped.append("%s (%d pictures)" % (r["title"], len(pics)))
                continue
            with Image.open(pics[0]) as im:
                im.convert("RGB").save(os.path.join(mod, "images", image_name(r["title"], used_names)),
                                       optimize=True)
            placed += 1
        authors = sorted({r["author"] for r in rows if r["author"]}, key=str.lower)
        manifest = {
            "uuid": mod_uuid,
            "id": slug,
            "name": "Backgrounds: %s" % gtitle,
            "version": "1.%d" % rec["revision"],
            "author": gtitle if gtitle != "Various" else ", ".join(authors),
            "about": "%d menu backgrounds from SWOS 2020's DLC. %s" % (placed, ABOUT_NOTE["bgi2"]),
            "targets": ["sdl"],
            "changes": "cosmetic",
            "title": gtitle,
            "category": "bgi2",
            "swos2020": {"type": "bgi2", "titles": sorted(r["title"] for r in rows)},
        }
        return finish(mod, manifest, "bgi2", out_dir, base_url), placed, skipped
    finally:
        shutil.rmtree(work, ignore_errors=True)


def _zip(folder, zip_path):
    # The mod's folder's contents at the zip's root, mod.json at its top (MODS.md §10.1).
    # Already-compressed files are stored, the rest deflated.
    tmp = zip_path + ".part"
    with zipfile.ZipFile(tmp, "w") as z:
        for root, _, files in os.walk(folder):
            for f in sorted(files):
                full = os.path.join(root, f)
                arc = os.path.relpath(full, folder).replace(os.sep, "/")
                stored = f.lower().endswith((".mp3", ".ogg", ".zip", ".png", ".jpg"))
                z.write(full, arc, zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED)
    os.replace(tmp, zip_path)


def save_registry(registry):
    with open(UUIDS, "w", encoding="utf-8") as f:
        json.dump(dict(sorted(registry.items())), f, indent=1, ensure_ascii=False)
        f.write("\n")


# ---- main -------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="where the zips and index.json go")
    ap.add_argument("--cache", help="where downloads are kept (default: <out>/.cache)")
    ap.add_argument("--types", default=",".join(t for t in TYPES if t != "teamdb"), help="comma-separated, of: " + ", ".join(TYPES))
    ap.add_argument("--only", help="convert only packs whose title contains this (case-insensitive)")
    ap.add_argument("--limit", type=int, default=0,
                    help="at most this many packs a type (for bgi2: pictures, before grouping)")
    ap.add_argument("--openswos", help="an OpenSWOS checkout with the game's data (bin/data_json/teams), "
                                       "for team databases: its scripts/decode_data.py and the 96/97 teams")
    ap.add_argument("--openswos-teams", help="the game's 96/97 team JSON, if not <openswos>/bin/data_json/teams "
                                             "(a worktree has no data)")
    ap.add_argument("--base-url", default="https://CHANGE-ME/swos2020-mods",
                    help="where the zips will be hosted; each index entry's url is <base-url>/<type>/<slug>.zip")
    args = ap.parse_args()

    global SHIPPED_TEAMS
    SHIPPED_TEAMS = args.openswos_teams
    types = [t.strip() for t in args.types.split(",") if t.strip()]
    for t in types:
        if t not in TYPES:
            sys.exit("dlc2020_to_mods: %s is not a supported type (%s)" % (t, ", ".join(TYPES)))
    os.makedirs(args.out, exist_ok=True)
    cache = args.cache or os.path.join(args.out, ".cache")
    os.makedirs(cache, exist_ok=True)
    seven = find_7z()
    uuids = json.load(open(UUIDS, encoding="utf-8")) if os.path.exists(UUIDS) else {}
    # An entry from before slugs were kept (a bare uuid string) is made again.
    uuids = {k: v for k, v in uuids.items() if isinstance(v, dict)}

    index_path = os.path.join(args.out, "index.json")
    index = {"format": "openswos-mod-index", "version": 1, "mods": []}
    if os.path.exists(index_path):
        index = json.load(open(index_path, encoding="utf-8"))
    by_uuid = {m["uuid"]: m for m in index["mods"]}

    failures = 0
    for t in types:
        rows = catalogue(t, cache)
        if args.only:
            rows = [r for r in rows if args.only.lower() in r["title"].lower()]
        if args.limit:
            rows = rows[:args.limit]
        print("%s: %d pack(s)" % (t, len(rows)))
        if t == "bgi2":
            used = set()
            for gtitle, group in galleries(rows):
                try:
                    entry, placed, skipped = convert_gallery(gtitle, group, cache, args.out, uuids,
                                                             seven, args.base_url, used)
                except Exception as e:
                    failures += 1
                    print("  FAILED gallery %s: %s" % (gtitle, e))
                    continue
                by_uuid[entry["uuid"]] = entry
                print("  gallery %-50s %4d picture(s), %6.1f MB%s" % (
                    entry["slug"], placed, entry["size"] / 1e6,
                    (", left out: " + ", ".join(skipped)) if skipped else ""))
                save_registry(uuids)
            continue
        for row in rows:
            try:
                archive = pack_file(t, row["title"], cache)
                if t == "teamdb":
                    entry, placed, skipped = convert_teamdb(row, archive, args.out, uuids, seven,
                                                            args.base_url, args.openswos)
                else:
                    entry, placed, skipped = convert(t, row, archive, args.out, uuids, seven,
                                                     args.base_url)
            except Exception as e:     # one bad pack does not stop the rest
                failures += 1
                print("  FAILED %s: %s" % (row["title"], e))
                continue
            by_uuid[entry["uuid"]] = entry
            note = (", note: " + ", ".join(skipped[:4]) + (" ..." if len(skipped) > 4 else "")) if skipped else ""
            print("  %-60s %4d file(s), %6.1f MB%s" % (row["title"][:60], placed, entry["size"] / 1e6, note))
            save_registry(uuids)   # as we go: an interrupted run keeps the ids it handed out

    index["mods"] = sorted(by_uuid.values(), key=lambda m: (m.get("slot") or "", m["name"].lower()))
    with open(index_path, "w", encoding="utf-8") as f:
        json.dump(index, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("index: %s (%d mods)%s" % (index_path, len(index["mods"]),
                                     ", %d FAILED" % failures if failures else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
