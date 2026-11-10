// mod_store.cpp -- see mod_store.h.

#include "mod_store.h"

#include "ini_file.h"
#include "json.h"
#include "paths.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace swos {

namespace {

// SWOS United's own DLC folder on the SWOS 2020 server (dlc/swosutd/).
constexpr const char *kDefaultModIndex = "http://www.sensiblesoccer.de/swos2020/dlc/swosutd/index.json";

std::string read_text(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string trim(const std::string &s)
{
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool same_text(const std::string &a, const std::string &b)
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

// The sidebar category. An entry the converter wrote says which SWOS 2020 type
// it came from ("category" in the index, "swos2020": {"type"} in mod.json);
// otherwise the slot decides, and a mod with neither is not one of ours to list.
bool category_of(const JsonValue &v, DlcType &out)
{
    std::string id = v["category"].str();
    if (id.empty()) id = v["swos2020"]["type"].str();
    if (!id.empty()) {
        for (DlcType type : all_dlc_types())
            if (id == dlc_type_id(type)) {
                out = type;
                return true;
            }
    }
    static const std::pair<const char *, DlcType> by_slot[] = {
            {"pitch", DlcType::Pitch},           {"commentary", DlcType::Commentary},
            {"fx", DlcType::Fx},                 {"title_music", DlcType::TitleMusic},
            {"menu_music", DlcType::MenuMusic},  {"teamdb", DlcType::TeamDb},
    };
    const std::string slot = v["slot"].str();
    for (const auto &pair : by_slot)
        if (slot == pair.first) {
            out = pair.second;
            return true;
        }
    return false;
}

bool for_sdl(const JsonValue &v)
{
    if (!v["targets"].is_array()) return true;   // says nothing: assume it plays here
    for (const JsonValue &t : v["targets"].items())
        if (t.str() == "sdl") return true;
    return false;
}

// Fields common to an index entry and a mod.json.
DlcEntry entry_from(const JsonValue &v)
{
    DlcEntry e;
    e.uuid = v["uuid"].str();
    e.slug = v["slug"].str(v["id"].str());
    e.title = v["title"].str(v["name"].str(e.slug));
    e.author = v["author"].str();
    e.server_version = v["version"].str("1.0");
    e.about = v["about"].str();
    e.description = e.about;
    e.slot = v["slot"].str();
    e.changes = v["changes"].str();
    e.offline_only = v.has("online") && !v["online"].boolean(true);
    e.url = v["url"].str();
    e.sha256 = v["sha256"].str();
    e.size = static_cast<long long>(v["size"].num(0));
    return e;
}

std::string join_list(const std::vector<std::string> &items)
{
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ", ";
        out += items[i];
    }
    return out;
}

bool write_enabled(const std::string &base_dir, const std::vector<std::string> &list)
{
    const std::string path = game_ini_path(base_dir);
    IniFile ini;
    ini.load(path);
    ini.write_string("mods", "enabled", join_list(list));
    return ini.save(path);
}

} // namespace

std::string mods_root(const std::string &base_dir)
{
    return path_join(base_dir, "mods");
}

std::string game_ini_path(const std::string &base_dir)
{
    return path_join(base_dir, "SWOS.ini");
}

std::string mod_index_url(const std::string &base_dir)
{
    std::string url;
    if (const char *env = std::getenv("SWOS_MOD_INDEX")) url = env;
    if (url.empty()) {
        IniFile ini;
        ini.load(path_join(path_join(base_dir, "ini"), "gameLauncher.ini"));
        url = trim(ini.read_string("DLC", "modIndexUrl", kDefaultModIndex));
    }
    // "off" (or "none") shows SWOS 2020's own catalogues instead.
    if (url == "off" || url == "none") return {};
    return url;
}

bool parse_mod_index(const std::string &body, std::vector<CategorisedMod> &out,
                     std::string &error)
{
    out.clear();
    JsonValue root;
    if (!JsonValue::parse(body, root, error)) {
        error = "the mod index is not JSON (" + error + ")";
        return false;
    }
    if (root["format"].str() != "openswos-mod-index") {
        error = "not a mod index";
        return false;
    }
    if (root["version"].num(0) != 1) {
        error = "a newer mod index than this manager knows (version " +
                std::to_string(static_cast<int>(root["version"].num(0))) + ")";
        return false;
    }
    for (const JsonValue &m : root["mods"].items()) {
        DlcType type;
        if (!m.is_object() || !for_sdl(m) || !category_of(m, type)) continue;
        DlcEntry e = entry_from(m);
        if (e.uuid.empty() || e.slug.empty() || e.url.empty()) continue;
        out.emplace_back(type, std::move(e));
    }
    return true;
}

std::vector<CategorisedMod> scan_installed_mods(const std::string &base_dir)
{
    std::vector<CategorisedMod> out;
    const std::string root = mods_root(base_dir);
    for (const std::string &slug : list_directory(root, true)) {
        if (slug.empty() || slug[0] == '.') continue;   // .<slug>.part: an install in progress
        const std::string text = read_text(path_join(path_join(root, slug), "mod.json"));
        if (text.empty()) continue;
        JsonValue manifest;
        std::string error;
        DlcType type;
        if (!JsonValue::parse(text, manifest, error) || !category_of(manifest, type)) continue;
        DlcEntry e = entry_from(manifest);
        e.slug = slug;                                  // the folder is what [mods] names
        out.emplace_back(type, std::move(e));
    }
    return out;
}

std::vector<std::string> enabled_mods(const std::string &base_dir)
{
    IniFile ini;
    ini.load(game_ini_path(base_dir));
    std::vector<std::string> out;
    std::stringstream in(ini.read_string("mods", "enabled"));
    std::string item;
    while (std::getline(in, item, ',')) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

bool turn_mod_on(const std::string &base_dir, const DlcEntry &mod, DlcType category,
                 const std::vector<CategorisedMod> &installed)
{
    std::vector<std::string> list = enabled_mods(base_dir);
    // Others of the slot off. A trailing '*' (an Amiga drawer's not-cosmetic mark)
    // is not part of the name.
    auto name_of = [](std::string s) {
        if (!s.empty() && s.back() == '*') s.pop_back();
        return s;
    };
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const std::string &item) {
                                  const std::string slug = name_of(item);
                                  if (same_text(slug, mod.slug)) return true;   // re-added below
                                  if (mod.slot.empty()) return false;
                                  for (const auto &other : installed)
                                      if (same_text(other.second.slug, slug) &&
                                          other.second.slot == mod.slot)
                                          return true;
                                  return false;
                              }),
               list.end());
    list.push_back(mod.slug);
    if (!write_enabled(base_dir, list)) return false;

    // What the game needs set for the mod to be heard or seen.
    IniFile ini;
    const std::string path = game_ini_path(base_dir);
    ini.load(path);
    switch (category) {
    case DlcType::Pitch:      ini.write_string("graphics", "style", "2020"); break;
    case DlcType::Commentary:
    case DlcType::Fx:         ini.write_string("audio", "style", "pc"); break;
    case DlcType::TitleMusic:
    case DlcType::MenuMusic: {
        // A recording (sound_dos/<song>.mp3), or the PC game's song as MIDI
        // (sound_dos/<song>.xmi), played on the engine's emulated AdLib card.
        const bool title = category == DlcType::TitleMusic;
        const std::string midi = path_join(path_join(path_join(mods_root(base_dir), mod.slug),
                                                     "sound_dos"), title ? "title.xmi" : "menu.xmi");
        ini.write_string("audio", "music", "pc");
        ini.write_string("audio", title ? "title_music" : "menu_music",
                         file_exists(midi) ? "midi" : "mp3");
        break;
    }
    default: return true;
    }
    return ini.save(path);
}

bool turn_mod_off(const std::string &base_dir, const std::string &slug)
{
    std::vector<std::string> list = enabled_mods(base_dir);
    const size_t before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](std::string item) {
                                  if (!item.empty() && item.back() == '*') item.pop_back();
                                  return same_text(item, slug);
                              }),
               list.end());
    return list.size() == before || write_enabled(base_dir, list);
}

std::string check_extracted_mod(const std::string &folder, const DlcEntry &entry)
{
    const std::string text = read_text(path_join(folder, "mod.json"));
    if (text.empty()) return "the download has no mod.json";
    JsonValue manifest;
    std::string error;
    if (!JsonValue::parse(text, manifest, error)) return "its mod.json is not JSON (" + error + ")";
    if (manifest["uuid"].str() != entry.uuid) return "its mod.json is for a different mod";
    return {};
}

} // namespace swos
