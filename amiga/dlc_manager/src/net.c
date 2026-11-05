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

#define MAX_RESPONSE (128UL * 1024UL)  /* the biggest catalogue today is ~8 KB */
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

BOOL net_http_get(const char *host, const char *path, char **body, ULONG *length,
                  char *err, ULONG err_size)
{
    struct hostent *he;
    struct sockaddr_in addr;
    LONG sock, tv[2];
    char request[384];
    char *buf = NULL;
    ULONG used = 0;
    BOOL ok = FALSE;

    *body = NULL;
    *length = 0;
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
    addr.sin_port = 80;  /* network order is big-endian: no swap on 68k */
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
               "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: SWOS-DLC-Amiga/0.1\r\n"
               "Connection: close\r\n\r\n",
               (ULONG)path, (ULONG)host);
    if (!send_all(sock, request, strlen(request))) {
        str_format(err, err_size, "send failed (%ld)", Errno());
        goto done;
    }

    buf = AllocVec(MAX_RESPONSE + 1, MEMF_ANY);
    if (!buf) {
        str_copy(err, "out of memory", err_size);
        goto done;
    }
    for (;;) {
        LONG got;
        if (used >= MAX_RESPONSE) {
            str_copy(err, "response too large", err_size);
            goto done;
        }
        got = recv(sock, buf + used, MAX_RESPONSE - used, 0);
        if (got < 0) {
            str_format(err, err_size, "receive failed (%ld)", Errno());
            goto done;
        }
        if (got == 0) break;
        used += got;
    }
    buf[used] = 0;

    /* "HTTP/1.x 200 ..." then headers, a blank line, the body. */
    {
        char *p = buf, *status, *start = NULL;
        LONG code = 0;

        if (!str_starts(buf, "HTTP/")) {
            str_copy(err, "not an HTTP response", err_size);
            goto done;
        }
        while (*p && *p != ' ') p++;
        status = p;
        while (*status == ' ') status++;
        while (*status >= '0' && *status <= '9') code = code * 10 + (*status++ - '0');
        if (code != 200) {
            str_format(err, err_size, "server answered HTTP %ld", code);
            goto done;
        }
        for (p = buf; *p; p++) {
            if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
                start = p + 4;
                break;
            }
            if (p[0] == '\n' && p[1] == '\n') {
                start = p + 2;
                break;
            }
        }
        if (!start) {
            str_copy(err, "malformed response", err_size);
            goto done;
        }
        used -= (ULONG)(start - buf);
        memmove(buf, start, used + 1);  /* body and its NUL */
    }

    *body = buf;
    *length = used;
    buf = NULL;
    ok = TRUE;

done:
    if (buf) FreeVec(buf);
    CloseSocket(sock);
    return ok;
}
