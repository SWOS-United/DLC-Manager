/* catalog.h -- the DLC model: 17 categories, each a list of packs.
 *
 * Mirrors swos::DlcCatalog in core/include/dlc_catalog.h: the same type ids,
 * the same <type>-info.csv format (Name,Author,Version,Desc with a header row
 * and '#' comments), and the same New / Installed / Update / Removed merge of
 * the server list against what is installed.
 *
 * DEMO: "installed" is held in memory and seeded with placeholders. A real
 * build would keep it in <type>-info.csv files on disk, as the PC one does.
 */
#ifndef SWOSDLC_CATALOG_H
#define SWOSDLC_CATALOG_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/nodes.h>

#define NUM_CATEGORIES 17
#define MAX_INSTALLED  24
#define TITLE_LEN      80
#define VERSION_LEN    12

enum PackStatus { PACK_NEW, PACK_INSTALLED, PACK_UPDATE, PACK_REMOVED };

struct Pack {
    struct Node node;       /* ln_Name -> label, what the listview shows */
    char *title;
    char *author;
    char *version;          /* server version; local one for Removed rows */
    char *desc;
    BOOL from_server;       /* FALSE for Removed rows synthesised by merge */
    UBYTE status;
    char label[TITLE_LEN + 12];
};

struct Installed {
    char title[TITLE_LEN];
    char version[VERSION_LEN];
};

struct Category {
    struct Node node;       /* ln_Name -> label, for the category listview */
    const char *id;         /* wire/directory name: "pitch", "bgi2", ... */
    const char *name;       /* "Pitch", "Background Image", ... */
    struct List packs;
    WORD count;
    BOOL online;            /* list came from the server, not placeholders */
    struct Installed installed[MAX_INSTALLED];
    WORD num_installed;
    char applied[TITLE_LEN];
    char label[40];
};

extern struct Category g_categories[NUM_CATEGORIES];
extern struct List g_category_list;

void catalog_init(void);          /* placeholder data for every category */
void catalog_free(void);

/* Replaces a category's server list with a downloaded <type>-info.csv.
 * Returns FALSE, changing nothing, if `csv` is not a catalogue (the server
 * answers a missing file with a redirect to an HTML page). Modifies `csv`. */
BOOL catalog_load_csv(struct Category *cat, char *csv);

/* Recomputes every pack's status from the installed table, and the labels. */
void catalog_merge(struct Category *cat);

struct Pack *catalog_pack_at(struct Category *cat, LONG index);
LONG catalog_index_of(struct Category *cat, const char *title);
WORD catalog_new_count(struct Category *cat);

void catalog_mark_installed(struct Category *cat, struct Pack *pack);
void catalog_remove(struct Category *cat, struct Pack *pack);

const char *pack_status_name(UBYTE status);

#endif
