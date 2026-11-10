/* net.c -- see net.h. */
#include "net.h"
#include "util.h"

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <libraries/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

struct Library *SocketBase;

#define MAX_RESPONSE (256UL * 1024UL)  /* the Amiga index is ~70 KB */
#define RECV_TIMEOUT_SECS 15

static char s_stack_name[64];

BOOL net_open(void)
{
    STRPTR release = NULL;

    if (SocketBase) return TRUE;
    SocketBase = OpenLibrary((CONST_STRPTR) "bsdsocket.library", 4);
    if (!SocketBase) return FALSE;

    /* "Roadshow 4.x (date)". Not every stack answers this tag. */
    if (SocketBaseTags(SBTM_GETREF(SBTC_RELEASESTRPTR), (ULONG)&release, TAG_END) == 0 &&
        release) {
        str_copy(s_stack_name, (const char *)release, sizeof(s_stack_name));
    } else if (SocketBase->lib_IdString) {
        str_copy(s_stack_name, (const char *)SocketBase->lib_IdString, sizeof(s_stack_name));
    } else {
        str_copy(s_stack_name, "bsdsocket.library", sizeof(s_stack_name));
    }
    {
        /* Trim the trailing date and newline the release strings carry. */
        char *p = s_stack_name;
        while (*p && *p != '(' && *p != '\r' && *p != '\n') p++;
        while (p > s_stack_name && p[-1] == ' ') p--;
        *p = 0;
    }
    return TRUE;
}

void net_close(void)
{
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

BOOL net_available(void) { return SocketBase != NULL; }
const char *net_stack_name(void) { return s_stack_name; }

BOOL net_ready(char *why, ULONG why_size)
{
    ULONG status = 0;

    if (!SocketBase) {
        str_copy(why, "No TCP/IP stack - placeholder data", why_size);
        return FALSE;
    }
    /* Roadshow answers this; AmiTCP, Miami and UAE's emulated library may
     * not, and then the only way to find out is to try. */
    if (SocketBaseTags(SBTM_GETREF(SBTC_SYSTEM_STATUS), (ULONG)&status, TAG_END) != 0)
        return TRUE;

    if (!(status & SBSYSSTAT_Interfaces)) {
        str_copy(why, "Offline: no network interface is up", why_size);
        return FALSE;
    }
    if (!(status & SBSYSSTAT_Resolver)) {
        str_copy(why, "Offline: no DNS server configured", why_size);
        return FALSE;
    }
    if (!(status & SBSYSSTAT_DefaultRoute)) {
        str_copy(why, "Offline: no default route", why_size);
        return FALSE;
    }
    return TRUE;
}

static BOOL send_all(LONG sock, const char *data, LONG len)
{
    while (len > 0) {
        LONG sent = send(sock, (APTR)data, len, 0);
        if (sent <= 0) return FALSE;
        data += sent;
        len -= sent;
    }
    return TRUE;
}

BOOL net_parse_url(const char *url, char *host, ULONG host_size, UWORD *port, char *path,
                   ULONG path_size)
{
    const char *p = url, *h;
    ULONG n = 0;
    if (!str_starts(p, "http://")) return FALSE;
    p += 7;
    h = p;
    while (*p && *p != '/' && *p != ':') p++;
    if (p == h || (ULONG)(p - h) >= host_size) return FALSE;
    while (h < p) host[n++] = *h++;
    host[n] = 0;
    *port = 80;
    if (*p == ':') {
        ULONG v = 0;
        p++;
        while (*p >= '0' && *p <= '9') v = v * 10 + (ULONG)(*p++ - '0');
        if (!v || v > 65535) return FALSE;
        *port = (UWORD)v;
    }
    str_copy(path, *p ? p : "/", path_size);
    return TRUE;
}

#define HEAD_MAX 2048
#define RECV_CHUNK 4096

/* The status line's code and Content-Length, from the header block. */
static LONG parse_head(const char *head, ULONG *length)
{
    const char *p = head;
    LONG code = 0;
    *length = 0;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') code = code * 10 + (*p++ - '0');
    for (; *p; p++) {
        if (*p != '\n') continue;
        {
            const char *k = p + 1, *want = "content-length:";
            while (*want && *k && ((*k | 0x20) == *want || *k == *want)) k++, want++;
            if (!*want) {
                ULONG v = 0;
                while (*k == ' ') k++;
                while (*k >= '0' && *k <= '9') v = v * 10 + (ULONG)(*k++ - '0');
                *length = v;
            }
        }
    }
    return code;
}

BOOL net_http_stream(const char *host, UWORD port, const char *path, NetSink sink, void *ctx,
                     char *err, ULONG err_size)
{
    struct hostent *he;
    struct sockaddr_in addr;
    LONG sock, tv[2];
    char request[512];
    UBYTE *buf = NULL;
    char *head = NULL;
    ULONG head_len = 0, total = 0, got_body = 0;
    BOOL in_body = FALSE, ok = FALSE;

    if (!SocketBase) {
        str_copy(err, "no TCP/IP stack", err_size);
        return FALSE;
    }
    he = gethostbyname((STRPTR)host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        str_format(err, err_size, "cannot resolve %s", (ULONG)host);
        return FALSE;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = port;  /* network order is big-endian: no swap on 68k */
    memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        str_format(err, err_size, "socket() failed (%ld)", Errno());
        return FALSE;
    }
    tv[0] = RECV_TIMEOUT_SECS;
    tv[1] = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, tv, sizeof(tv));

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        str_format(err, err_size, "cannot connect to %s (%ld)", (ULONG)host, Errno());
        goto done;
    }
    /* HTTP/1.0: no chunked encoding, and the server closes when it is done. */
    str_format(request, sizeof(request),
               "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: SWOS-DLC-Amiga/0.2\r\n"
               "Connection: close\r\n\r\n",
               (ULONG)path, (ULONG)host);
    if (!send_all(sock, request, strlen(request))) {
        str_format(err, err_size, "send failed (%ld)", Errno());
        goto done;
    }

    buf = AllocVec(RECV_CHUNK, MEMF_ANY);
    head = AllocVec(HEAD_MAX + 1, MEMF_ANY);
    if (!buf || !head) {
        str_copy(err, "out of memory", err_size);
        goto done;
    }
    for (;;) {
        LONG got = recv(sock, buf, RECV_CHUNK, 0), used = 0;
        if (got < 0) {
            str_format(err, err_size, "receive failed (%ld)", Errno());
            goto done;
        }
        if (got == 0) break;
        if (!in_body) {
            /* Gather the header block; whatever follows it is body. */
            LONG i;
            for (i = 0; i < got && !in_body; i++) {
                if (head_len >= HEAD_MAX) {
                    str_copy(err, "malformed response", err_size);
                    goto done;
                }
                head[head_len++] = (char)buf[i];
                if (head_len >= 4 && head[head_len - 1] == '\n' &&
                    ((head[head_len - 2] == '\r' && head[head_len - 3] == '\n') ||
                     head[head_len - 2] == '\n')) {
                    LONG code;
                    head[head_len] = 0;
                    if (!str_starts(head, "HTTP/")) {
                        str_copy(err, "not an HTTP response", err_size);
                        goto done;
                    }
                    code = parse_head(head, &total);
                    if (code != 200) {
                        /* The server answers a missing file with a redirect. */
                        str_format(err, err_size, "server answered HTTP %ld", code);
                        goto done;
                    }
                    in_body = TRUE;
                }
            }
            used = i;
        }
        if (in_body && got > used) {
            if (!sink(ctx, buf + used, (ULONG)(got - used), total)) {
                str_copy(err, "stopped", err_size);
                goto done;
            }
            got_body += (ULONG)(got - used);
        }
    }
    if (!in_body) {
        str_copy(err, "malformed response", err_size);
        goto done;
    }
    if (total && got_body != total) {
        str_format(err, err_size, "connection closed early (%lu of %lu bytes)", got_body, total);
        goto done;
    }
    ok = TRUE;

done:
    if (buf) FreeVec(buf);
    if (head) FreeVec(head);
    CloseSocket(sock);
    return ok;
}

/* net_http_get's sink: one buffer, grown as the body arrives, up to MAX_RESPONSE. */
struct MemSink {
    char *buf;
    ULONG used, size;
    BOOL too_big, no_memory;
};

static BOOL mem_sink(void *vctx, const UBYTE *data, ULONG len, ULONG total)
{
    struct MemSink *m = vctx;
    if (m->used + len + 1 > m->size) {
        ULONG want = m->size ? m->size : 16384;
        char *bigger;
        if (total + 1 > want) want = total + 1;
        while (want < m->used + len + 1) want <<= 1;
        if (want > MAX_RESPONSE + 1) {
            if (m->used + len + 1 > MAX_RESPONSE + 1) {
                m->too_big = TRUE;
                return FALSE;
            }
            want = MAX_RESPONSE + 1;
        }
        bigger = AllocVec(want, MEMF_ANY);
        if (!bigger) {
            m->no_memory = TRUE;
            return FALSE;
        }
        if (m->buf) {
            memcpy(bigger, m->buf, m->used);
            FreeVec(m->buf);
        }
        m->buf = bigger;
        m->size = want;
    }
    memcpy(m->buf + m->used, data, len);
    m->used += len;
    return TRUE;
}

BOOL net_http_get(const char *host, UWORD port, const char *path, char **body, ULONG *length,
                  char *err, ULONG err_size)
{
    struct MemSink m;
    *body = NULL;
    *length = 0;
    memset(&m, 0, sizeof(m));
    if (!net_http_stream(host, port, path, mem_sink, &m, err, err_size)) {
        if (m.too_big) str_copy(err, "response too large", err_size);
        else if (m.no_memory) str_copy(err, "out of memory", err_size);
        if (m.buf) FreeVec(m.buf);
        return FALSE;
    }
    if (!m.buf) {
        str_copy(err, "empty response", err_size);
        return FALSE;
    }
    m.buf[m.used] = 0;
    *body = m.buf;
    *length = m.used;
    return TRUE;
}
