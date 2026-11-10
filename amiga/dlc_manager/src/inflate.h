/* inflate.h -- a streaming DEFLATE decoder (RFC 1951) for a zip's entries.
 *
 * Memory is fixed and small for an Amiga with 1 MB: the 32 KB window the
 * format needs, a 2 KB input buffer and the code tables. Input comes from a
 * callback, output goes to one each time the window fills (and at the end),
 * so an entry of any size streams from file to file.
 */
#ifndef SWOSDLC_INFLATE_H
#define SWOSDLC_INFLATE_H

#include "port.h"

/* Fills buf with up to len bytes; returns how many (0 at the end, -1 on error). */
typedef LONG (*InflateIn)(void *ctx, UBYTE *buf, LONG len);
/* Takes len bytes of output; FALSE stops the decoder (a write failed, or cancel). */
typedef BOOL (*InflateOut)(void *ctx, const UBYTE *buf, LONG len);

enum InflateResult {
    INFLATE_OK = 0,
    INFLATE_BAD_DATA,      /* not valid DEFLATE */
    INFLATE_TRUNCATED,     /* the input ended early */
    INFLATE_READ_ERROR,
    INFLATE_STOPPED,       /* the output callback said stop */
    INFLATE_NO_MEMORY
};

/* Decodes one DEFLATE stream. */
int inflate_stream(InflateIn in, InflateOut out, void *ctx);

#endif
