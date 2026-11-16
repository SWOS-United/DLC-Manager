// mod_store.h -- SWOS 2020's DLC as OpenSWOS / SWOS United mods.
//
// SWOS United reads DLC as mods (OpenSWOS docs/MODS.md §10, §14): each is a folder in
// the player's mods/ with a mod.json, and it is on when SWOS.ini's
//
//     [mods]
//     enabled = pt-1994-moon-soccer, cm-barry-davies
//
// names it. What is downloadable is listed in one index.json (format
// "openswos-mod-index", version 1), each entry a mod's identity plus the url,
// sha256 and size of its zip. scripts/dlc2020_to_mods.py makes both from SWOS
// 2020's own packs.
//
// A mod's "slot" (pitch, commentary, fx, title_music, menu_music, teamdb, cutscenes_hd) means
// one mod of that slot at a time: turning one on turns the others of its slot
// off, as the game would leave all but the first out anyway.

#pragma once

#include "dlc_catalog.h"

#include <string>
#include <utility>
#include <vector>

namespace swos {

// <base>/mods: where DLC mods are installed (OpenSWOS Mods_Root()).
std::string mods_root(const std::string &base_dir);
// <base>/SWOS.ini: the game's settings, beside the mods.
std::string game_ini_path(const std::string &base_dir);

// Where the index lives: $SWOS_MOD_INDEX, else [DLC] modIndexUrl in
// ini/gameLauncher.ini, else SWOS United's on the SWOS 2020 server
// (http://www.sensiblesoccer.de/swos2020/dlc/swosutd/index.json). Empty when set
// to "off" -- the manager then shows SWOS 2020's own catalogues.
std::string mod_index_url(const std::string &base_dir);

// A mod as listed, with the sidebar category it belongs to.
using CategorisedMod = std::pair<DlcType, DlcEntry>;

// Parses index.json. Entries for other targets than "sdl", or whose category
// cannot be told, are skipped. False, with the reason, if it is not an index
// this manager understands.
bool parse_mod_index(const std::string &body, std::vector<CategorisedMod> &out,
                     std::string &error);

// Every mod installed in mods_root() whose mod.json can be read, with its
// installed version in server_version (DlcCatalog's convention for the local list).
std::vector<CategorisedMod> scan_installed_mods(const std::string &base_dir);

// [mods] enabled, in order.
std::vector<std::string> enabled_mods(const std::string &base_dir);

// Turns `mod` on: appended to [mods] enabled, every other mod of its slot taken
// off, and the settings it needs to be heard or seen made (a pitch: [graphics]
// style = 2020; commentary and effects: [audio] style = pc; music: [audio]
// music = pc, and title_music / menu_music = mp3 -- midi when the mod's song is a
// sound_dos/title.xmi / menu.xmi). `installed` is every
// installed mod, so the slot's others can be found by their slot.
bool turn_mod_on(const std::string &base_dir, const DlcEntry &mod, DlcType category,
                 const std::vector<CategorisedMod> &installed);

// Takes `slug` out of [mods] enabled.
bool turn_mod_off(const std::string &base_dir, const std::string &slug);

// Checks a freshly extracted mod folder against the index entry it came from:
// a mod.json at its root whose uuid is the entry's. "" when it is fine.
std::string check_extracted_mod(const std::string &folder, const DlcEntry &entry);

// --- Pitch maps ----------------------------------------------------------------
//
// A pitch map is not a mod: it is data for the game's canonical "pitchmaps" mod,
// kept with any mod's downloaded data in the player folder's moddata/<slug>/.
// A pack is Tile Agent's own export ("File > Export Package...", its
// docs/FORMAT.md §11): a zip whose root holds package.json ({"format":
// "tileagent-package", "format_version": 1, uuid, name, author, version, about,
// "map": "<file>.tmj", "preview": "<file>.png" or absent}), the map, its tileset
// images and the preview. It is installed as moddata/pitchmaps/<uuid>/, and the
// one in use is SWOS.ini's [mod.pitchmaps] map = <uuid> ("off" for none).

// <base>/moddata/pitchmaps
std::string pitch_maps_root(const std::string &base_dir);

// Every folder there whose package.json can be read, with its folder as the
// slug and its version in server_version.
std::vector<DlcEntry> scan_pitch_maps(const std::string &base_dir);

// [mod.pitchmaps] map: the uuid of the pitch map in use, or "" for none.
std::string selected_pitch_map(const std::string &base_dir);

// Selects `uuid` and turns the pitchmaps mod on, which draws it.
bool turn_pitch_map_on(const std::string &base_dir, const std::string &uuid);

// map = off, if `uuid` is the one selected. The pitchmaps mod stays on: with no
// map it draws the game's own pitches.
bool turn_pitch_map_off(const std::string &base_dir, const std::string &uuid);

// As check_extracted_mod, for a pitch map: a package.json at the root, of
// format tileagent-package, whose uuid is the entry's.
std::string check_extracted_pitch_map(const std::string &folder, const DlcEntry &entry);

// The preview image an installed pitch map's package.json names, or "".
std::string pitch_map_preview(const std::string &folder);

} // namespace swos
