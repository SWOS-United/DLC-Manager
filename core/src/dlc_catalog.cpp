// dlc_catalog.cpp -- see dlc_catalog.h. Ported from TDLCInfoManager
// (loadEx / addServerVersions / downloadDLC / loadCurrentFromIni /
// saveCurrentToIni) in dlc_info_u.pas.

#include "dlc_catalog.h"

#include "http.h"
#include "ini_file.h"
#include "paths.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

namespace swos {

namespace {

struct TypeInfo {
    DlcType type;
    const char *id;
    const char *label;
    PreviewKind preview;
    const char *default_selection;
    bool has_enabled_flag;   // types that also carry "<id>_Enabled"
    bool default_enabled;
};

// Ids, defaults and the _Enabled flags are exactly what loadCurrentFromIni
// used. "bgi2" is deliberate: the Delphi code moved background images to a
// second-generation layout and the directory name went with it.
constexpr std::array<TypeInfo, 17> kTypes{{
    {DlcType::TeamDb, "teamdb", "Team Database", PreviewKind::Image,
     "1996/97 - Original Amiga / PC DOS", true, false},
    {DlcType::Pitch, "pitch", "Pitch", PreviewKind::Image, "2020 - Official 2021", false, false},
    {DlcType::Cup, "cup", "Cup", PreviewKind::Image, "1964 UEFA EURO CUP (Spain)", true, true},
    {DlcType::Commentary, "commentary", "Commentary", PreviewKind::Audio,
     "English - Original 96/97 (Jonathan Pearce)", false, false},
    {DlcType::Fx, "fx", "Sound Effects", PreviewKind::Audio, "Original 96/97", false, false},
    {DlcType::BackgroundImage, "bgi2", "Background Image", PreviewKind::Image, "Blue (DOS HD)",
     false, false},
    {DlcType::TitleMusic, "tmusic", "Title Music", PreviewKind::Audio,
     "AMIGA - Goalscoringsuperstarhero", false, false},
    {DlcType::MenuMusic, "mmusic", "Menu Music", PreviewKind::Audio, "AMIGA - SWOS 96/97", false,
     false},
    {DlcType::SpriteBall, "sprite_ball", "Ball", PreviewKind::Image, "ball-default", true, false},
    {DlcType::SpriteGoalkeeper, "sprite_gk", "Goalkeeper", PreviewKind::Image,
     "team1-main-goalkeeper-default", true, false},
    {DlcType::SpriteGoalNet, "sprite_goalnet", "Goal Net", PreviewKind::Image, "goal-net-default",
     true, false},
    {DlcType::SpriteManager, "sprite_manager", "Manager", PreviewKind::Image, "managers-default",
     true, false},
    {DlcType::SpriteReferee, "sprite_referee", "Referee", PreviewKind::Image, "referee-default",
     true, false},
    {DlcType::SpriteTeam1, "sprite_team1", "Team 1 Kit", PreviewKind::Image, "team1-set1-default",
     true, false},
    {DlcType::SpriteTeam2, "sprite_team2", "Team 2 Kit", PreviewKind::Image, "team2-set1-default",
     true, false},
    {DlcType::SpriteTeam3, "sprite_team3", "Team 3 Kit", PreviewKind::Image, "team3-set1-default",
     true, false},
    {DlcType::SpriteFlag, "sprite_flag", "Corner Flags", PreviewKind::Image, "corner-flags-default",
     true, false},
}};

const TypeInfo &info_for(DlcType type)
{
    for (const auto &info : kTypes)
        if (info.type == type) return info;
    return kTypes[0];
}

std::string trim(const std::string &s)
{
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// One CSV record. TStringList's defaults applied in the Delphi build: comma
// delimiter, '"' quoting, and a doubled quote inside a quoted field is a
// literal quote (AnsiExtractQuotedStr).
std::vector<std::string> split_csv_line(const std::string &line)
{
    std::vector<std::string> fields;
    std::string current;
    bool quoted = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                current += c;
            }
        } else if (c == '"' && current.empty()) {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    fields.push_back(trim(current));
    return fields;
}

std::string read_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

constexpr const char *kDlcSection = "DLC";

} // namespace

const char *dlc_server_root()
{
    return "http://www.sensiblesoccer.de/swos2020";
}

const char *dlc_type_id(DlcType type)
{
    return info_for(type).id;
}

const char *dlc_type_label(DlcType type)
{
    return info_for(type).label;
}

PreviewKind dlc_preview_kind(DlcType type)
{
    return info_for(type).preview;
}

bool is_sprite_type(DlcType type)
{
    return std::string(dlc_type_id(type)).rfind("sprite_", 0) == 0;
}

const std::vector<DlcType> &all_dlc_types()
{
    static const std::vector<DlcType> types = [] {
        std::vector<DlcType> out;
        for (const auto &info : kTypes) out.push_back(info.type);
        return out;
    }();
    return types;
}

const char *dlc_status_label(DlcStatus status)
{
    switch (status) {
    case DlcStatus::New: return "New";
    case DlcStatus::Installed: return "Installed";
    case DlcStatus::Update: return "Update";
    case DlcStatus::Removed: return "Removed";
    }
    return "";
}

std::string title_to_folder(const std::string &title)
{
    std::string out = title;
    std::replace(out.begin(), out.end(), '/', '_');
    return out;
}

std::string folder_to_title(const std::string &folder)
{
    std::string out = folder;
    std::replace(out.begin(), out.end(), '_', '/');
    return out;
}

std::string DlcEntry::folder_name() const
{
    return title_to_folder(title);
}

// ---------------------------------------------------------------------------
// DlcCatalog
// ---------------------------------------------------------------------------

DlcCatalog::DlcCatalog(DlcType type, std::string base_dir)
    : m_type(type), m_base_dir(std::move(base_dir))
{
}

std::vector<DlcEntry> DlcCatalog::parse_csv(const std::string &csv)
{
    std::vector<DlcEntry> entries;
    std::istringstream in(csv);
    std::string line;

    bool first = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) {  // header row, skipped by loadEx's leading readln
            first = false;
            continue;
        }
        if (line.empty() || line[0] == '#') continue;

        const auto fields = split_csv_line(line);
        if (fields.empty() || fields[0].empty()) continue;

        DlcEntry entry;
        entry.title = fields[0];
        if (fields.size() > 1) entry.author = fields[1];
        if (fields.size() > 2) entry.server_version = fields[2];
        if (fields.size() > 3) entry.description = fields[3];
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::string DlcCatalog::type_dir() const
{
    return path_join(path_join(m_base_dir, "dlc"), dlc_type_id(m_type));
}

std::string DlcCatalog::local_catalog_path() const
{
    return path_join(type_dir(), std::string(dlc_type_id(m_type)) + "-info.csv");
}

std::string DlcCatalog::server_catalog_path() const
{
    return path_join(path_join(m_base_dir, "temp"),
                     std::string(dlc_type_id(m_type)) + "-info.csv");
}

std::string DlcCatalog::catalog_url() const
{
    return std::string(dlc_server_root()) + "/dlc/" + dlc_type_id(m_type) + "/" +
           url_encode_segment(std::string(dlc_type_id(m_type)) + "-info.csv");
}

std::string DlcCatalog::package_url(const DlcEntry &entry, bool preview) const
{
    const std::string name = entry.folder_name() + (preview ? "_preview" : "") + ".7z";
    return std::string(dlc_server_root()) + "/dlc/" + dlc_type_id(m_type) + "/" +
           url_encode_segment(name);
}

std::string DlcCatalog::package_path(const DlcEntry &entry, bool preview) const
{
    return path_join(type_dir(), entry.folder_name() + (preview ? "_preview" : "") + ".7z");
}

std::string DlcCatalog::install_dir(const DlcEntry &entry) const
{
    return path_join(type_dir(), entry.folder_name());
}

bool DlcCatalog::is_extracted(const DlcEntry &entry) const
{
    return directory_exists(install_dir(entry));
}

bool DlcCatalog::load()
{
    m_server.clear();
    m_local.clear();

    const std::string server_csv = read_file(server_catalog_path());
    if (!server_csv.empty()) m_server = parse_csv(server_csv);

    // First run seeds the local catalogue from the shipped default, as loadEx
    // did with <type>-info_default.csv.
    std::string local_path = local_catalog_path();
    if (!file_exists(local_path)) {
        const std::string fallback =
                path_join(type_dir(), std::string(dlc_type_id(m_type)) + "-info_default.csv");
        if (file_exists(fallback)) {
            ensure_directory(type_dir());
            std::ofstream out(local_path, std::ios::binary);
            out << read_file(fallback);
        }
    }
    const std::string local_csv = read_file(local_path);
    if (!local_csv.empty()) m_local = parse_csv(local_csv);

    merge();
    return !m_server.empty() || !m_local.empty();
}

bool DlcCatalog::looks_like_catalog(const std::string &body)
{
    if (body.empty()) return false;

    // Anything that opens as markup is the soft-404 page, not a catalogue.
    const std::string head = body.substr(0, std::min<size_t>(body.size(), 512));
    std::string lowered = head;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowered.find("<!doctype") != std::string::npos) return false;
    if (lowered.find("<html") != std::string::npos) return false;
    if (lowered.find("<?xml") != std::string::npos) return false;

    // The real files open with a header row: "Name,Author,Version,Desc".
    std::istringstream in(body);
    std::string first;
    if (!std::getline(in, first)) return false;
    if (!first.empty() && first.back() == '\r') first.pop_back();
    if (first.find('<') != std::string::npos) return false;
    return std::count(first.begin(), first.end(), ',') >= 2;
}

bool DlcCatalog::set_server_csv(const std::string &csv)
{
    if (!looks_like_catalog(csv)) return false;

    m_server = parse_csv(csv);

    ensure_directory(parent_path(server_catalog_path()));
    std::ofstream out(server_catalog_path(), std::ios::binary);
    out << csv;
    out.close();

    merge();
    return true;
}

// addServerVersions, plus the local-only case the Delphi merge dropped.
void DlcCatalog::merge()
{
    m_entries.clear();
    m_entries.reserve(m_server.size());

    for (const DlcEntry &server : m_server) {
        DlcEntry entry = server;

        const auto local = std::find_if(m_local.begin(), m_local.end(),
                                        [&](const DlcEntry &l) { return l.title == server.title; });
        if (local == m_local.end()) {
            entry.local_version = "-";
            entry.status = DlcStatus::New;
        } else {
            // The local csv reuses the version column, so what it holds is
            // the version that was installed.
            entry.local_version = local->server_version;
            entry.status = (entry.server_version == entry.local_version) ? DlcStatus::Installed
                                                                        : DlcStatus::Update;
        }
        m_entries.push_back(std::move(entry));
    }

    // Installed packs the server no longer lists. The Delphi merge walked the
    // server list only, so these became invisible and could not be removed from
    // the UI even though the code already had a "Removed" status for them.
    for (const DlcEntry &local : m_local) {
        const bool on_server = std::any_of(m_server.begin(), m_server.end(),
                                           [&](const DlcEntry &s) { return s.title == local.title; });
        if (on_server) continue;

        DlcEntry entry = local;
        entry.local_version = local.server_version;  // the local csv's version column
        entry.server_version = "-";
        entry.status = DlcStatus::Removed;
        m_entries.push_back(std::move(entry));
    }
}

int DlcCatalog::new_count() const
{
    return static_cast<int>(std::count_if(m_entries.begin(), m_entries.end(),
                                          [](const DlcEntry &e) {
                                              return e.status == DlcStatus::New;
                                          }));
}

int DlcCatalog::update_count() const
{
    return static_cast<int>(std::count_if(m_entries.begin(), m_entries.end(),
                                          [](const DlcEntry &e) {
                                              return e.status == DlcStatus::Update;
                                          }));
}

bool DlcCatalog::write_local_catalog() const
{
    ensure_directory(type_dir());

    std::ofstream out(local_catalog_path(), std::ios::binary);
    if (!out) return false;

    // Byte-identical header to what the server publishes, so a local
    // catalogue and a server one are interchangeable and the Delphi build can
    // still read what we write.
    out << "Name,Author,Version,Desc\r\n";
    for (const DlcEntry &entry : m_local) {
        auto quote = [](const std::string &field) {
            if (field.find_first_of(",\"") == std::string::npos) return field;
            std::string escaped = "\"";
            for (char c : field) {
                if (c == '"') escaped += '"';
                escaped += c;
            }
            return escaped + "\"";
        };
        out << quote(entry.title) << ',' << quote(entry.author) << ','
            << quote(entry.server_version) << ',' << quote(entry.description) << "\r\n";
    }
    return out.good();
}

bool DlcCatalog::mark_installed(const std::string &title)
{
    const auto server = std::find_if(m_server.begin(), m_server.end(),
                                     [&](const DlcEntry &e) { return e.title == title; });
    if (server == m_server.end()) return false;

    const auto local = std::find_if(m_local.begin(), m_local.end(),
                                    [&](const DlcEntry &e) { return e.title == title; });
    if (local == m_local.end())
        m_local.push_back(*server);
    else
        *local = *server;

    const bool ok = write_local_catalog();
    merge();
    return ok;
}

bool DlcCatalog::remove(const std::string &title)
{
    const auto entry = std::find_if(m_entries.begin(), m_entries.end(),
                                    [&](const DlcEntry &e) { return e.title == title; });
    if (entry == m_entries.end()) return false;

    remove_directory(install_dir(*entry));
    remove_file(package_path(*entry, false));
    remove_file(package_path(*entry, true));

    m_local.erase(std::remove_if(m_local.begin(), m_local.end(),
                                 [&](const DlcEntry &e) { return e.title == title; }),
                  m_local.end());

    const bool ok = write_local_catalog();
    merge();
    return ok;
}

// --- Selection -------------------------------------------------------------

const char *DlcCatalog::default_selection() const
{
    return info_for(m_type).default_selection;
}

std::string DlcCatalog::load_selection() const
{
    IniFile ini;
    ini.load(path_join(path_join(m_base_dir, "ini"), "gameLauncher.ini"));
    return ini.read_string(kDlcSection, dlc_type_id(m_type), default_selection());
}

void DlcCatalog::save_selection(const std::string &title) const
{
    const std::string path = path_join(path_join(m_base_dir, "ini"), "gameLauncher.ini");
    ensure_directory(parent_path(path));

    IniFile ini;
    ini.load(path);
    ini.write_string(kDlcSection, dlc_type_id(m_type), title);
    ini.save(path);
}

bool DlcCatalog::load_enabled() const
{
    const TypeInfo &info = info_for(m_type);
    if (!info.has_enabled_flag) return true;

    IniFile ini;
    ini.load(path_join(path_join(m_base_dir, "ini"), "gameLauncher.ini"));
    return ini.read_bool(kDlcSection, std::string(info.id) + "_Enabled", info.default_enabled);
}

void DlcCatalog::save_enabled(bool enabled) const
{
    const TypeInfo &info = info_for(m_type);
    if (!info.has_enabled_flag) return;

    const std::string path = path_join(path_join(m_base_dir, "ini"), "gameLauncher.ini");
    ensure_directory(parent_path(path));

    IniFile ini;
    ini.load(path);
    // The Delphi build stored these as the strings "true"/"false", not 0/1.
    ini.write_string(kDlcSection, std::string(info.id) + "_Enabled", enabled ? "true" : "false");
    ini.save(path);
}

} // namespace swos
