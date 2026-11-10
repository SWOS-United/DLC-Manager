/* catalog.c -- see catalog.h. */
#include "catalog.h"
#include "json.h"
#include "util.h"

#include <exec/memory.h>
#include <proto/exec.h>

#define MAX_INSTALLED 64

/* What the index offers, kept between merges. */
struct Offered {
    char uuid[MOD_UUID_LEN];
    char slug[MOD_SLUG_LEN];
    char title[80];
    char author[64];
    char version[MOD_SHORT_LEN];
    char slot[MOD_SHORT_LEN];
    char category[MOD_SHORT_LEN];
    char changes[MOD_SHORT_LEN];
    char about[ABOUT_LEN];
    char url[URL_LEN];
    char sha256[72];
    ULONG size;
};

static struct Offered *s_offered;
static LONG s_offered_count;
static struct ModInfo *s_installed;
static LONG s_installed_count;

BOOL g_index_loaded;

struct Category g_categories[NUM_CATEGORIES] = {
    {{0}, "pitch", "Pitch", {0}, 0, {0}},
    {{0}, "teamdb", "Team Database", {0}, 0, {0}},
    {{0}, "cup", "Team Pack", {0}, 0, {0}},
    {{0}, "bgi2", "Background", {0}, 0, {0}},
    {{0}, "commentary", "Commentary", {0}, 0, {0}},
    {{0}, "fx", "Sound Effects", {0}, 0, {0}},
    {{0}, "tmusic", "Title Music", {0}, 0, {0}},
    {{0}, "", "Other", {0}, 0, {0}},   /* anything else: last */
};
struct List g_category_list;

static void new_list(struct List *l)
{
    l->lh_Head = (struct Node *)&l->lh_Tail;
    l->lh_Tail = NULL;
    l->lh_TailPred = (struct Node *)&l->lh_Head;
}

static void free_packs(struct Category *cat)
{
    struct Node *n;
    while ((n = RemHead(&cat->packs))) FreeVec(n);
    cat->count = 0;
}

static struct Category *category_for(const char *id)
{
    LONG i;
    for (i = 0; i < NUM_CATEGORIES - 1; i++)
        if (str_equal(g_categories[i].id, id)) return &g_categories[i];
    return &g_categories[NUM_CATEGORIES - 1];
}

static void relabel_categories(void)
{
    LONG i;
    struct Node *n;
    /* Only the categories with something in them are listed. */
    while ((n = RemHead(&g_category_list))) (void)n;
    for (i = 0; i < NUM_CATEGORIES; i++) {
        struct Category *c = &g_categories[i];
        WORD fresh = catalog_new_count(c);
        if (fresh)
            str_format(c->label, sizeof(c->label), "%s (%ld new)", (ULONG)c->name, (LONG)fresh);
        else
            str_format(c->label, sizeof(c->label), "%s (%ld)", (ULONG)c->name, (LONG)c->count);
        c->node.ln_Name = (char *)c->label;
        if (c->count || i < 2) AddTail(&g_category_list, &c->node);   /* Pitch, Team Database always */
    }
}

void catalog_init(void)
{
    LONG i;
    new_list(&g_category_list);
    for (i = 0; i < NUM_CATEGORIES; i++) new_list(&g_categories[i].packs);
    s_installed = AllocVec(sizeof(struct ModInfo) * MAX_INSTALLED, MEMF_ANY | MEMF_CLEAR);
    relabel_categories();
}

void catalog_free(void)
{
    LONG i;
    for (i = 0; i < NUM_CATEGORIES; i++) free_packs(&g_categories[i]);
    if (s_offered) FreeVec(s_offered);
    if (s_installed) FreeVec(s_installed);
    s_offered = NULL;
    s_installed = NULL;
    s_offered_count = s_installed_count = 0;
}

/* One entry of "mods". FALSE if it is not for the Amiga, or lacks what a
 * download needs. */
static BOOL read_entry(struct Json *j, struct Offered *o)
{
    char key[24];
    BOOL amiga = FALSE, any_targets = FALSE;
    LONG size = 0;

    memset(o, 0, sizeof(*o));
    if (!json_object_begin(j)) return FALSE;
    while (json_next_key(j, key, sizeof(key))) {
        char *dst = NULL;
        ULONG n = 0;
        if (str_equal(key, "uuid")) dst = o->uuid, n = sizeof(o->uuid);
        else if (str_equal(key, "slug")) dst = o->slug, n = sizeof(o->slug);
        else if (str_equal(key, "title")) dst = o->title, n = sizeof(o->title);
        else if (str_equal(key, "name") && !o->title[0]) dst = o->title, n = sizeof(o->title);
        else if (str_equal(key, "author")) dst = o->author, n = sizeof(o->author);
        else if (str_equal(key, "version")) dst = o->version, n = sizeof(o->version);
        else if (str_equal(key, "slot")) dst = o->slot, n = sizeof(o->slot);
        else if (str_equal(key, "category")) dst = o->category, n = sizeof(o->category);
        else if (str_equal(key, "changes")) dst = o->changes, n = sizeof(o->changes);
        else if (str_equal(key, "about")) dst = o->about, n = sizeof(o->about);
        else if (str_equal(key, "url")) dst = o->url, n = sizeof(o->url);
        else if (str_equal(key, "sha256")) dst = o->sha256, n = sizeof(o->sha256);
        if (dst && json_peek(j) == '"') {
            json_string(j, dst, n);
        } else if (str_equal(key, "size")) {
            json_integer(j, &size);
        } else if (str_equal(key, "targets") && json_peek(j) == '[') {
            char t[16];
            any_targets = TRUE;
            json_array_begin(j);
            while (json_array_next(j)) {
                if (json_peek(j) == '"' && json_string(j, t, sizeof(t))) {
                    if (str_equal(t, "amiga")) amiga = TRUE;
                } else {
                    json_skip(j);
                }
            }
        } else {
            json_skip(j);
        }
    }
    o->size = size > 0 ? (ULONG)size : 0;
    if (!o->version[0]) str_copy(o->version, "1.0", sizeof(o->version));
    if (!o->title[0]) str_copy(o->title, o->slug, sizeof(o->title));
    /* A mod that names no targets is the PC's (§10.1 makes "targets" required). */
    return !j->error && any_targets && amiga && o->uuid[0] && o->slug[0] && o->url[0] &&
           strlen(o->slug) <= 30;
}

/* Walks index.json's "mods". With `out` NULL it only counts the Amiga's mods. */
static BOOL walk_index(const char *text, ULONG len, struct Offered *out, LONG max, LONG *count,
                       LONG *version, BOOL *is_index)
{
    struct Json j;
    char key[24], val[40];
    struct Offered *tmp = out ? NULL : AllocVec(sizeof(struct Offered), MEMF_ANY);
    LONG n = 0;

    *version = 0;
    *is_index = FALSE;
    if (!out && !tmp) return FALSE;
    json_init(&j, text, len);
    if (json_object_begin(&j)) {
        while (json_next_key(&j, key, sizeof(key))) {
            if (str_equal(key, "format") && json_peek(&j) == '"') {
                json_string(&j, val, sizeof(val));
                *is_index = str_equal(val, "openswos-mod-index");
            } else if (str_equal(key, "version") && json_peek(&j) != '"') {
                json_integer(&j, version);
            } else if (str_equal(key, "mods") && json_peek(&j) == '[') {
                json_array_begin(&j);
                while (json_array_next(&j)) {
                    if (json_peek(&j) != '{') json_skip(&j);
                    else if (read_entry(&j, out ? &out[n < max ? n : max - 1] : tmp) &&
                             (!out || n < max))
                        n++;
                    if (j.error) break;
                }
            } else {
                json_skip(&j);
            }
        }
    }
    if (tmp) FreeVec(tmp);
    *count = n;
    return !j.error;
}

BOOL catalog_load_index(const char *text, ULONG len, char *err, ULONG err_size)
{
    LONG version, n, got;
    BOOL is_index;
    struct Offered *list;

    /* Counted first, so the table is the size it needs: an Amiga with 1 MB
     * has no room for a guess. */
    if (!walk_index(text, len, NULL, 0, &n, &version, &is_index) || !is_index) {
        str_copy(err, "the server sent something that is not a mod index", err_size);
        return FALSE;
    }
    if (version != 1) {
        str_format(err, err_size, "a newer mod index (version %ld) - update SWOS-DLC", version);
        return FALSE;
    }
    list = AllocVec(sizeof(struct Offered) * (n ? n : 1), MEMF_ANY | MEMF_CLEAR);
    if (!list) {
        str_copy(err, "out of memory", err_size);
        return FALSE;
    }
    walk_index(text, len, list, n, &got, &version, &is_index);
    if (s_offered) FreeVec(s_offered);
    s_offered = list;
    s_offered_count = got;
    g_index_loaded = TRUE;
    return TRUE;
}

static BOOL is_enabled(char ids[][MOD_SLUG_LEN], LONG n, const char *slug)
{
    LONG i;
    for (i = 0; i < n; i++)
        if (str_equal(ids[i], slug)) return TRUE;
    return FALSE;
}

static void add_pack(struct Category *cat, struct Pack *p)
{
    static const char *const tag[] = {"[NEW]", "[ OK]", "[UPD]", "[OLD]"};
    const char *t = tag[p->status];
    if (p->enabled && p->status == PACK_INSTALLED) t = "[ ON]";
    str_format(p->label, sizeof(p->label), "%s %s", (ULONG)t, (ULONG)p->title);
    p->node.ln_Name = p->label;
    AddTail(&cat->packs, &p->node);
    cat->count++;
}

void catalog_merge(const char *root)
{
    char (*ids)[MOD_SLUG_LEN] = AllocVec(MODS_LIST_MAX * MOD_SLUG_LEN, MEMF_ANY | MEMF_CLEAR);
    LONG enabled = 0, i, k;
    BOOL *matched;

    for (i = 0; i < NUM_CATEGORIES; i++) free_packs(&g_categories[i]);
    s_installed_count = s_installed && root[0] ? mods_scan(root, s_installed, MAX_INSTALLED) : 0;
    if (ids && root[0]) enabled = mods_enabled(root, ids, MODS_LIST_MAX);
    matched = AllocVec(sizeof(BOOL) * (MAX_INSTALLED + 1), MEMF_ANY | MEMF_CLEAR);

    for (i = 0; i < s_offered_count; i++) {
        struct Offered *o = &s_offered[i];
        struct Pack *p = AllocVec(sizeof(struct Pack), MEMF_ANY | MEMF_CLEAR);
        if (!p) break;
        p->uuid = o->uuid;
        str_copy(p->slug, o->slug, sizeof(p->slug));
        p->title = o->title;
        p->author = o->author;
        p->version = o->version;
        p->slot = o->slot;
        p->changes = o->changes;
        p->about = o->about;
        p->url = o->url;
        p->sha256 = o->sha256;
        p->size = o->size;
        p->status = PACK_NEW;
        for (k = 0; k < s_installed_count; k++) {
            if (!str_equal(s_installed[k].uuid, o->uuid)) continue;
            if (matched) matched[k] = TRUE;
            /* Installed under the folder it has on disk, which data/MODS names. */
            str_copy(p->slug, s_installed[k].slug, sizeof(p->slug));
            str_copy(p->installed_version, s_installed[k].version, sizeof(p->installed_version));
            p->status = str_equal(s_installed[k].version, o->version) ? PACK_INSTALLED : PACK_UPDATE;
            p->enabled = is_enabled(ids, enabled, p->slug);
            break;
        }
        add_pack(category_for(o->category), p);
    }

    /* Installed, but no longer offered (or offered elsewhere, before a Refresh). */
    for (k = 0; k < s_installed_count; k++) {
        struct ModInfo *m = &s_installed[k];
        struct Pack *p;
        if (matched && matched[k]) continue;
        p = AllocVec(sizeof(struct Pack), MEMF_ANY | MEMF_CLEAR);
        if (!p) break;
        p->uuid = m->uuid;
        str_copy(p->slug, m->slug, sizeof(p->slug));
        p->title = m->title;
        p->author = m->author;
        p->version = m->version;
        str_copy(p->installed_version, m->version, sizeof(p->installed_version));
        p->slot = m->slot;
        p->changes = m->changes;
        p->about = g_index_loaded ? "Installed, but no longer offered."
                                  : "Installed. Press Refresh to see what is offered.";
        p->url = p->sha256 = "";
        p->status = g_index_loaded ? PACK_REMOVED : PACK_INSTALLED;
        p->enabled = is_enabled(ids, enabled, p->slug);
        add_pack(category_for(m->category), p);
    }
    if (matched) FreeVec(matched);
    if (ids) FreeVec(ids);
    relabel_categories();
}

struct Pack *catalog_pack_at(struct Category *cat, LONG index)
{
    struct Node *n;
    LONG i = 0;
    if (index < 0) return NULL;
    for (n = cat->packs.lh_Head; n->ln_Succ; n = n->ln_Succ, i++)
        if (i == index) return (struct Pack *)n;
    return NULL;
}

LONG catalog_index_of(struct Category *cat, const char *uuid)
{
    struct Node *n;
    LONG i = 0;
    for (n = cat->packs.lh_Head; n->ln_Succ; n = n->ln_Succ, i++)
        if (str_equal(((struct Pack *)n)->uuid, uuid)) return i;
    return -1;
}

WORD catalog_new_count(struct Category *cat)
{
    struct Node *n;
    WORD c = 0;
    for (n = cat->packs.lh_Head; n && n->ln_Succ; n = n->ln_Succ) {
        UBYTE s = ((struct Pack *)n)->status;
        if (s == PACK_NEW || s == PACK_UPDATE) c++;
    }
    return c;
}

const struct ModInfo *catalog_installed(LONG *count)
{
    *count = s_installed_count;
    return s_installed;
}

struct Pack *catalog_enabled_in(struct Category *cat)
{
    struct Node *n;
    for (n = cat->packs.lh_Head; n->ln_Succ; n = n->ln_Succ)
        if (((struct Pack *)n)->enabled) return (struct Pack *)n;
    return NULL;
}

const char *pack_status_name(UBYTE status)
{
    switch (status) {
    case PACK_NEW: return "New";
    case PACK_INSTALLED: return "Installed";
    case PACK_UPDATE: return "Update available";
    case PACK_REMOVED: return "No longer offered";
    }
    return "?";
}
