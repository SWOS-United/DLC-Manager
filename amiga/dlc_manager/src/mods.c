/* mods.c -- see mods.h. */
#include "mods.h"
#include "fio.h"
#include "json.h"
#include "unzip.h"

#define MODS_FILE_MAX 4096    /* data/MODS: 48 ids of up to 33 bytes, and room */

static void copy(char *dst, const char *src, ULONG size)
{
    if (!size) return;
    while (--size && *src) *dst++ = *src++;
    *dst = 0;
}

static BOOL eq(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static BOOL same(const char *a, const char *b)
{
    /* AmigaDOS names ignore case, so ids do too. */
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return FALSE;
        if (!x) return TRUE;
    }
}

static void path2(char *out, ULONG size, const char *root, const char *a, const char *b)
{
    char tmp[256];
    fio_join(tmp, sizeof(tmp), root, a);
    if (b) fio_join(out, size, tmp, b);
    else copy(out, tmp, size);
}

static void set_err(char *err, ULONG size, const char *a, const char *b)
{
    copy(err, a, size);
    if (b) {
        ULONG n = 0;
        while (n < size && err[n]) n++;
        if (n < size) copy(err + n, b, size - n);
    }
}

BOOL mods_read_info(const char *root, const char *slug, struct ModInfo *out)
{
    char path[256], dir[160], key[24];
    ULONG len;
    char *text;
    struct Json j;

    memset(out, 0, sizeof(*out));
    path2(dir, sizeof(dir), root, "mods", slug);
    fio_join(path, sizeof(path), dir, "mod.json");
    text = fio_slurp(path, 64UL * 1024UL, &len);
    if (!text) return FALSE;
    copy(out->slug, slug, sizeof(out->slug));
    json_init(&j, text, len);
    if (json_object_begin(&j)) {
        while (json_next_key(&j, key, sizeof(key))) {
            char *dst = NULL;
            ULONG size = 0;
            if (eq(key, "uuid")) dst = out->uuid, size = sizeof(out->uuid);
            else if (eq(key, "version")) dst = out->version, size = sizeof(out->version);
            else if (eq(key, "slot")) dst = out->slot, size = sizeof(out->slot);
            else if (eq(key, "category")) dst = out->category, size = sizeof(out->category);
            else if (eq(key, "changes")) dst = out->changes, size = sizeof(out->changes);
            else if (eq(key, "title")) dst = out->title, size = sizeof(out->title);
            else if (eq(key, "author")) dst = out->author, size = sizeof(out->author);
            else if (eq(key, "name") && !out->title[0]) dst = out->title, size = sizeof(out->title);
            if (dst && json_peek(&j) == '"') json_string(&j, dst, size);
            else json_skip(&j);
        }
    }
    mem_free(text);
    if (!out->title[0]) copy(out->title, slug, sizeof(out->title));
    return !j.error && out->uuid[0];
}

struct ScanCtx {
    const char *root;
    struct ModInfo *out;
    LONG max, count;
};

static void scan_entry(const char *name, BOOL is_dir, void *vctx)
{
    struct ScanCtx *ctx = vctx;
    if (!is_dir || name[0] == '.' || ctx->count >= ctx->max) return;
    if (mods_read_info(ctx->root, name, &ctx->out[ctx->count])) ctx->count++;
}

LONG mods_scan(const char *root, struct ModInfo *out, LONG max)
{
    struct ScanCtx ctx;
    char dir[160];
    ctx.root = root;
    ctx.out = out;
    ctx.max = max;
    ctx.count = 0;
    fio_join(dir, sizeof(dir), root, "mods");
    fio_list_dir(dir, scan_entry, &ctx);
    return ctx.count;
}

/* data/MODS as lines, each id with its '*' if it had one. */
static LONG read_list(const char *root, char lines[][MOD_SLUG_LEN + 2], LONG max)
{
    char path[200], *text, *p;
    ULONG len;
    LONG n = 0;

    path2(path, sizeof(path), root, "data", "MODS");
    text = fio_slurp(path, MODS_FILE_MAX, &len);
    if (!text) return 0;
    for (p = text; *p && n < max;) {
        char *start;
        ULONG k = 0;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
        start = p;
        while (*p && *p != '\n' && *p != '\r' && *p != ',' && *p != ' ' && *p != '\t') p++;
        if (p == start) continue;
        while (start < p && k + 1 < MOD_SLUG_LEN + 2) lines[n][k++] = *start++;
        lines[n][k] = 0;
        n++;
    }
    mem_free(text);
    return n;
}

static BOOL write_list(const char *root, char lines[][MOD_SLUG_LEN + 2], LONG n, char *err,
                       ULONG err_size)
{
    char path[200], dir[160];
    FioFile f;
    LONG i;

    path2(dir, sizeof(dir), root, "data", NULL);
    fio_join(path, sizeof(path), dir, "MODS");
    if (n == 0) {
        /* No list is what the setup leaves when no mod is on. */
        if (fio_exists(path) && !fio_delete(path)) {
            set_err(err, err_size, "cannot delete data/MODS", NULL);
            return FALSE;
        }
        return TRUE;
    }
    f = fio_open_write(path);
    if (!f) {
        set_err(err, err_size, "cannot write data/MODS", NULL);
        return FALSE;
    }
    for (i = 0; i < n; i++) {
        if (!fio_write(f, lines[i], (LONG)strlen(lines[i])) || !fio_write(f, "\n", 1)) {
            fio_close(f);
            set_err(err, err_size, "cannot write data/MODS", NULL);
            return FALSE;
        }
    }
    fio_close(f);
    return TRUE;
}

static void bare_id(char *out, const char *line)
{
    copy(out, line, MOD_SLUG_LEN);
    {
        ULONG n = strlen(out);
        if (n && out[n - 1] == '*') out[n - 1] = 0;
    }
}

LONG mods_enabled(const char *root, char ids[][MOD_SLUG_LEN], LONG max)
{
    char (*lines)[MOD_SLUG_LEN + 2] = mem_alloc(MODS_LIST_MAX * (MOD_SLUG_LEN + 2));
    LONG n, i;
    if (!lines) return 0;
    n = read_list(root, lines, MODS_LIST_MAX);
    if (n > max) n = max;
    for (i = 0; i < n; i++) bare_id(ids[i], lines[i]);
    mem_free(lines);
    return n;
}

BOOL mods_is_enabled(const char *root, const char *slug)
{
    char ids[MODS_LIST_MAX][MOD_SLUG_LEN];
    LONG n = mods_enabled(root, ids, MODS_LIST_MAX), i;
    for (i = 0; i < n; i++)
        if (same(ids[i], slug)) return TRUE;
    return FALSE;
}

static BOOL change_list(const char *root, const struct ModInfo *on, const char *off,
                        const struct ModInfo *installed, LONG count, char *err, ULONG err_size)
{
    char (*lines)[MOD_SLUG_LEN + 2] = mem_alloc((MODS_LIST_MAX + 1) * (MOD_SLUG_LEN + 2));
    char id[MOD_SLUG_LEN];
    LONG n, i, k, out = 0;
    BOOL ok;

    if (!lines) {
        set_err(err, err_size, "out of memory", NULL);
        return FALSE;
    }
    n = read_list(root, lines, MODS_LIST_MAX);
    for (i = 0; i < n; i++) {
        BOOL drop = FALSE;
        bare_id(id, lines[i]);
        if (off && same(id, off)) drop = TRUE;
        if (on) {
            if (same(id, on->slug)) drop = TRUE;                 /* added again at the end */
            else if (on->slot[0])
                for (k = 0; k < count; k++)
                    if (same(installed[k].slug, id) && same(installed[k].slot, on->slot))
                        drop = TRUE;
        }
        if (!drop) copy(lines[out++], lines[i], MOD_SLUG_LEN + 2);
    }
    if (on) {
        if (out >= MODS_LIST_MAX) {
            mem_free(lines);
            set_err(err, err_size, "data/MODS is full (48 mods)", NULL);
            return FALSE;
        }
        copy(lines[out], on->slug, MOD_SLUG_LEN);
        /* The setup's mark for a mod that is not cosmetic (nothing reads it yet). */
        if (on->changes[0] && !eq(on->changes, "cosmetic")) {
            ULONG len = strlen(lines[out]);
            lines[out][len] = '*';
            lines[out][len + 1] = 0;
        }
        out++;
    }
    ok = write_list(root, lines, out, err, err_size);
    mem_free(lines);
    return ok;
}

BOOL mods_turn_on(const char *root, const struct ModInfo *mod, const struct ModInfo *installed,
                  LONG count, char *err, ULONG err_size)
{
    return change_list(root, mod, NULL, installed, count, err, err_size);
}

BOOL mods_turn_off(const char *root, const char *slug, char *err, ULONG err_size)
{
    return change_list(root, NULL, slug, NULL, 0, err, err_size);
}

BOOL mods_make_root(const char *root, char *err, ULONG err_size)
{
    char dir[160];
    fio_join(dir, sizeof(dir), root, "mods");
    if (!fio_mkdir(dir)) {
        set_err(err, err_size, "cannot make the mods drawer", NULL);
        return FALSE;
    }
    return TRUE;
}

BOOL mods_install_zip(const char *root, const char *zip, const char *slug, const char *uuid,
                      ModsProgress progress, void *ctx, char *err, ULONG err_size)
{
    char part_name[MOD_SLUG_LEN + 2], part[200], final[200];
    struct ModInfo info;

    if (!mods_make_root(root, err, err_size)) return FALSE;
    part_name[0] = '.';
    copy(part_name + 1, slug, sizeof(part_name) - 1);
    path2(part, sizeof(part), root, "mods", part_name);
    path2(final, sizeof(final), root, "mods", slug);

    fio_delete_tree(part);
    if (!fio_mkdir(part)) {
        set_err(err, err_size, "cannot make ", part);
        return FALSE;
    }
    if (!unzip_to(zip, part, progress, ctx, err, err_size)) {
        fio_delete_tree(part);
        return FALSE;
    }
    mods_read_info(root, part_name, &info);
    if (!info.uuid[0] || !eq(info.uuid, uuid)) {
        fio_delete_tree(part);
        set_err(err, err_size, info.uuid[0] ? "the download is a different mod"
                                            : "the download has no mod.json", NULL);
        return FALSE;
    }
    if (!fio_delete_tree(final)) {
        fio_delete_tree(part);
        set_err(err, err_size, "cannot replace the old copy (in use?)", NULL);
        return FALSE;
    }
    if (!fio_rename(part, final)) {
        fio_delete_tree(part);
        set_err(err, err_size, "cannot rename the new copy into place", NULL);
        return FALSE;
    }
    return TRUE;
}

BOOL mods_remove(const char *root, const char *slug, char *err, ULONG err_size)
{
    char dir[200];
    if (!mods_turn_off(root, slug, err, err_size)) return FALSE;
    path2(dir, sizeof(dir), root, "mods", slug);
    if (!fio_delete_tree(dir)) {
        set_err(err, err_size, "cannot delete all of mods/", slug);
        return FALSE;
    }
    return TRUE;
}
