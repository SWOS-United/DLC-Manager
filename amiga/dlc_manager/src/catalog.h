/* catalog.h -- the mods on offer and on disk, grouped into categories.
 *
 * "On offer" is SWOS United Amiga's mod index (index.json, format
 * openswos-mod-index v1, only mods whose targets include "amiga"); "on disk"
 * is the game drawer's mods/ (mods.h). They are merged by uuid into one list
 * a category, each pack New / Installed / Update / Removed (installed, but
 * no longer offered), as the PC manager's merge does, plus whether data/MODS
 * has it on.
 */
#ifndef SWOSDLC_CATALOG_H
#define SWOSDLC_CATALOG_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/nodes.h>

#include "mods.h"

#define NUM_CATEGORIES 8
#define URL_LEN 160
#define ABOUT_LEN 160

enum PackStatus { PACK_NEW, PACK_INSTALLED, PACK_UPDATE, PACK_REMOVED };

/* A row. Its strings point into the index's table or the installed list, so
 * they hold until the next catalog_load_index / catalog_merge -- and a load
 * must always be followed by a merge. */
struct Pack {
    struct Node node;               /* ln_Name -> label, what the listview shows */
    const char *uuid, *title, *author, *slot, *changes, *about;
    const char *version;            /* offered; the installed one for Removed rows */
    const char *url, *sha256;       /* "" when not offered */
    ULONG size;
    char slug[MOD_SLUG_LEN];        /* the folder: the installed one, else the offered */
    char installed_version[MOD_SHORT_LEN];
    UBYTE status;
    BOOL enabled;
    char label[MOD_TITLE_LEN + 8];
};

struct Category {
    struct Node node;               /* ln_Name -> label, for the category listview */
    const char *id;                 /* the index's "category": "pitch", "teamdb", ... */
    const char *name;
    struct List packs;
    WORD count;
    char label[40];
};

extern struct Category g_categories[NUM_CATEGORIES];
extern struct List g_category_list;
extern BOOL g_index_loaded;         /* a Refresh has read the index this run */

void catalog_init(void);
void catalog_free(void);

/* Reads index.json's mods (only those for the Amiga). FALSE, keeping the
 * previous list, if it is not an index this manager understands; `err` says
 * why. Does not merge: call catalog_merge after. */
BOOL catalog_load_index(const char *text, ULONG len, char *err, ULONG err_size);

/* Rebuilds every category's packs from the index and what is installed in
 * `root` (and data/MODS there). */
void catalog_merge(const char *root);

struct Pack *catalog_pack_at(struct Category *cat, LONG index);
LONG catalog_index_of(struct Category *cat, const char *uuid);
WORD catalog_new_count(struct Category *cat);
/* The installed mods mods_scan found in the last merge. */
const struct ModInfo *catalog_installed(LONG *count);
/* The pack of this category that is on, or NULL. */
struct Pack *catalog_enabled_in(struct Category *cat);

const char *pack_status_name(UBYTE status);

#endif
