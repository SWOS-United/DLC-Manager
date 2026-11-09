// json.cpp -- see json.h.

#include "json.h"

#include <cstdlib>

namespace swos {

namespace {
const JsonValue &null_value()
{
    static const JsonValue v;
    return v;
}

void append_utf8(std::string &out, unsigned cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}
} // namespace

class JsonParser {
public:
    explicit JsonParser(const std::string &text) : m_text(text) {}

    bool run(JsonValue &out, std::string &error)
    {
        skip_bom();
        if (!value(out, 0) || (skip_ws(), m_pos != m_text.size())) {
            if (m_error.empty()) m_error = "unexpected text";
            error = m_error + " at offset " + std::to_string(m_pos);
            return false;
        }
        return true;
    }

private:
    static constexpr int kMaxDepth = 64;

    void skip_bom()
    {
        if (m_text.compare(0, 3, "\xEF\xBB\xBF") == 0) m_pos = 3;
    }
    void skip_ws()
    {
        while (m_pos < m_text.size() &&
               (m_text[m_pos] == ' ' || m_text[m_pos] == '\t' || m_text[m_pos] == '\n' ||
                m_text[m_pos] == '\r'))
            ++m_pos;
    }
    bool fail(const char *what)
    {
        if (m_error.empty()) m_error = what;
        return false;
    }
    bool literal(const char *word)
    {
        const std::string w(word);
        if (m_text.compare(m_pos, w.size(), w) != 0) return fail("bad literal");
        m_pos += w.size();
        return true;
    }

    bool value(JsonValue &v, int depth)
    {
        if (depth > kMaxDepth) return fail("nested too deeply");
        skip_ws();
        if (m_pos >= m_text.size()) return fail("unexpected end");
        const char c = m_text[m_pos];
        switch (c) {
        case '{': return object(v, depth);
        case '[': return array(v, depth);
        case '"': v.m_kind = JsonValue::Kind::String; return string(v.m_string);
        case 't': v.m_kind = JsonValue::Kind::Bool; v.m_bool = true; return literal("true");
        case 'f': v.m_kind = JsonValue::Kind::Bool; v.m_bool = false; return literal("false");
        case 'n': v.m_kind = JsonValue::Kind::Null; return literal("null");
        default: return number(v);
        }
    }

    bool number(JsonValue &v)
    {
        const char *start = m_text.c_str() + m_pos;
        char *end = nullptr;
        v.m_number = std::strtod(start, &end);
        if (end == start) return fail("expected a value");
        m_pos += static_cast<size_t>(end - start);
        v.m_kind = JsonValue::Kind::Number;
        return true;
    }

    bool hex4(unsigned &out)
    {
        if (m_pos + 4 > m_text.size()) return fail("bad \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = m_text[m_pos++];
            out <<= 4;
            if (h >= '0' && h <= '9') out |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') out |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') out |= static_cast<unsigned>(h - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }

    bool string(std::string &out)
    {
        ++m_pos;  // opening quote
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_pos >= m_text.size()) break;
            const char e = m_text[m_pos++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && m_text.compare(m_pos, 2, "\\u") == 0) {
                    m_pos += 2;
                    unsigned low = 0;
                    if (!hex4(low)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                append_utf8(out, cp);
                break;
            }
            default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    bool array(JsonValue &v, int depth)
    {
        v.m_kind = JsonValue::Kind::Array;
        ++m_pos;
        skip_ws();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
            return true;
        }
        for (;;) {
            JsonValue item;
            if (!value(item, depth + 1)) return false;
            v.m_items.push_back(std::move(item));
            skip_ws();
            if (m_pos >= m_text.size()) return fail("unterminated array");
            if (m_text[m_pos] == ',') { ++m_pos; continue; }
            if (m_text[m_pos] == ']') { ++m_pos; return true; }
            return fail("expected , or ]");
        }
    }

    bool object(JsonValue &v, int depth)
    {
        v.m_kind = JsonValue::Kind::Object;
        ++m_pos;
        skip_ws();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            ++m_pos;
            return true;
        }
        for (;;) {
            skip_ws();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') return fail("expected a key");
            std::string key;
            if (!string(key)) return false;
            skip_ws();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') return fail("expected :");
            ++m_pos;
            JsonValue member;
            if (!value(member, depth + 1)) return false;
            v.m_members[key] = std::move(member);
            skip_ws();
            if (m_pos >= m_text.size()) return fail("unterminated object");
            if (m_text[m_pos] == ',') { ++m_pos; continue; }
            if (m_text[m_pos] == '}') { ++m_pos; return true; }
            return fail("expected , or }");
        }
    }

    const std::string &m_text;
    size_t m_pos = 0;
    std::string m_error;
};

std::string JsonValue::str(const std::string &fallback) const
{
    return m_kind == Kind::String ? m_string : fallback;
}

double JsonValue::num(double fallback) const
{
    return m_kind == Kind::Number ? m_number : fallback;
}

bool JsonValue::boolean(bool fallback) const
{
    return m_kind == Kind::Bool ? m_bool : fallback;
}

const JsonValue &JsonValue::operator[](const std::string &key) const
{
    if (m_kind != Kind::Object) return null_value();
    const auto it = m_members.find(key);
    return it == m_members.end() ? null_value() : it->second;
}

bool JsonValue::has(const std::string &key) const
{
    return m_kind == Kind::Object && m_members.count(key) != 0;
}

bool JsonValue::parse(const std::string &text, JsonValue &out, std::string &error)
{
    out = JsonValue();
    return JsonParser(text).run(out, error);
}

} // namespace swos
