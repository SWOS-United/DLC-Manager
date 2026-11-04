/* catalog.c -- see catalog.h. */
#include "catalog.h"
#include "util.h"

#include <exec/memory.h>
#include <proto/exec.h>

struct Category g_categories[NUM_CATEGORIES];
struct List g_category_list;

/* Ids and labels in the PC manager's order (core/src/dlc_catalog.cpp). */
static const char *const kIds[NUM_CATEGORIES][2] = {
    {"teamdb", "Team Database"},   {"pitch", "Pitch"},
    {"cup", "Cup"},                {"commentary", "Commentary"},
    {"fx", "Sound Effects"},       {"bgi2", "Background Image"},
    {"tmusic", "Title Music"},     {"mmusic", "Menu Music"},
    {"sprite_ball", "Ball"},       {"sprite_gk", "Goalkeeper"},
    {"sprite_goalnet", "Goal Net"}, {"sprite_manager", "Manager"},
    {"sprite_referee", "Referee"}, {"sprite_team1", "Team 1 Kit"},
    {"sprite_team2", "Team 2 Kit"}, {"sprite_team3", "Team 3 Kit"},
    {"sprite_flag", "Corner Flags"},
};

/* --- Placeholder data -------------------------------------------------- */
/* PLACEHOLDER: shown until a catalogue is fetched, and whenever there is no
 * TCP/IP stack. A few real titles so the demo looks like the real thing; the
 * rest are obviously fake. `installed` is the version "on disk", NULL if not
 * installed. */
struct Seed {
    const char *title, *author, *version, *installed;
};

static const struct Seed kSeedTeamDb[] = {
    {"1996_97 - Original Amiga _ PC DOS", "Sensible Software", "1.0", "1.0"},
    {"Placeholder - 2024_25 Season", "Placeholder Author", "1.2", "1.1"},
    {"Placeholder - Retro Leagues", "Placeholder Author", "1.0", NULL},
};
static const struct Seed kSeedPitch[] = {
    {"1994 - Moon Soccer", "Sensible Software and starwindz", "1.0", NULL},
    {"1994 - SWOS 1.1 (Amiga)", "Sensible Software and starwindz", "1.0", "1.0"},
    {"1997 - 96_97 - Original", "Sensible Software", "1.0", "1.0"},
    {"1998 - Worldcup 98 (Amiga)", "Dominic Cresswell and starwindz", "1.0", NULL},
    {"2020 - Official 2021", "Placeholder Author", "1.1", "1.0"},
};
static const struct Seed kSeedCup[] = {
    {"1930 FIFA WORLD CUP (Uruguay)", "Insane", "1.0", NULL},
    {"1934 FIFA WORLD CUP (Italy)", "Insane", "1.0", NULL},
    {"1964 UEFA EURO CUP (Spain)", "Placeholder Author", "1.0", "1.0"},
};
static const struct Seed kSeedCommentary[] = {
    {"English - Original 96_97 (Jonathan Pearce)", "Sensible Software", "1.0", "1.0"},
    {"English - Barry Davies", "Carling4737 sir", "1.1", "1.0"},
    {"English - Clive Tyldesley", "Carling4737 sir", "1.0", NULL},
};
static const struct Seed kSeedFx[] = {
    {"Original 96_97", "Sensible Software", "1.0", "1.0"},
    {"Placeholder - Stadium FX", "Placeholder Author", "1.0", NULL},
};
static const struct Seed kSeedBgi[] = {
    {"Blue (DOS HD)", "Sensible Software", "1.0", "1.0"},
    {"Placeholder - Green Stripes", "Placeholder Author", "1.0", NULL},
};
static const struct Seed kSeedTMusic[] = {
    {"AMIGA - Goalscoringsuperstarhero", "Sensible Software", "1.0", "1.0"},
    {"Placeholder - Remix", "Placeholder Author", "1.0", NULL},
};
static const struct Seed kSeedMMusic[] = {
    {"AMIGA - SWOS 96_97", "Sensible Software", "1.0", "1.0"},
    {"Placeholder - Chiptune", "Placeholder Author", "1.0", NULL},
};
static const struct Seed kSeedSprite[] = {
    {"default", "Sensible Software", "1.0", "1.0"},
    {"Placeholder - Variant A", "Placeholder Author", "1.0", NULL},
    {"Placeholder - Variant B", "Placeholder Author", "1.0", NULL},
};

struct SeedSet {
    const struct Seed *seeds;
    WORD count;
};
#define SEEDS(a) {a, sizeof(a) / sizeof(a[0])}

static const struct SeedSet kSeeds[NUM_CATEGORIES] = {
    SEEDS(kSeedTeamDb), SEEDS(kSeedPitch),  SEEDS(kSeedCup),    SEEDS(kSeedCommentary),
    SEEDS(kSeedFx),     SEEDS(kSeedBgi),    SEEDS(kSeedTMusic), SEEDS(kSeedMMusic),
    SEEDS(kSeedSprite), SEEDS(kSeedSprite), SEEDS(kSeedSprite), SEEDS(kSeedSprite),
    SEEDS(kSeedSprite), SEEDS(kSeedSprite), SEEDS(kSeedSprite), SEEDS(kSeedSprite),
    SEEDS(kSeedSprite),
};

/* amiga.lib's NewList, which a -nostdlib build does not have. */
static void init_list(struct List *l)
{
    l->lh_Head = (struct Node *)&l->lh_Tail;
    l->lh_Tail = NULL;
    l->lh_TailPred = (struct Node *)&l->lh_Head;
}

/* --- Packs ------------------------------------------------------------- */

static char *dup_into(char **cursor, const char *s)
{
    char *start = *cursor;
    ULONG n = strlen(s) + 1;
    memcpy(start, s, n);
    *cursor += n;
    return start;
}

/* One allocation per pack: the struct, then its four strings. */
static struct Pack *add_pack(struct Category *cat, const char *title, const char *author,
                             const char *version, const char *desc, BOOL from_server)
{
    ULONG size = sizeof(struct Pack) + strlen(title) + strlen(author) + strlen(version) +
                 strlen(desc) + 4;
    struct Pack *p = AllocVec(size, MEMF_CLEAR);
    char *cursor;
    if (!p) return NULL;

    cursor = (char *)(p + 1);
    p->title = dup_into(&cursor, title);
    p->author = dup_into(&cursor, author);
    p->version = dup_into(&cursor, version);
    p->desc = dup_into(&cursor, desc);
    p->from_server = from_server;
    p->node.ln_Name = p->label;
    AddTail(&cat->packs, &p->node);
    cat->count++;
    return p;
}

static void free_packs(struct Category *cat, BOOL server_too)
{
    struct Node *n = cat->packs.lh_Head, *next;
    while ((next = n->ln_Succ)) {
        struct Pack *p = (struct Pack *)n;
        if (server_too || !p->from_server) {
            Remove(n);
            FreeVec(p);
            cat->count--;
        }
        n = next;
    }
}

static struct Installed *find_installed(struct Category *cat, const char *title)
{
    WORD i;
    for (i = 0; i < cat->num_installed; i++)
        if (str_equal(cat->installed[i].title, title)) return &cat->installed[i];
    return NULL;
}

static struct Pack *find_server_pack(struct Category *cat, const char *title)
{
    struct Node *n;
    for (n = cat->packs.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        struct Pack *p = (struct Pack *)n;
        if (p->from_server && str_equal(p->title, title)) return p;
    }
    return NULL;
}

const char *pack_status_name(UBYTE status)
{
    switch (status) {
    case PACK_INSTALLED: return "Installed";
    case PACK_UPDATE: return "Update available";
    case PACK_REMOVED: return "Removed from server";
    default: return "New";
    }
}

static const char *status_tag(UBYTE status)
{
    switch (status) {
    case PACK_INSTALLED: return "[ OK] ";
    case PACK_UPDATE: return "[UPD] ";
    case PACK_REMOVED: return "[REM] ";
    default: return "[NEW] ";
    }
}

void catalog_merge(struct Category *cat)
{
    struct Node *n;
    WORD i, fresh;

    free_packs(cat, FALSE);  /* drop the previous merge's Removed rows */

    for (n = cat->packs.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        struct Pack *p = (struct Pack *)n;
        struct Installed *inst = find_installed(cat, p->title);
        if (!inst)
            p->status = PACK_NEW;
        else
            p->status = str_equal(inst->version, p->version) ? PACK_INSTALLED : PACK_UPDATE;
    }

    /* Installed but gone from the server: listed so it can still be removed. */
    for (i = 0; i < cat->num_installed; i++) {
        struct Installed *inst = &cat->installed[i];
        if (!find_server_pack(cat, inst->title)) {
            struct Pack *p = add_pack(cat, inst->title, "", inst->version, "", FALSE);
            if (p) p->status = PACK_REMOVED;
        }
    }

    for (n = cat->packs.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        struct Pack *p = (struct Pack *)n;
        str_copy(p->label, status_tag(p->status), sizeof(p->label));
        str_append(p->label, p->title, sizeof(p->label));
    }

    fresh = catalog_new_count(cat);
    if (fresh)
        str_format(cat->label, sizeof(cat->label), "%s (%ld)", (ULONG)cat->name, (LONG)fresh);
    else
        str_copy(cat->label, cat->name, sizeof(cat->label));
}

void catalog_init(void)
{
    WORD c, i;

    init_list(&g_category_list);
    for (c = 0; c < NUM_CATEGORIES; c++) {
        struct Category *cat = &g_categories[c];
        const struct SeedSet *set = &kSeeds[c];

        memset(cat, 0, sizeof(*cat));
        cat->id = kIds[c][0];
        cat->name = kIds[c][1];
        cat->node.ln_Name = cat->label;
        init_list(&cat->packs);

        for (i = 0; i < set->count; i++) {
            const struct Seed *s = &set->seeds[i];
            add_pack(cat, s->title, s->author, s->version, "", TRUE);
            if (s->installed && cat->num_installed < MAX_INSTALLED) {
                struct Installed *inst = &cat->installed[cat->num_installed++];
                str_copy(inst->title, s->title, sizeof(inst->title));
                str_copy(inst->version, s->installed, sizeof(inst->version));
                if (!cat->applied[0]) str_copy(cat->applied, s->title, sizeof(cat->applied));
            }
        }
        /* PLACEHOLDER: one pack that is "installed" but no longer published,
         * so the Removed state is visible in the demo. */
        if (c == 1 && cat->num_installed < MAX_INSTALLED) {
            struct Installed *inst = &cat->installed[cat->num_installed++];
            str_copy(inst->title, "Placeholder - Withdrawn Pitch", sizeof(inst->title));
            str_copy(inst->version, "0.9", sizeof(inst->version));
        }
        catalog_merge(cat);
        AddTail(&g_category_list, &cat->node);
    }
}

void catalog_free(void)
{
    WORD c;
    for (c = 0; c < NUM_CATEGORIES; c++) free_packs(&g_categories[c], TRUE);
}

/* --- CSV --------------------------------------------------------------- */

/* Splits one line in place into up to `max` fields. Comma delimited, '"'
 * quoting with "" for a literal quote -- what Delphi's TStringList did. */
static WORD split_csv(char *line, char **fields, WORD max)
{
    WORD count = 0;
    char *src = line;

    while (count < max) {
        char *out = src, *start = src;
        BOOL quoted = FALSE;

        while (*src == ' ' || *src == '\t') src++;
        if (*src == '"') {
            quoted = TRUE;
            src++;
        }
        out = start = src;
        for (;;) {
            char c = *src;
            if (!c) break;
            if (quoted) {
                if (c == '"') {
                    if (src[1] == '"') {
                        *out++ = '"';
                        src += 2;
                        continue;
                    }
                    quoted = FALSE;
                    src++;
                    continue;
                }
            } else if (c == ',') {
                break;
            }
            *out++ = c;
            src++;
        }
        {
            BOOL more = *src == ',';
            *out = 0;
            while (out > start && (out[-1] == ' ' || out[-1] == '\t')) *--out = 0;
            fields[count++] = start;
            if (!more) break;
            src++;
        }
    }
    return count;
}

BOOL catalog_load_csv(struct Category *cat, char *csv)
{
    char *line = csv;
    BOOL header = TRUE;

    /* Same test as DlcCatalog::looks_like_catalog: the header row. */
    if (!str_starts(csv, "Name,")) return FALSE;

    free_packs(cat, TRUE);
    while (*line) {
        char *eol = line, *next;
        char *f[4];
        WORD n;

        while (*eol && *eol != '\n') eol++;
        next = *eol ? eol + 1 : eol;
        *eol = 0;
        if (eol > line && eol[-1] == '\r') eol[-1] = 0;

        if (header) {
            header = FALSE;
        } else if (*line && *line != '#') {
            n = split_csv(line, f, 4);
            if (n > 0 && f[0][0])
                add_pack(cat, f[0], n > 1 ? f[1] : "", n > 2 ? f[2] : "", n > 3 ? f[3] : "",
                         TRUE);
        }
        line = next;
    }
    cat->online = TRUE;
    catalog_merge(cat);
    return TRUE;
}

/* --- Queries and actions ----------------------------------------------- */

struct Pack *catalog_pack_at(struct Category *cat, LONG index)
{
    struct Node *n;
    if (index < 0) return NULL;
    for (n = cat->packs.lh_Head; n->ln_Succ; n = n->ln_Succ)
        if (index-- == 0) return (struct Pack *)n;
    return NULL;
}

LONG catalog_index_of(struct Category *cat, const char *title)
{
    struct Node *n;
    LONG i = 0;
    for (n = cat->packs.lh_Head; n->ln_Succ; n = n->ln_Succ, i++)
        if (str_equal(((struct Pack *)n)->title, title)) return i;
    return -1;
}

WORD catalog_new_count(struct Category *cat)
{
    struct Node *n;
    WORD count = 0;
    for (n = cat->packs.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        UBYTE s = ((struct Pack *)n)->status;
        if (s == PACK_NEW || s == PACK_UPDATE) count++;
    }
    return count;
}

void catalog_mark_installed(struct Category *cat, struct Pack *pack)
{
    struct Installed *inst = find_installed(cat, pack->title);
    if (!inst) {
        if (cat->num_installed >= MAX_INSTALLED) return;
        inst = &cat->installed[cat->num_installed++];
        str_copy(inst->title, pack->title, sizeof(inst->title));
    }
    str_copy(inst->version, pack->version, sizeof(inst->version));
}

void catalog_remove(struct Category *cat, struct Pack *pack)
{
    WORD i;
    for (i = 0; i < cat->num_installed; i++) {
        if (str_equal(cat->installed[i].title, pack->title)) {
            cat->installed[i] = cat->installed[--cat->num_installed];
            break;
        }
    }
    if (str_equal(cat->applied, pack->title)) cat->applied[0] = 0;
}
