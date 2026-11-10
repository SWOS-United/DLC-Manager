/* json.c -- see json.h. */
#include "json.h"

void json_init(struct Json *j, const char *text, ULONG len)
{
    j->p = text;
    j->end = text + len;
    j->error = FALSE;
    j->depth = 0;
    /* A UTF-8 byte order mark, as some editors write one. */
    if (len >= 3 && (UBYTE)text[0] == 0xef && (UBYTE)text[1] == 0xbb && (UBYTE)text[2] == 0xbf)
        j->p += 3;
}

static BOOL fail(struct Json *j)
{
    j->error = TRUE;
    return FALSE;
}

char json_peek(struct Json *j)
{
    if (j->error) return 0;
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n'))
        j->p++;
    return j->p < j->end ? *j->p : 0;
}

static BOOL expect(struct Json *j, char c)
{
    if (json_peek(j) != c) return fail(j);
    j->p++;
    return TRUE;
}

static BOOL open_nest(struct Json *j, char c)
{
    if (!expect(j, c)) return FALSE;
    if (j->depth >= JSON_MAX_DEPTH) return fail(j);
    j->first[j->depth++] = 1;
    return TRUE;
}

BOOL json_object_begin(struct Json *j) { return open_nest(j, '{'); }
BOOL json_array_begin(struct Json *j) { return open_nest(j, '['); }

/* Before an element: a comma unless it is the first. FALSE, consuming it, at
 * the closing character. */
static BOOL element(struct Json *j, char close)
{
    char c = json_peek(j);
    if (j->error || !j->depth) return fail(j);
    if (c == close) {
        j->p++;
        j->depth--;
        return FALSE;
    }
    if (j->first[j->depth - 1]) {
        j->first[j->depth - 1] = 0;
        return TRUE;
    }
    return expect(j, ',');
}

BOOL json_next_key(struct Json *j, char *key, ULONG size)
{
    if (!element(j, '}')) return FALSE;
    if (!json_string(j, key, size)) return FALSE;
    return expect(j, ':');
}

BOOL json_array_next(struct Json *j)
{
    if (!element(j, ']')) return FALSE;
    return json_peek(j) != 0;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

BOOL json_string(struct Json *j, char *out, ULONG size)
{
    ULONG n = 0;
    if (!expect(j, '"')) return FALSE;
    while (j->p < j->end && *j->p != '"') {
        UBYTE c = (UBYTE)*j->p++;
        ULONG cp = c;
        if (c == '\\') {
            if (j->p >= j->end) return fail(j);
            c = (UBYTE)*j->p++;
            switch (c) {
            case 'n': cp = '\n'; break;
            case 't': cp = '\t'; break;
            case 'r': cp = '\r'; break;
            case 'b': cp = 8; break;
            case 'f': cp = 12; break;
            case 'u': {
                int i;
                cp = 0;
                for (i = 0; i < 4; i++) {
                    int d = j->p < j->end ? hex_digit(*j->p++) : -1;
                    if (d < 0) return fail(j);
                    cp = (cp << 4) | (ULONG)d;
                }
                break;
            }
            default: cp = c; break;   /* \" \\ \/ */
            }
        } else if (c >= 0xc0) {
            /* UTF-8: Latin-1 when it fits, else '?'. */
            int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
            cp = c & (0x3f >> more);
            while (more-- && j->p < j->end && ((UBYTE)*j->p & 0xc0) == 0x80)
                cp = (cp << 6) | ((UBYTE)*j->p++ & 0x3f);
        }
        if (cp > 0xff) cp = '?';
        if (out && n + 1 < size) out[n++] = (char)cp;
    }
    if (j->p >= j->end) return fail(j);
    j->p++;   /* the closing quote */
    if (out && size) out[n] = 0;
    return TRUE;
}

BOOL json_integer(struct Json *j, LONG *out)
{
    LONG v = 0;
    BOOL neg = FALSE, any = FALSE;
    json_peek(j);
    if (j->p < j->end && *j->p == '-') {
        neg = TRUE;
        j->p++;
    }
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9') {
        v = v * 10 + (*j->p++ - '0');
        any = TRUE;
    }
    if (!any) return fail(j);
    /* A fraction or exponent is read past; the integer part is the value. */
    while (j->p < j->end && (*j->p == '.' || *j->p == 'e' || *j->p == 'E' || *j->p == '+' ||
                             *j->p == '-' || (*j->p >= '0' && *j->p <= '9')))
        j->p++;
    if (out) *out = neg ? -v : v;
    return TRUE;
}

static BOOL word(struct Json *j, const char *w)
{
    while (*w) {
        if (j->p >= j->end || *j->p != *w) return fail(j);
        j->p++, w++;
    }
    return TRUE;
}

BOOL json_bool(struct Json *j, BOOL *out)
{
    char c = json_peek(j);
    if (c == 't') {
        if (out) *out = TRUE;
        return word(j, "true");
    }
    if (c == 'f') {
        if (out) *out = FALSE;
        return word(j, "false");
    }
    return fail(j);
}

BOOL json_skip(struct Json *j)
{
    char c = json_peek(j);
    switch (c) {
    case '"': return json_string(j, NULL, 0);
    case '{': {
        char key[2];
        if (!json_object_begin(j)) return FALSE;
        while (json_next_key(j, key, sizeof(key)))
            if (!json_skip(j)) return FALSE;
        return !j->error;
    }
    case '[':
        if (!json_array_begin(j)) return FALSE;
        while (json_array_next(j))
            if (!json_skip(j)) return FALSE;
        return !j->error;
    case 't':
    case 'f': return json_bool(j, NULL);
    case 'n': return word(j, "null");
    default: return json_integer(j, NULL);
    }
}
