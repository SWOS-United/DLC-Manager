/* host_test.c -- the portable parts on a PC, against real mod zips.
 *
 *   cc -DHOST_TEST -Isrc tests/host_test.c src/json.c src/sha256.c \
 *      src/inflate.c src/unzip.c src/fio.c -o host_test
 *   ./host_test index <index.json>          prints each mod: slug, sha256, size
 *   ./host_test sha256 <file>               prints the file's sha256
 *   ./host_test unzip <zip> <drawer>        unpacks (name limit lifted)
 *   ./host_test install <root> <zip> <slug> <uuid>
 *   ./host_test on|off|remove <root> <slug>
 *   ./host_test list <root>                 installed mods, and data/MODS
 */
#include <stdio.h>
#include <string.h>
#include "json.h"
#include "sha256.h"
#include "unzip.h"
#include "fio.h"
#include "mods.h"

static int cmd_index(const char *path)
{
    ULONG len;
    char *text = fio_slurp(path, 4UL << 20, &len), key[32], val[256];
    struct Json j;
    int mods = 0;
    if (!text) return 1;
    json_init(&j, text, len);
    if (!json_object_begin(&j)) return 2;
    while (json_next_key(&j, key, sizeof key)) {
        if (strcmp(key, "mods") != 0) {
            if (strcmp(key, "format") == 0 && json_string(&j, val, sizeof val))
                printf("format %s\n", val);
            else json_skip(&j);
            continue;
        }
        json_array_begin(&j);
        while (json_array_next(&j)) {
            char slug[64] = "", sha[80] = "", name[128] = "";
            LONG size = -1;
            json_object_begin(&j);
            while (json_next_key(&j, key, sizeof key)) {
                if (!strcmp(key, "slug")) json_string(&j, slug, sizeof slug);
                else if (!strcmp(key, "sha256")) json_string(&j, sha, sizeof sha);
                else if (!strcmp(key, "name")) json_string(&j, name, sizeof name);
                else if (!strcmp(key, "size")) json_integer(&j, &size);
                else json_skip(&j);
            }
            printf("%s %s %ld %s\n", slug, sha, (long)size, name);
            mods++;
        }
    }
    printf("mods %d error %d\n", mods, j.error);
    return j.error ? 3 : 0;
}

static int cmd_sha(const char *path)
{
    FioFile f = fio_open_read(path);
    struct Sha256 s;
    unsigned char buf[3000];
    char hex[65];
    LONG got;
    if (!f) return 1;
    sha256_init(&s);
    while ((got = fio_read(f, buf, sizeof buf)) > 0) sha256_update(&s, buf, (ULONG)got);
    fio_close(f);
    sha256_hex(&s, hex);
    printf("%s\n", hex);
    return 0;
}

int main(int argc, char **argv)
{
    char err[200];
    if (argc == 3 && !strcmp(argv[1], "index")) return cmd_index(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "sha256")) return cmd_sha(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "unzip")) {
        unzip_name_max = 255;
        if (!unzip_to(argv[2], argv[3], NULL, NULL, err, sizeof err)) {
            printf("FAIL %s\n", err);
            return 1;
        }
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "install")) {
        if (!mods_install_zip(argv[2], argv[3], argv[4], argv[5], NULL, NULL, err, sizeof err)) {
            printf("FAIL %s\n", err);
            return 1;
        }
        return 0;
    }
    if (argc == 4 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off") || !strcmp(argv[1], "remove"))) {
        static struct ModInfo all[64];
        LONG n = mods_scan(argv[2], all, 64), i;
        BOOL ok = FALSE;
        if (!strcmp(argv[1], "off")) ok = mods_turn_off(argv[2], argv[3], err, sizeof err);
        else if (!strcmp(argv[1], "remove")) ok = mods_remove(argv[2], argv[3], err, sizeof err);
        else for (i = 0; i < n; i++)
            if (!strcmp(all[i].slug, argv[3])) ok = mods_turn_on(argv[2], &all[i], all, n, err, sizeof err);
        if (!ok) printf("FAIL %s\n", err);
        return !ok;
    }
    if (argc == 3 && !strcmp(argv[1], "list")) {
        static struct ModInfo all[64];
        char ids[48][MOD_SLUG_LEN];
        LONG n = mods_scan(argv[2], all, 64), i;
        for (i = 0; i < n; i++)
            printf("mod %s v%s slot=%s cat=%s changes=%s on=%d %s\n", all[i].slug, all[i].version,
                   all[i].slot, all[i].category, all[i].changes, mods_is_enabled(argv[2], all[i].slug),
                   all[i].title);
        n = mods_enabled(argv[2], ids, 48);
        for (i = 0; i < n; i++) printf("MODS %s\n", ids[i]);
        return 0;
    }
    fprintf(stderr, "usage: see the top of host_test.c\n");
    return 64;
}
