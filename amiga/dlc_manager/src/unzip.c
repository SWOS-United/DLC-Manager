/* unzip.c -- see unzip.h. */
#include "unzip.h"
#include "fio.h"
#include "inflate.h"

#define EOCD_SIG 0x06054b50UL
#define CEN_SIG  0x02014b50UL
#define LOC_SIG  0x04034b50UL
#define MAX_CD   (256UL * 1024UL)   /* a mod's zip lists a few hundred files */
#define COPY_BUF 2048

ULONG unzip_name_max = 30;

static ULONG s_crc_table[256];
static BOOL s_crc_ready;

ULONG unzip_crc32(ULONG crc, const UBYTE *data, ULONG len)
{
    if (!s_crc_ready) {
        ULONG n;
        for (n = 0; n < 256; n++) {
            ULONG c = n;
            int k;
            for (k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1;
            s_crc_table[n] = c;
        }
        s_crc_ready = TRUE;
    }
    crc = ~crc;
    while (len--) crc = s_crc_table[(crc ^ *data++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

static UWORD le16(const UBYTE *p) { return (UWORD)(p[0] | (p[1] << 8)); }
static ULONG le32(const UBYTE *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) | ((ULONG)p[3] << 24);
}

static void set_err(char *err, ULONG size, const char *a, const char *b)
{
    err[0] = 0;
    if (!size) return;
    {
        ULONG n = 0;
        while (*a && n + 1 < size) err[n++] = *a++;
        while (b && *b && n + 1 < size) err[n++] = *b++;
        err[n] = 0;
    }
}

/* A name safe to write under the destination drawer. */
static BOOL safe_name(const char *name)
{
    const char *p = name, *seg = name;
    if (!*name || *name == '/') return FALSE;
    for (;; p++) {
        if (*p == ':' || *p == '\\') return FALSE;
        if (*p == '/' || !*p) {
            /* AmigaDOS keeps 30 characters of a name (OFS and FFS alike). */
            if (p - seg > (LONG)unzip_name_max) return FALSE;
            if (p - seg == 2 && seg[0] == '.' && seg[1] == '.') return FALSE;
            if (p - seg == 1 && seg[0] == '.') return FALSE;
            if (!*p) break;
            seg = p + 1;
        }
    }
    return TRUE;
}

/* Creates each drawer on the way to `path` (a file's path, or a drawer's
 * with a trailing '/'). */
static BOOL make_parents(char *path)
{
    char *p;
    for (p = path; *p; p++) {
        if (*p == '/' && p != path && p[-1] != ':') {
            BOOL ok;
            *p = 0;
            ok = fio_mkdir(path);
            *p = '/';
            if (!ok) return FALSE;
        }
    }
    return TRUE;
}

struct Entry {
    FioFile zip;
    FioFile out;
    ULONG left;          /* compressed bytes still to read */
    ULONG crc;
    ULONG written;
    ULONG base_done, total;
    UnzipProgress progress;
    void *ctx;
    BOOL cancelled;
};

static LONG entry_in(void *vctx, UBYTE *buf, LONG len)
{
    struct Entry *e = vctx;
    LONG got;
    if (!e->left) return 0;
    if ((ULONG)len > e->left) len = (LONG)e->left;
    got = fio_read(e->zip, buf, len);
    if (got <= 0) return -1;
    e->left -= (ULONG)got;
    return got;
}

static BOOL entry_out(void *vctx, const UBYTE *buf, LONG len)
{
    struct Entry *e = vctx;
    if (!fio_write(e->out, buf, len)) return FALSE;
    e->crc = unzip_crc32(e->crc, buf, (ULONG)len);
    e->written += (ULONG)len;
    if (e->progress && !e->progress(e->ctx, e->base_done + e->written, e->total)) {
        e->cancelled = TRUE;
        return FALSE;
    }
    return TRUE;
}

BOOL unzip_to(const char *zip_path, const char *dest, UnzipProgress progress, void *ctx,
              char *err, ULONG err_size)
{
    FioFile zip;
    UBYTE tail[22 + 256], *cd = NULL, *p;
    LONG size, tail_len, i;
    ULONG cd_size, cd_off, entries, n, total = 0, done = 0;
    BOOL ok = FALSE;
    char name[256], path[384];

    zip = fio_open_read(zip_path);
    if (!zip) {
        set_err(err, err_size, "cannot open the download", NULL);
        return FALSE;
    }
    size = fio_size(zip);
    if (size < 22) {
        set_err(err, err_size, "the download is not a zip", NULL);
        goto done;
    }

    /* The end record is the last 22 bytes when there is no comment; a short
     * comment is looked past too. */
    tail_len = size < (LONG)sizeof(tail) ? size : (LONG)sizeof(tail);
    if (!fio_seek(zip, size - tail_len) || fio_read(zip, tail, tail_len) != tail_len) {
        set_err(err, err_size, "cannot read the download", NULL);
        goto done;
    }
    for (i = tail_len - 22; i >= 0; i--)
        if (le32(tail + i) == EOCD_SIG) break;
    if (i < 0) {
        set_err(err, err_size, "the download is not a zip", NULL);
        goto done;
    }
    entries = le16(tail + i + 10);
    cd_size = le32(tail + i + 12);
    cd_off = le32(tail + i + 16);
    if (le16(tail + i + 4) || le16(tail + i + 6) || cd_off == 0xffffffffUL) {
        set_err(err, err_size, "a split or zip64 archive is not supported", NULL);
        goto done;
    }
    if (cd_size > MAX_CD || cd_off + cd_size > (ULONG)size) {
        set_err(err, err_size, "the zip's directory is damaged", NULL);
        goto done;
    }
    cd = mem_alloc(cd_size + 1);
    if (!cd) {
        set_err(err, err_size, "out of memory", NULL);
        goto done;
    }
    if (!fio_seek(zip, (LONG)cd_off) || fio_read(zip, cd, (LONG)cd_size) != (LONG)cd_size) {
        set_err(err, err_size, "cannot read the zip's directory", NULL);
        goto done;
    }

    /* First pass: check every entry before writing anything. */
    for (n = 0, p = cd; n < entries; n++) {
        UWORD nlen, xlen, clen, method, flags;
        if (p + 46 > cd + cd_size || le32(p) != CEN_SIG) {
            set_err(err, err_size, "the zip's directory is damaged", NULL);
            goto done;
        }
        flags = le16(p + 8);
        method = le16(p + 10);
        nlen = le16(p + 28);
        xlen = le16(p + 30);
        clen = le16(p + 32);
        if (p + 46 + nlen > cd + cd_size || nlen >= sizeof(name)) {
            set_err(err, err_size, "the zip's directory is damaged", NULL);
            goto done;
        }
        memcpy(name, p + 46, nlen);
        name[nlen] = 0;
        if (flags & 1) {
            set_err(err, err_size, "encrypted entry: ", name);
            goto done;
        }
        if (method != 0 && method != 8) {
            set_err(err, err_size, "unsupported compression: ", name);
            goto done;
        }
        if (!safe_name(name)) {
            set_err(err, err_size, "unsafe name in the zip: ", name);
            goto done;
        }
        total += le32(p + 24);
        p += 46 + nlen + xlen + clen;
    }

    /* Second pass: unpack. */
    for (n = 0, p = cd; n < entries; n++) {
        UWORD nlen = le16(p + 28), xlen = le16(p + 30), clen = le16(p + 32);
        UWORD method = le16(p + 10);
        ULONG crc = le32(p + 16), csize = le32(p + 20), usize = le32(p + 24);
        ULONG loc = le32(p + 42);
        UBYTE lh[30];
        struct Entry e;

        memcpy(name, p + 46, nlen);
        name[nlen] = 0;
        p += 46 + nlen + xlen + clen;

        fio_join(path, sizeof(path), dest, name);
        if (!make_parents(path)) {
            set_err(err, err_size, "cannot make a drawer for ", name);
            goto done;
        }
        if (name[nlen - 1] == '/') continue;   /* a drawer: made above */

        if (!fio_seek(zip, (LONG)loc) || fio_read(zip, lh, 30) != 30 || le32(lh) != LOC_SIG ||
            !fio_seek(zip, (LONG)(loc + 30 + le16(lh + 26) + le16(lh + 28)))) {
            set_err(err, err_size, "damaged entry: ", name);
            goto done;
        }

        memset(&e, 0, sizeof(e));
        e.zip = zip;
        e.left = csize;
        e.base_done = done;
        e.total = total;
        e.progress = progress;
        e.ctx = ctx;
        e.out = fio_open_write(path);
        if (!e.out) {
            set_err(err, err_size, "cannot write ", name);
            goto done;
        }
        if (method == 8) {
            int r = inflate_stream(entry_in, entry_out, &e);
            if (r != INFLATE_OK) {
                fio_close(e.out);
                if (e.cancelled) set_err(err, err_size, "cancelled", NULL);
                else if (r == INFLATE_STOPPED) set_err(err, err_size, "cannot write ", name);
                else if (r == INFLATE_NO_MEMORY) set_err(err, err_size, "out of memory", NULL);
                else set_err(err, err_size, "damaged data in ", name);
                goto done;
            }
        } else {
            UBYTE buf[COPY_BUF];
            while (e.left) {
                LONG got = entry_in(&e, buf, COPY_BUF);
                if (got <= 0 || !entry_out(&e, buf, got)) {
                    fio_close(e.out);
                    set_err(err, err_size, e.cancelled ? "cancelled" : "cannot copy ",
                            e.cancelled ? NULL : name);
                    goto done;
                }
            }
        }
        fio_close(e.out);
        if (e.written != usize || e.crc != crc) {
            set_err(err, err_size, "checksum mismatch in ", name);
            goto done;
        }
        done += usize;
    }
    ok = TRUE;

done:
    if (cd) mem_free(cd);
    fio_close(zip);
    return ok;
}
