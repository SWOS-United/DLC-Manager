/* mods.h -- SWOS United Amiga's mods on disk: the drawer's mods/ and its
 * data/MODS list.
 *
 * The Amiga game reads PROGDIR:data/MODS at its first file read, one mod id a
 * line (a trailing '*' marks a mod that is not cosmetic, as OpenSWOS Setup
 * writes it), and then reads each file from PROGDIR:mods/<id>/ first, in that
 * order (OpenSWOS src/resload.c). Under WHDLoad the same two paths are
 * relative to the package's data drawer. `root` below is the drawer holding
 * mods/ and data/: the SWOS drawer, or a WHDLoad package's data/.
 *
 * Lines this manager did not write (the setup's built-in mods) are kept, in
 * their place. Turning a mod on takes off the others of its slot, as the PC
 * manager does: the game would only use the first anyway.
 */
#ifndef SWOSDLC_MODS_H
#define SWOSDLC_MODS_H

#include "port.h"

#define MOD_SLUG_LEN 32       /* MODS_ID_MAX: 31 characters; the Amiga keeps 30 */
#define MOD_UUID_LEN 40
#define MOD_SHORT_LEN 24      /* version, slot, category, changes */
#define MOD_TITLE_LEN 96
#define MODS_LIST_MAX 48      /* MODS_MAX: the game reads no more */

/* What an installed mod's mod.json says. */
struct ModInfo {
    char slug[MOD_SLUG_LEN];  /* its drawer's name, which data/MODS uses */
    char uuid[MOD_UUID_LEN];
    char version[MOD_SHORT_LEN];
    char slot[MOD_SHORT_LEN];
    char category[MOD_SHORT_LEN];
    char changes[MOD_SHORT_LEN];
    char title[MOD_TITLE_LEN];
    char author[MOD_TITLE_LEN];
};

/* Reads <root>/mods/<slug>/mod.json. FALSE if it is missing or not JSON. */
BOOL mods_read_info(const char *root, const char *slug, struct ModInfo *out);

/* Every installed mod (a drawer in <root>/mods/ with a readable mod.json;
 * names starting with '.' are an install in progress). Returns how many,
 * up to `max`. */
LONG mods_scan(const char *root, struct ModInfo *out, LONG max);

/* data/MODS, as ids without their '*'. Returns how many (0 if no file). */
LONG mods_enabled(const char *root, char ids[][MOD_SLUG_LEN], LONG max);
BOOL mods_is_enabled(const char *root, const char *slug);

/* Adds `mod` to data/MODS (with '*' unless cosmetic) and takes out every other
 * installed mod of its slot. `installed`/`count` is mods_scan's result. */
BOOL mods_turn_on(const char *root, const struct ModInfo *mod, const struct ModInfo *installed,
                  LONG count, char *err, ULONG err_size);
BOOL mods_turn_off(const char *root, const char *slug, char *err, ULONG err_size);

/* Unpacks a downloaded zip as <root>/mods/<slug>: into <root>/mods/.<slug>
 * first, its mod.json checked against `uuid`, then put in place of any older
 * copy. The zip is left for the caller to delete. */
typedef BOOL (*ModsProgress)(void *ctx, ULONG done, ULONG total);
BOOL mods_install_zip(const char *root, const char *zip, const char *slug, const char *uuid,
                      ModsProgress progress, void *ctx, char *err, ULONG err_size);

/* Turns it off and deletes its drawer. */
BOOL mods_remove(const char *root, const char *slug, char *err, ULONG err_size);

/* <root>/mods, made if missing. */
BOOL mods_make_root(const char *root, char *err, ULONG err_size);

#endif
