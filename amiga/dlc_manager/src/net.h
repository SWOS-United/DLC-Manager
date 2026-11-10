/* net.h -- plain HTTP/1.0 GET over bsdsocket.library.
 *
 * Built against the Roadshow SDK headers, but bsdsocket.library is a shared
 * API: this runs on Roadshow, AmiTCP or Miami alike. The DLC server is plain
 * http:// (sensiblesoccer.de/swos2020), so no TLS stack is needed.
 *
 * Calls block. Sockets get a receive timeout so a dead server cannot hang the
 * program forever, but the window does not repaint while a request runs.
 */
#ifndef SWOSDLC_NET_H
#define SWOSDLC_NET_H

#include <exec/types.h>

#define DLC_HOST     "www.sensiblesoccer.de"
#define DLC_BASEPATH "/swos2020/dlc"

/* Opens bsdsocket.library. FALSE when no TCP/IP stack is running. */
BOOL net_open(void);
void net_close(void);
BOOL net_available(void);
/* e.g. "Roadshow 4.x" -- whatever the stack reports, for the status line. */
const char *net_stack_name(void);

/* Whether the stack is actually online: bsdsocket.library opens from LIBS:
 * whether or not an interface was ever brought up, so "the library opened"
 * says little. Asks Roadshow for its system status (interfaces, DNS, default
 * route). FALSE with the reason in `why` when it is not usable; TRUE when it
 * is, or when the stack cannot say (then the request itself is the test).
 * `why` is short enough for the status line (about 50 characters). */
BOOL net_ready(char *why, ULONG why_size);

/* Fetches http://host/path. On success *body is a NUL-terminated buffer the
 * caller frees with FreeVec, and the result is TRUE. A non-200 answer is a
 * failure: the DLC server redirects missing files to an HTML homepage.
 * On failure `err` says why. */
BOOL net_http_get(const char *host, UWORD port, const char *path, char **body, ULONG *length,
                  char *err, ULONG err_size);

/* Splits "http://host[:port]/path". FALSE for anything else (https too: the
 * Amiga has no TLS here). */
BOOL net_parse_url(const char *url, char *host, ULONG host_size, UWORD *port, char *path,
                   ULONG path_size);

/* Streams http://host:port/path to `sink` as it arrives, a piece at a time.
 * `total` is the Content-Length, or 0 when the server sends none. The sink
 * returns FALSE to stop (a write failed, or the user cancelled); then the
 * result is FALSE and `err` says "stopped". */
typedef BOOL (*NetSink)(void *ctx, const UBYTE *data, ULONG len, ULONG total);
BOOL net_http_stream(const char *host, UWORD port, const char *path, NetSink sink, void *ctx,
                     char *err, ULONG err_size);

#endif
