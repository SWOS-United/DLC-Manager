/* json.h -- a pull reader for the little JSON the manager reads: the Amiga
 * mod index and each installed mod's mod.json.
 *
 * No tree is built: the caller walks objects and arrays and copies out the
 * values it wants, skipping the rest. Strings are copied into the caller's
 * buffer, cut to fit; \u escapes become Latin-1 (the Amiga's character set)
 * or '?' above it. The first error sticks, and every call after it fails.
 */
#ifndef SWOSDLC_JSON_H
#define SWOSDLC_JSON_H

#include "port.h"

#define JSON_MAX_DEPTH 32

struct Json {
    const char *p, *end;
    BOOL error;
    WORD depth;                        /* open objects and arrays */
    UBYTE first[JSON_MAX_DEPTH];       /* no element read yet at that depth */
};

void json_init(struct Json *j, const char *text, ULONG len);

/* The next character that is not white space, or 0 at the end. */
char json_peek(struct Json *j);

BOOL json_object_begin(struct Json *j);           /* '{' */
/* The next key of the current object, and its ':'. FALSE at '}' (consumed)
 * or on an error. */
BOOL json_next_key(struct Json *j, char *key, ULONG size);

BOOL json_array_begin(struct Json *j);            /* '[' */
/* TRUE when another element follows; FALSE at ']' (consumed) or an error. */
BOOL json_array_next(struct Json *j);

BOOL json_string(struct Json *j, char *out, ULONG size);
/* A number's integer part (what "size" and "version" need). */
BOOL json_integer(struct Json *j, LONG *out);
BOOL json_bool(struct Json *j, BOOL *out);
BOOL json_skip(struct Json *j);                   /* any value */

#endif
