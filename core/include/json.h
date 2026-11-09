// json.h -- a small JSON reader, enough for mod.json and the mod index.
//
// Only reading is needed: the DLC manager reads the server's index.json and each
// installed mod's mod.json, and writes neither. A malformed file is an error the
// caller reports, never an exception.

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace swos {

class JsonValue {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind() const { return m_kind; }
    bool is_object() const { return m_kind == Kind::Object; }
    bool is_array() const { return m_kind == Kind::Array; }
    bool is_string() const { return m_kind == Kind::String; }

    // Typed reads with a fallback for a missing or differently typed value.
    std::string str(const std::string &fallback = {}) const;
    double num(double fallback = 0.0) const;
    bool boolean(bool fallback = false) const;

    // Object member, or a shared Null when absent (so lookups chain safely).
    const JsonValue &operator[](const std::string &key) const;
    bool has(const std::string &key) const;
    const std::vector<JsonValue> &items() const { return m_items; }

    // Parses `text`. On failure returns false and says where in `error`.
    static bool parse(const std::string &text, JsonValue &out, std::string &error);

private:
    friend class JsonParser;
    Kind m_kind = Kind::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::vector<JsonValue> m_items;
    std::map<std::string, JsonValue> m_members;
};

} // namespace swos
