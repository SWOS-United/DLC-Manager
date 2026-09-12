// ini_file.h -- a minimal INI reader/writer matching Delphi's TIniFile.
//
// The launcher must keep reading and writing the exact files the Delphi build
// produced (ini/gameLauncher.ini and the game's swos.ini), so the semantics
// here follow TIniFile rather than any particular INI dialect:
//   * section and key lookup are case-insensitive
//   * ReadInteger returns the supplied default when the key is missing OR the
//     value does not parse as an integer
//   * writing preserves existing sections and only replaces the named key
//
// Comments (';' or '#') and blank lines are preserved on rewrite so a
// hand-annotated ini survives a round-trip.

#pragma once

#include <map>
#include <string>
#include <vector>

namespace swos {

class IniFile {
public:
    // Missing files are not an error: every getter then returns its default,
    // which is how the Delphi launcher behaved on a first run.
    bool load(const std::string &path);
    bool save(const std::string &path) const;

    bool has(const std::string &section, const std::string &key) const;

    std::string read_string(const std::string &section, const std::string &key,
                            const std::string &fallback = {}) const;
    int read_int(const std::string &section, const std::string &key, int fallback = 0) const;
    bool read_bool(const std::string &section, const std::string &key, bool fallback = false) const;

    void write_string(const std::string &section, const std::string &key,
                      const std::string &value);
    void write_int(const std::string &section, const std::string &key, int value);

private:
    struct Entry {
        std::string key;       // empty for a comment or blank line
        std::string value;
        std::string raw;       // verbatim text for comments / blank lines
    };
    struct Section {
        std::string name;
        std::vector<Entry> entries;
    };

    Section *find_section(const std::string &name);
    const Section *find_section(const std::string &name) const;
    Section &ensure_section(const std::string &name);

    std::vector<Section> m_sections;
};

} // namespace swos
