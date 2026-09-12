// ini_file.cpp -- see ini_file.h.

#include "ini_file.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace swos {

namespace {

std::string trim(const std::string &s)
{
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool iequal(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
        return std::tolower(x) == std::tolower(y);
    });
}

} // namespace

IniFile::Section *IniFile::find_section(const std::string &name)
{
    for (auto &s : m_sections)
        if (iequal(s.name, name)) return &s;
    return nullptr;
}

const IniFile::Section *IniFile::find_section(const std::string &name) const
{
    return const_cast<IniFile *>(this)->find_section(name);
}

IniFile::Section &IniFile::ensure_section(const std::string &name)
{
    if (Section *s = find_section(name)) return *s;
    m_sections.push_back(Section{name, {}});
    return m_sections.back();
}

bool IniFile::load(const std::string &path)
{
    m_sections.clear();

    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    // Everything before the first [section] header lives in an unnamed section,
    // matching TIniFile's treatment of a headerless file.
    m_sections.push_back(Section{"", {}});
    Section *current = &m_sections.back();

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);

        if (t.empty() || t[0] == ';' || t[0] == '#') {
            current->entries.push_back(Entry{"", "", line});
            continue;
        }
        if (t.front() == '[' && t.back() == ']') {
            m_sections.push_back(Section{trim(t.substr(1, t.size() - 2)), {}});
            current = &m_sections.back();
            continue;
        }
        const auto eq = t.find('=');
        if (eq == std::string::npos) {
            current->entries.push_back(Entry{"", "", line});  // keep malformed lines verbatim
            continue;
        }
        current->entries.push_back(Entry{trim(t.substr(0, eq)), trim(t.substr(eq + 1)), {}});
    }
    return true;
}

bool IniFile::save(const std::string &path) const
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    bool first = true;
    for (const auto &section : m_sections) {
        // Skip the synthetic leading section unless it actually holds something.
        if (section.name.empty() && section.entries.empty()) continue;

        if (!section.name.empty()) {
            if (!first) out << "\r\n";
            out << '[' << section.name << "]\r\n";
        }
        first = false;

        // Drop blank lines at the end of a section: we emit our own separator
        // before the next header, and preserving them too makes the file grow
        // by one blank line on every save.
        size_t count = section.entries.size();
        while (count > 0) {
            const Entry &last = section.entries[count - 1];
            if (!last.key.empty() || !trim(last.raw).empty()) break;
            --count;
        }

        for (size_t i = 0; i < count; ++i) {
            const Entry &e = section.entries[i];
            if (e.key.empty())
                out << e.raw << "\r\n";
            else
                out << e.key << '=' << e.value << "\r\n";
        }
    }
    return out.good();
}

bool IniFile::has(const std::string &section, const std::string &key) const
{
    const Section *s = find_section(section);
    if (!s) return false;
    for (const auto &e : s->entries)
        if (!e.key.empty() && iequal(e.key, key)) return true;
    return false;
}

std::string IniFile::read_string(const std::string &section, const std::string &key,
                                 const std::string &fallback) const
{
    const Section *s = find_section(section);
    if (!s) return fallback;
    for (const auto &e : s->entries)
        if (!e.key.empty() && iequal(e.key, key)) return e.value;
    return fallback;
}

int IniFile::read_int(const std::string &section, const std::string &key, int fallback) const
{
    const std::string v = read_string(section, key);
    if (v.empty()) return fallback;

    // TIniFile.ReadInteger falls back on anything that is not a clean integer.
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(v, &consumed);
        if (consumed != v.size()) return fallback;
        return parsed;
    } catch (...) {
        return fallback;
    }
}

bool IniFile::read_bool(const std::string &section, const std::string &key, bool fallback) const
{
    const std::string v = read_string(section, key);
    if (v.empty()) return fallback;
    if (iequal(v, "true") || v == "1") return true;
    if (iequal(v, "false") || v == "0") return false;
    return fallback;
}

void IniFile::write_string(const std::string &section, const std::string &key,
                           const std::string &value)
{
    Section &s = ensure_section(section);
    for (auto &e : s.entries) {
        if (!e.key.empty() && iequal(e.key, key)) {
            e.value = value;
            return;
        }
    }
    s.entries.push_back(Entry{key, value, {}});
}

void IniFile::write_int(const std::string &section, const std::string &key, int value)
{
    write_string(section, key, std::to_string(value));
}

} // namespace swos
