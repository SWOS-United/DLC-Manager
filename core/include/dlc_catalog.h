// dlc_catalog.h -- the DLC model, ported from TDLCInfoManager in dlc_info_u.pas.
//
// Shared deliberately: the DLC manager browses and installs packs, and the
// launcher reads the same selections back to apply them before starting the
// game (runSWOS2020 drives gPitchInfoMan, gCupInfoMan, gTeamDBInfoMan and the
// rest). Both also read and write the selections from the same
// ini/gameLauncher.ini [DLC] section.
//
// Server layout, unchanged from the Delphi build:
//
//   http://www.sensiblesoccer.de/swos2020/dlc/<type>/<type>-info.csv
//   http://www.sensiblesoccer.de/swos2020/dlc/<type>/<name>.7z
//   http://www.sensiblesoccer.de/swos2020/dlc/<type>/<name>_preview.7z
//
// Local layout:
//
//   <base>/temp/<type>-info.csv          the catalogue just fetched
//   <base>/dlc/<type>/<type>-info.csv    what is installed (seeded from
//                                        <type>-info_default.csv on first run)
//   <base>/dlc/<type>/<name>.7z          the downloaded pack
//   <base>/dlc/<type>/<name>/            its extracted contents
//
// `<name>` is the title with '/' replaced by '_', because titles like
// "English - Original 96/97 (Jonathan Pearce)" are also path components.

#pragma once

#include <string>
#include <vector>

namespace swos {

// The 17 DLC types the Delphi manager handled, in the order its tabs used.
// The string is the wire and directory name and must not be renamed.
enum class DlcType {
    TeamDb,
    Pitch,
    Cup,
    Commentary,
    Fx,
    BackgroundImage,
    TitleMusic,
    MenuMusic,
    SpriteBall,
    SpriteGoalkeeper,
    SpriteGoalNet,
    SpriteManager,
    SpriteReferee,
    SpriteTeam1,
    SpriteTeam2,
    SpriteTeam3,
    SpriteFlag,
};

// Wire/directory name, e.g. "pitch", "sprite_ball". Note BackgroundImage is
// "bgi2", not "bgi" -- the Delphi code moved to a second-generation layout and
// kept the old name in use elsewhere.
const char *dlc_type_id(DlcType type);
// Human label for the UI, e.g. "Title Music".
const char *dlc_type_label(DlcType type);
// Every type, in tab order.
const std::vector<DlcType> &all_dlc_types();
bool is_sprite_type(DlcType type);

// What kind of preview a type offers, which decides the right-hand pane.
enum class PreviewKind {
    None,
    Image,   // pitch, sprites, background images, cup artwork
    Audio,   // commentary, sound effects, title/menu music
};
PreviewKind dlc_preview_kind(DlcType type);

// How an entry in the server catalogue compares with what is installed.
enum class DlcStatus {
    New,        // on the server, not installed
    Installed,  // installed, same version as the server
    Update,     // installed, server has a newer version
    Removed,    // installed but no longer on the server
};
const char *dlc_status_label(DlcStatus status);

struct DlcEntry {
    std::string title;            // as published, may contain '/'
    std::string author;
    std::string server_version;
    std::string local_version;    // empty when not installed
    std::string description;
    DlcStatus status = DlcStatus::New;

    // Title with '/' replaced by '_': the on-disk and in-URL form.
    std::string folder_name() const;
    bool installed() const { return status != DlcStatus::New; }
};

// One type's catalogue: the server list merged with what is on disk.
class DlcCatalog {
public:
    DlcCatalog() = default;
    DlcCatalog(DlcType type, std::string base_dir);

    DlcType type() const { return m_type; }
    const std::string &base_dir() const { return m_base_dir; }

    // --- Catalogue -------------------------------------------------------
    // Parses a <type>-info.csv body. The first line is a header and lines
    // starting with '#' are comments, as in loadEx().
    static std::vector<DlcEntry> parse_csv(const std::string &csv);

    // Reads <base>/temp/<type>-info.csv (server) and
    // <base>/dlc/<type>/<type>-info.csv (local), then merges them into
    // entries() with a status per row. Returns false if neither exists.
    bool load();

    // True when `body` actually looks like one of these catalogues. The DLC
    // server answers a missing file with a 301 to its homepage, which then
    // returns 200 and a quarter-megabyte of HTML -- so "the download
    // succeeded" is not enough to conclude the bytes are a catalogue. Without
    // this check that page parses into thousands of nonsense entries.
    static bool looks_like_catalog(const std::string &body);

    // Stores a freshly downloaded catalogue as the server list and re-merges.
    // Returns false (changing nothing) if `csv` fails looks_like_catalog.
    bool set_server_csv(const std::string &csv);

    const std::vector<DlcEntry> &entries() const { return m_entries; }
    int new_count() const;
    int update_count() const;

    // --- Paths and URLs --------------------------------------------------
    std::string catalog_url() const;
    std::string package_url(const DlcEntry &entry, bool preview = false) const;
    std::string package_path(const DlcEntry &entry, bool preview = false) const;
    std::string install_dir(const DlcEntry &entry) const;
    std::string local_catalog_path() const;
    std::string server_catalog_path() const;
    std::string type_dir() const;

    // True when the pack's extracted directory is present on disk.
    bool is_extracted(const DlcEntry &entry) const;

    // Deletes the extracted folder and both .7z files, then rewrites the local
    // catalogue without the entry -- the Delphi Remove button.
    bool remove(const std::string &title);

    // Records a pack as installed at its server version and rewrites the local
    // catalogue. Call after a successful extract.
    bool mark_installed(const std::string &title);

    // --- Selection (ini/gameLauncher.ini [DLC]) --------------------------
    // Reads/writes "[DLC] <type>" plus "<type>_Enabled" for the types that
    // carry it (cup, teamdb, sprite_*).
    std::string load_selection() const;
    void save_selection(const std::string &title) const;
    bool load_enabled() const;
    void save_enabled(bool enabled) const;
    // Per-type default selection, matching the defaults LoadCurrentFromIni used.
    const char *default_selection() const;

private:
    void merge();
    bool write_local_catalog() const;

    DlcType m_type = DlcType::Pitch;
    std::string m_base_dir;
    std::vector<DlcEntry> m_server;
    std::vector<DlcEntry> m_local;
    std::vector<DlcEntry> m_entries;
};

// Index into the flag strip (assets/icons/flags.png) for a pack, or -1 when the
// type shows no flag.
//
// A direct port of the ImageIndex chains in lvwCommentaryInfoCustomDrawItem and
// lvwCupInfoCustomDrawItem: the original picked a flag by searching the visible
// title for a substring, and the indices are positions in the same strip that
// scripts/extract_flags.py pulls out of the Delphi form. Only Commentary and Cup
// had flags -- those are the two lists that set SmallImages = imgFlags.
//
// Order matters and is preserved: "Portugues Brasileiro" has to be tested before
// "Portuguese", and the first of two "(Sweden" arms is the one that wins.
int dlc_flag_index(DlcType type, const std::string &title);

// Geometry of assets/icons/flags.png, so the UI can clip a cell out of it.
inline constexpr int kFlagCell = 16;
inline constexpr int kFlagColumns = 4;

// Base URL of the DLC server, without a trailing slash.
const char *dlc_server_root();

// Title -> on-disk/in-URL name and back, ported from TUtil.replaceSlashToUnderbar
// and replaceUnderbarToSlash.
std::string title_to_folder(const std::string &title);
std::string folder_to_title(const std::string &folder);

} // namespace swos
