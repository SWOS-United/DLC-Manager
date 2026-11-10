/* fio.c -- see fio.h. */
#include "fio.h"

void fio_join(char *out, ULONG size, const char *dir, const char *name)
{
    ULONG n;
    if (!size) return;
    out[0] = 0;
    if (dir && *dir) {
        n = 0;
        while (dir[n] && n + 1 < size) {
            out[n] = dir[n];
            n++;
        }
        out[n] = 0;
        if (n && out[n - 1] != '/' && out[n - 1] != ':' && n + 1 < size) {
            out[n++] = '/';
            out[n] = 0;
        }
    } else {
        n = 0;
    }
    while (*name && n + 1 < size) out[n++] = *name++;
    out[n] = 0;
}

char *fio_slurp(const char *path, ULONG max, ULONG *len)
{
    FioFile f = fio_open_read(path);
    LONG size, got;
    char *buf;

    if (len) *len = 0;
    if (!f) return NULL;
    size = fio_size(f);
    if (size < 0 || (ULONG)size > max) {
        fio_close(f);
        return NULL;
    }
    buf = mem_alloc((ULONG)size + 1);
    if (!buf) {
        fio_close(f);
        return NULL;
    }
    got = size ? fio_read(f, buf, size) : 0;
    fio_close(f);
    if (got != size) {
        mem_free(buf);
        return NULL;
    }
    buf[size] = 0;
    if (len) *len = (ULONG)size;
    return buf;
}

struct TreeCtx {
    const char *dir;
    BOOL ok;
};

static void delete_entry(const char *name, BOOL is_dir, void *vctx)
{
    struct TreeCtx *ctx = vctx;
    char path[256];
    fio_join(path, sizeof(path), ctx->dir, name);
    if (is_dir ? !fio_delete_tree(path) : !fio_delete(path)) ctx->ok = FALSE;
}

BOOL fio_delete_tree(const char *path)
{
    struct TreeCtx ctx;
    if (!fio_exists(path)) return TRUE;
    if (!fio_is_dir(path)) return fio_delete(path);
    ctx.dir = path;
    ctx.ok = TRUE;
    /* Deleting while an ExNext walk is on the same drawer upsets it, so the
     * entries are collected first (fio_list_dir does), then deleted. */
    if (!fio_list_dir(path, delete_entry, &ctx)) return FALSE;
    return fio_delete(path) && ctx.ok;
}

#ifdef HOST_TEST
/* ---- PC ---------------------------------------------------------------- */
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

FioFile fio_open_read(const char *path) { return fopen(path, "rb"); }
FioFile fio_open_write(const char *path) { return fopen(path, "wb"); }
void fio_close(FioFile f) { if (f) fclose(f); }

LONG fio_read(FioFile f, void *buf, LONG len)
{
    size_t got = fread(buf, 1, (size_t)len, f);
    return ferror((FILE *)f) ? -1 : (LONG)got;
}

BOOL fio_write(FioFile f, const void *buf, LONG len)
{
    return fwrite(buf, 1, (size_t)len, f) == (size_t)len;
}

BOOL fio_seek(FioFile f, LONG pos) { return fseek(f, pos, SEEK_SET) == 0; }

LONG fio_size(FioFile f)
{
    long here = ftell(f), end;
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    end = ftell(f);
    fseek(f, here, SEEK_SET);
    return (LONG)end;
}

BOOL fio_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

BOOL fio_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

BOOL fio_mkdir(const char *path) { return mkdir(path, 0777) == 0 || fio_is_dir(path); }
BOOL fio_delete(const char *path) { return remove(path) == 0; }
BOOL fio_rename(const char *from, const char *to) { return rename(from, to) == 0; }

BOOL fio_list_dir(const char *path, FioDirFn fn, void *ctx)
{
    char names[64][64];
    BOOL dirs[64];
    int n = 0, i;
    struct dirent *e;
    DIR *d = opendir(path);
    if (!d) return FALSE;
    while ((e = readdir(d)) && n < 64) {
        char full[512];
        if (e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2])))
            continue;
        snprintf(names[n], sizeof(names[n]), "%s", e->d_name);
        fio_join(full, sizeof(full), path, e->d_name);
        dirs[n++] = fio_is_dir(full);
    }
    closedir(d);
    for (i = 0; i < n; i++) fn(names[i], dirs[i], ctx);
    return TRUE;
}

#else
/* ---- Amiga ------------------------------------------------------------- */
#include <dos/dos.h>
#include <proto/dos.h>

FioFile fio_open_read(const char *path) { return (FioFile)Open((CONST_STRPTR)path, MODE_OLDFILE); }
FioFile fio_open_write(const char *path) { return (FioFile)Open((CONST_STRPTR)path, MODE_NEWFILE); }
void fio_close(FioFile f) { if (f) Close((BPTR)f); }
LONG fio_read(FioFile f, void *buf, LONG len) { return Read((BPTR)f, buf, len); }
BOOL fio_write(FioFile f, const void *buf, LONG len) { return Write((BPTR)f, (APTR)buf, len) == len; }
BOOL fio_seek(FioFile f, LONG pos) { return Seek((BPTR)f, pos, OFFSET_BEGINNING) != -1; }

LONG fio_size(FioFile f)
{
    /* Seek returns the old position: to the end, then back. Works on 2.04,
     * where ExamineFH would need 2.04's bugs worked around. */
    LONG here = Seek((BPTR)f, 0, OFFSET_END), end;
    if (here == -1) return -1;
    end = Seek((BPTR)f, here, OFFSET_BEGINNING);
    return end;
}

static BOOL examine(const char *path, struct FileInfoBlock *fib)
{
    BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    BOOL ok;
    if (!lock) return FALSE;
    ok = Examine(lock, fib) != 0;
    UnLock(lock);
    return ok;
}

BOOL fio_exists(const char *path)
{
    BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (!lock) return FALSE;
    UnLock(lock);
    return TRUE;
}

BOOL fio_is_dir(const char *path)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BOOL dir = FALSE;
    if (!fib) return FALSE;
    if (examine(path, fib)) dir = fib->fib_DirEntryType > 0;
    FreeDosObject(DOS_FIB, fib);
    return dir;
}

BOOL fio_mkdir(const char *path)
{
    BPTR lock = CreateDir((CONST_STRPTR)path);
    if (lock) {
        UnLock(lock);
        return TRUE;
    }
    return fio_is_dir(path);
}

BOOL fio_delete(const char *path) { return DeleteFile((CONST_STRPTR)path) != 0; }

BOOL fio_rename(const char *from, const char *to)
{
    return Rename((CONST_STRPTR)from, (CONST_STRPTR)to) != 0;
}

/* Names are collected first, so a callback may delete what it is given.
 * A drawer holds a mod's own few files; 64 is plenty, and more just means
 * another call sees the rest. */
#define LIST_MAX 64

BOOL fio_list_dir(const char *path, FioDirFn fn, void *ctx)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    char (*names)[108];
    BOOL *dirs;
    LONG n = 0, i;

    lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (!lock) return FALSE;
    fib = AllocDosObject(DOS_FIB, NULL);
    names = mem_alloc(LIST_MAX * sizeof(*names));
    dirs = mem_alloc(LIST_MAX * sizeof(*dirs));
    if (!fib || !names || !dirs || !Examine(lock, fib)) {
        if (fib) FreeDosObject(DOS_FIB, fib);
        if (names) mem_free(names);
        if (dirs) mem_free(dirs);
        UnLock(lock);
        return FALSE;
    }
    while (n < LIST_MAX && ExNext(lock, fib)) {
        str_copy(names[n], (const char *)fib->fib_FileName, sizeof(names[n]));
        dirs[n++] = fib->fib_DirEntryType > 0;
    }
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    for (i = 0; i < n; i++) fn(names[i], dirs[i], ctx);
    mem_free(names);
    mem_free(dirs);
    return TRUE;
}
#endif
