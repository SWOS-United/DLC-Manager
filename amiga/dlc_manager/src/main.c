/* main.c -- SWOS United DLC Manager for AmigaOS 2.04+ (Intuition + GadTools).
 *
 * The PC manager's layout on the Workbench screen: categories on the left,
 * that category's mods on the right, the selected mod's details below, then
 * the actions and a status line with a progress bar.
 *
 * Refresh reads SWOS United Amiga's mod index over bsdsocket.library
 * (Roadshow, AmiTCP, Miami); Download streams a mod's zip into the game
 * drawer's mods/, checks it against the index's sha256, and unpacks it;
 * Turn on/off edits data/MODS, which the game reads; Remove deletes it.
 * The game drawer is this program's own (or a WHDLoad package's data/), or
 * the one picked with Project -> Game drawer, kept in PROGDIR:SWOS-DLC.prefs.
 *
 * From the Shell:  SWOS-DLC [INDEX=<url>] [TEST]
 *   INDEX  reads that index instead of the server's (ENV:SWOS-DLC/IndexURL too)
 *   TEST   refreshes, installs and turns on the first new mod of each
 *          category, writes every status line to PROGDIR:SWOS-DLC.test, quits
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/gadgetclass.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/asl.h>

#include "catalog.h"
#include "fio.h"
#include "mods.h"
#include "net.h"
#include "sha256.h"
#include "util.h"

/* For the Version command. The About box prints it, which also keeps
 * --gc-sections from discarding it as unreferenced. */
static const char version_tag[] = "$VER: SWOS-DLC 0.2 (10.11.2026)";

#define INDEX_URL "http://" DLC_HOST DLC_BASEPATH "/swosutd/amiga/index.json"
#define PREFS_FILE "PROGDIR:SWOS-DLC.prefs"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase;
struct DosLibrary *DOSBase;
struct Library *AslBase;

enum {
    GID_CATEGORIES = 1,
    GID_PACKS,
    GID_AUTHOR,
    GID_VERSION,
    GID_STATUS,
    GID_INFO,
    GID_INUSE,
    GID_REFRESH,
    GID_DOWNLOAD,
    GID_REMOVE,
    GID_TOGGLE,
    GID_QUIT,
    GID_STATUSLINE,
    GID_COUNT
};

enum { MID_REFRESH = 1, MID_DRAWER, MID_ABOUT, MID_QUIT };

static struct NewMenu s_menu[] = {
    {NM_TITLE, (STRPTR) "Project", 0, 0, 0, 0},
    {NM_ITEM, (STRPTR) "Refresh", (STRPTR) "R", 0, 0, (APTR)MID_REFRESH},
    {NM_ITEM, (STRPTR) "Game drawer...", (STRPTR) "G", 0, 0, (APTR)MID_DRAWER},
    {NM_ITEM, NM_BARLABEL, 0, 0, 0, 0},
    {NM_ITEM, (STRPTR) "About...", (STRPTR) "?", 0, 0, (APTR)MID_ABOUT},
    {NM_ITEM, NM_BARLABEL, 0, 0, 0, 0},
    {NM_ITEM, (STRPTR) "Quit", (STRPTR) "Q", 0, 0, (APTR)MID_QUIT},
    {NM_END, 0, 0, 0, 0, 0},
};

static struct TextAttr s_topaz8 = {(STRPTR) "topaz.font", 8, FS_NORMAL, FPF_ROMFONT};

/* --- State ------------------------------------------------------------- */

static struct Screen *s_screen;
static struct DrawInfo *s_dri;
static APTR s_vi;
static struct Window *s_win;
static struct Gadget *s_glist;
static struct Gadget *s_gad[GID_COUNT];
static struct Menu *s_menustrip;

static WORD s_off_x, s_off_y;                  /* window border offsets */
static WORD s_bar_x, s_bar_y, s_bar_w, s_bar_h; /* progress bar, inner coords */
static WORD s_percent = -1;                    /* the bar; -1 when idle */

static LONG s_cat = 0;    /* selected row of the category list */
static LONG s_pack = -1;  /* selected row in the pack list, -1 for none */
static BOOL s_busy;       /* a download or unpack is running */
static BOOL s_cancel;     /* Esc or the close gadget during one */

/* The drawer holding mods/ and data/ (see mods.h); "" until known. */
static char s_root[200];

static char s_index_url[200];     /* INDEX= or ENV:SWOS-DLC/IndexURL; else INDEX_URL */
static BOOL s_test;               /* TEST: see the top */
static FioFile s_test_log;

/* Text gadgets point at these, so they must outlive every GT_SetGadgetAttrs. */
static char s_txt_author[96], s_txt_version[48], s_txt_status[40];
static char s_txt_info[176], s_txt_inuse[96], s_txt_statusline[128];

/* --- Helpers ----------------------------------------------------------- */

static struct Category *cur_cat(void)
{
    struct Node *n;
    LONG i = 0;
    for (n = g_category_list.lh_Head; n->ln_Succ; n = n->ln_Succ, i++)
        if (i == s_cat) return (struct Category *)n;
    return &g_categories[0];
}

static struct Pack *cur_pack(void) { return catalog_pack_at(cur_cat(), s_pack); }

static void set_text(UWORD id, const char *text)
{
    GT_SetGadgetAttrs(s_gad[id], s_win, NULL, GTTX_Text, (ULONG)text, TAG_DONE);
}

static void set_disabled(UWORD id, BOOL off)
{
    GT_SetGadgetAttrs(s_gad[id], s_win, NULL, GA_Disabled, off, TAG_DONE);
}

static void status(const char *text)
{
    str_copy(s_txt_statusline, text, sizeof(s_txt_statusline));
    set_text(GID_STATUSLINE, s_txt_statusline);
    if (s_test_log) {
        fio_write(s_test_log, text, (LONG)strlen(text));
        fio_write(s_test_log, "\n", 1);
    }
}

static void busy_pointer(BOOL on)
{
    /* WA_BusyPointer is V39 (OS 3.0); on 2.x the pointer just stays put. */
    if (IntuitionBase->LibNode.lib_Version >= 39)
        SetWindowPointer(s_win, WA_BusyPointer, on, TAG_DONE);
}

static void draw_progress(void)
{
    struct RastPort *rp = s_win->RPort;
    WORD x = s_off_x + s_bar_x, y = s_off_y + s_bar_y;
    WORD iw = s_bar_w - 4, fill;

    DrawBevelBox(rp, x, y, s_bar_w, s_bar_h, GT_VisualInfo, (ULONG)s_vi, GTBB_Recessed, TRUE,
                 TAG_DONE);
    fill = s_percent > 0 ? (WORD)(((LONG)iw * s_percent) / 100) : 0;
    if (fill > 0) {
        SetAPen(rp, s_dri->dri_Pens[FILLPEN]);
        RectFill(rp, x + 2, y + 1, x + 1 + fill, y + s_bar_h - 2);
    }
    if (fill < iw) {
        SetAPen(rp, s_dri->dri_Pens[BACKGROUNDPEN]);
        RectFill(rp, x + 2 + fill, y + 1, x + 1 + iw, y + s_bar_h - 2);
    }
}

/* Done and total as a percentage without a 32-bit multiply overflowing. */
static WORD percent_of(ULONG done, ULONG total)
{
    if (!total) return 0;
    if (done >= total) return 100;
    if (total > 0x01000000UL) return (WORD)(done / (total / 100));
    return (WORD)((done * 100) / total);
}

/* --- Refreshing gadgets from the model --------------------------------- */

static void update_buttons(void)
{
    struct Pack *p = cur_pack();
    BOOL offered = p && p->url[0];
    BOOL on_disk = p && p->status != PACK_NEW;

    set_disabled(GID_DOWNLOAD, s_busy || !offered || !(p->status == PACK_NEW ||
                                                       p->status == PACK_UPDATE));
    set_disabled(GID_REMOVE, s_busy || !on_disk);
    set_disabled(GID_TOGGLE, s_busy || !on_disk);
    set_disabled(GID_REFRESH, s_busy);
    set_disabled(GID_CATEGORIES, s_busy);
    set_disabled(GID_PACKS, s_busy);
}

static void update_details(void)
{
    struct Pack *p = cur_pack();
    struct Category *cat = cur_cat();
    struct Pack *on = catalog_enabled_in(cat);

    if (p) {
        str_copy(s_txt_author, p->author[0] ? p->author : "-", sizeof(s_txt_author));
        if (p->status == PACK_UPDATE)
            str_format(s_txt_version, sizeof(s_txt_version), "%s (have %s)", (ULONG)p->version,
                       (ULONG)p->installed_version);
        else
            str_copy(s_txt_version, p->version, sizeof(s_txt_version));
        if (p->status == PACK_NEW)
            str_format(s_txt_status, sizeof(s_txt_status), "New, %ld KB",
                       (LONG)((p->size + 1023) / 1024));
        else
            str_format(s_txt_status, sizeof(s_txt_status), "%s, %s",
                       (ULONG)pack_status_name(p->status), (ULONG)(p->enabled ? "on" : "off"));
        str_copy(s_txt_info, p->about[0] ? p->about : p->title, sizeof(s_txt_info));
    } else {
        s_txt_author[0] = s_txt_version[0] = s_txt_status[0] = 0;
        if (!s_root[0])
            str_copy(s_txt_info, "Choose the SWOS drawer: Project > Game drawer", sizeof(s_txt_info));
        else if (!g_index_loaded)
            str_format(s_txt_info, sizeof(s_txt_info), "%ld installed - press Refresh for more",
                       (LONG)cat->count);
        else
            str_format(s_txt_info, sizeof(s_txt_info), "%ld mods, %ld new or updated",
                       (LONG)cat->count, (LONG)catalog_new_count(cat));
    }
    str_copy(s_txt_inuse, on ? on->title : "(none: the game's own)", sizeof(s_txt_inuse));

    set_text(GID_AUTHOR, s_txt_author);
    set_text(GID_VERSION, s_txt_version);
    set_text(GID_STATUS, s_txt_status);
    set_text(GID_INFO, s_txt_info);
    set_text(GID_INUSE, s_txt_inuse);
    update_buttons();
}

/* The lists are detached while their nodes change, as GadTools requires. */
static void detach_lists(void)
{
    GT_SetGadgetAttrs(s_gad[GID_PACKS], s_win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
    GT_SetGadgetAttrs(s_gad[GID_CATEGORIES], s_win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
}

static void attach_lists(BOOL scroll_top)
{
    GT_SetGadgetAttrs(s_gad[GID_CATEGORIES], s_win, NULL, GTLV_Labels, (ULONG)&g_category_list,
                      GTLV_Selected, s_cat, TAG_DONE);
    if (scroll_top)
        GT_SetGadgetAttrs(s_gad[GID_PACKS], s_win, NULL, GTLV_Labels, (ULONG)&cur_cat()->packs,
                          GTLV_Selected, s_pack, GTLV_Top, 0, TAG_DONE);
    else
        GT_SetGadgetAttrs(s_gad[GID_PACKS], s_win, NULL, GTLV_Labels, (ULONG)&cur_cat()->packs,
                          GTLV_Selected, s_pack, TAG_DONE);
}

static void select_category(LONG index)
{
    detach_lists();
    s_cat = index;
    s_pack = -1;
    attach_lists(TRUE);
    update_details();
}

/* Rebuilds the lists from the index and the disk, keeping the selection. */
static void remerge(void)
{
    char keep_uuid[MOD_UUID_LEN], keep_cat[MOD_SHORT_LEN];
    struct Pack *p = cur_pack();
    struct Node *n;
    LONG i = 0;

    keep_uuid[0] = 0;
    str_copy(keep_cat, cur_cat()->id, sizeof(keep_cat));
    if (p) str_copy(keep_uuid, p->uuid, sizeof(keep_uuid));
    detach_lists();
    catalog_merge(s_root);
    s_cat = 0;
    for (n = g_category_list.lh_Head; n->ln_Succ; n = n->ln_Succ, i++)
        if (str_equal(((struct Category *)n)->id, keep_cat)) s_cat = i;
    s_pack = keep_uuid[0] ? catalog_index_of(cur_cat(), keep_uuid) : -1;
    attach_lists(FALSE);
    update_details();
}

/* --- While something runs ---------------------------------------------- */

/* Called between pieces of a download or an unpack: repaints, and notes Esc
 * or the close gadget as a cancel. Everything else waits. */
static void poll_window(void)
{
    struct IntuiMessage *msg;
    while ((msg = GT_GetIMsg(s_win->UserPort))) {
        ULONG cls = msg->Class;
        UWORD code = msg->Code;
        GT_ReplyIMsg(msg);
        if (cls == IDCMP_CLOSEWINDOW || (cls == IDCMP_VANILLAKEY && code == 0x1b))
            s_cancel = TRUE;
        else if (cls == IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh(s_win);
            draw_progress();
            GT_EndRefresh(s_win, TRUE);
        }
    }
}

static void set_percent(WORD pc)
{
    if (pc != s_percent) {
        s_percent = pc;
        draw_progress();
    }
}

/* --- The game drawer --------------------------------------------------- */

static BOOL is_game_root(const char *dir)
{
    char path[240];
    fio_join(path, sizeof(path), dir, "data");
    return dir[0] && fio_is_dir(path);
}

/* The drawer itself, or the data/ of a WHDLoad package chosen as a whole:
 * under WHDLoad the game's data/ and mods/ are inside the package's data/. */
static BOOL resolve_root(const char *dir, char *out, ULONG size)
{
    char inner[240], data2[260];
    fio_join(inner, sizeof(inner), dir, "data");
    fio_join(data2, sizeof(data2), inner, "data");
    if (fio_is_dir(data2)) {
        str_copy(out, inner, size);
        return TRUE;
    }
    if (is_game_root(dir)) {
        str_copy(out, dir, size);
        return TRUE;
    }
    return FALSE;
}

static void load_root(void)
{
    char *text;
    ULONG len;

    s_root[0] = 0;
    text = fio_slurp(PREFS_FILE, 1024, &len);
    if (text) {
        char *p = text;
        while (*p && *p != '\n' && *p != '\r') p++;
        *p = 0;
        if (!resolve_root(text, s_root, sizeof(s_root))) s_root[0] = 0;
        mem_free(text);
    }
    if (!s_root[0]) resolve_root("PROGDIR:", s_root, sizeof(s_root));
}

static void choose_root(void)
{
    struct FileRequester *fr;
    char picked[240], msg[128];

    if (!AslBase) {
        status("asl.library is needed to choose a drawer");
        return;
    }
    fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)"Where is SWOS United?",
                             ASLFR_DrawersOnly, TRUE, ASLFR_Window, (ULONG)s_win,
                             ASLFR_InitialDrawer, (ULONG)(s_root[0] ? s_root : "SYS:"),
                             TAG_DONE);
    if (!fr) return;
    if (AslRequest(fr, NULL)) {
        str_copy(picked, (const char *)fr->fr_Drawer, sizeof(picked));
        if (resolve_root(picked, s_root, sizeof(s_root))) {
            FioFile f = fio_open_write(PREFS_FILE);
            if (f) {
                fio_write(f, picked, (LONG)strlen(picked));
                fio_write(f, "\n", 1);
                fio_close(f);
            }
            remerge();
            str_format(msg, sizeof(msg), "Game drawer: %s", (ULONG)s_root);
            status(msg);
        } else {
            str_format(msg, sizeof(msg), "%s has no data drawer: not a SWOS drawer", (ULONG)picked);
            status(msg);
        }
    }
    FreeAslRequest(fr);
}

/* --- Actions ----------------------------------------------------------- */

static void do_refresh(void)
{
    char msg[128], err[80], host[64], path[160];
    char url[200];
    UWORD port;
    char *body;
    ULONG len;
    LONG got;

    /* Checked on every Refresh: the network may have come up (or gone down)
     * since the window opened. */
    net_open();
    if (!net_ready(msg, sizeof(msg))) {
        status(msg);
        return;
    }
    if (s_index_url[0]) {
        str_copy(url, s_index_url, sizeof(url));
    } else {
        got = GetVar((STRPTR) "SWOS-DLC/IndexURL", (STRPTR)url, sizeof(url), GVF_GLOBAL_ONLY);
        if (got <= 0) str_copy(url, INDEX_URL, sizeof(url));
    }
    if (!net_parse_url(url, host, sizeof(host), &port, path, sizeof(path))) {
        str_format(msg, sizeof(msg), "Not an http:// address: %s", (ULONG)url);
        status(msg);
        return;
    }

    status("Fetching the mod list...");
    busy_pointer(TRUE);
    body = NULL;
    err[0] = 0;
    if (net_http_get(host, port, path, &body, &len, err, sizeof(err))) {
        if (!catalog_load_index(body, len, err, sizeof(err))) {
            str_format(msg, sizeof(msg), "Mod list: %s", (ULONG)err);
            status(msg);
        } else {
            remerge();
            str_format(msg, sizeof(msg), "Mod list from %s (%s)", (ULONG)host,
                       (ULONG)net_stack_name());
            status(msg);
        }
        FreeVec(body);
    } else {
        str_format(msg, sizeof(msg), "Mod list: %s", (ULONG)err);
        status(msg);
    }
    busy_pointer(FALSE);
}

struct DlCtx {
    FioFile out;
    struct Sha256 sha;
    ULONG done, expect;
    BOOL write_failed;
};

static BOOL download_sink(void *vctx, const UBYTE *data, ULONG len, ULONG total)
{
    struct DlCtx *d = vctx;
    if (!fio_write(d->out, data, (LONG)len)) {
        d->write_failed = TRUE;
        return FALSE;
    }
    sha256_update(&d->sha, data, len);
    d->done += len;
    set_percent(percent_of(d->done, total ? total : d->expect));
    poll_window();
    return !s_cancel;
}

static BOOL unpack_progress(void *ctx, ULONG done, ULONG total)
{
    set_percent(percent_of(done, total));
    poll_window();
    return !s_cancel;
}

static void do_download(void)
{
    struct Pack *p = cur_pack();
    char msg[128], err[96], host[64], path[200], zip[240], name[MOD_SLUG_LEN + 6];
    char hex[65], title[MOD_TITLE_LEN], slug[MOD_SLUG_LEN], uuid[MOD_UUID_LEN];
    UWORD port;
    struct DlCtx d;
    BOOL ok;

    if (!p || !p->url[0] || s_busy) return;
    if (!s_root[0]) {
        status("Choose the SWOS drawer first: Project > Game drawer");
        return;
    }
    if (!net_parse_url(p->url, host, sizeof(host), &port, path, sizeof(path))) {
        status("The mod's address is not http://");
        return;
    }
    if (!mods_make_root(s_root, err, sizeof(err))) {
        status(err);
        return;
    }
    /* The pack's strings point into the index, which stays put meanwhile;
     * copied anyway, as the merge at the end frees the row. */
    str_copy(title, p->title, sizeof(title));
    str_copy(slug, p->slug, sizeof(slug));
    str_copy(uuid, p->uuid, sizeof(uuid));

    str_format(name, sizeof(name), ".%s.zip", (ULONG)slug);
    {
        char dir[220];
        fio_join(dir, sizeof(dir), s_root, "mods");
        fio_join(zip, sizeof(zip), dir, name);
    }
    memset(&d, 0, sizeof(d));
    d.expect = p->size;
    sha256_init(&d.sha);
    d.out = fio_open_write(zip);
    if (!d.out) {
        status("Cannot write to the mods drawer (write-protected?)");
        return;
    }

    s_busy = TRUE;
    s_cancel = FALSE;
    update_buttons();
    busy_pointer(TRUE);
    str_format(msg, sizeof(msg), "Downloading %s... (Esc cancels)", (ULONG)title);
    status(msg);
    set_percent(0);

    err[0] = 0;
    ok = net_http_stream(host, port, path, download_sink, &d, err, sizeof(err));
    fio_close(d.out);
    if (ok) {
        sha256_hex(&d.sha, hex);
        if (p->size && d.done != p->size) {
            str_format(err, sizeof(err), "got %lu bytes, the list says %lu", d.done, p->size);
            ok = FALSE;
        } else if (p->sha256[0] && !str_equal(hex, p->sha256)) {
            str_copy(err, "the download is damaged (checksum)", sizeof(err));
            ok = FALSE;
        }
    } else if (s_cancel) {
        str_copy(err, "cancelled", sizeof(err));
    } else if (d.write_failed) {
        str_copy(err, "the disk is full or write-protected", sizeof(err));
    }
    if (ok) {
        str_format(msg, sizeof(msg), "Unpacking %s...", (ULONG)title);
        status(msg);
        set_percent(0);
        ok = mods_install_zip(s_root, zip, slug, uuid, unpack_progress, NULL, err, sizeof(err));
        if (!ok && s_cancel) str_copy(err, "cancelled", sizeof(err));
    }
    fio_delete(zip);

    s_busy = FALSE;
    s_percent = -1;
    busy_pointer(FALSE);
    draw_progress();
    remerge();
    if (ok)
        str_format(msg, sizeof(msg), "Installed %s - Turn on to use it", (ULONG)title);
    else
        str_format(msg, sizeof(msg), "%s: %s", (ULONG)title, (ULONG)err);
    status(msg);
}

static BOOL confirm(const char *text, const char *arg)
{
    if (s_test) return TRUE;
    struct EasyStruct es = {sizeof(struct EasyStruct), 0, (STRPTR) "SWOS DLC Manager",
                            (STRPTR)text, (STRPTR) "Yes|No"};
    return EasyRequest(s_win, &es, NULL, (ULONG)arg) == 1;
}

static void do_remove(void)
{
    struct Pack *p = cur_pack();
    char msg[128], err[96], title[MOD_TITLE_LEN];
    if (!p || p->status == PACK_NEW) return;
    str_copy(title, p->title, sizeof(title));
    if (!confirm("Delete %s from the mods drawer?", title)) return;
    busy_pointer(TRUE);
    if (mods_remove(s_root, p->slug, err, sizeof(err)))
        str_format(msg, sizeof(msg), "Removed %s", (ULONG)title);
    else
        str_format(msg, sizeof(msg), "%s: %s", (ULONG)title, (ULONG)err);
    busy_pointer(FALSE);
    remerge();
    status(msg);
}

static void do_toggle(void)
{
    struct Pack *p = cur_pack();
    char msg[128], err[96];
    BOOL ok;
    if (!p || p->status == PACK_NEW) return;

    if (p->enabled) {
        ok = mods_turn_off(s_root, p->slug, err, sizeof(err));
        if (ok) str_format(msg, sizeof(msg), "%s is off", (ULONG)p->title);
    } else {
        LONG n, i;
        const struct ModInfo *all = catalog_installed(&n);
        ok = FALSE;
        str_copy(err, "it is not installed", sizeof(err));
        for (i = 0; i < n; i++)
            if (str_equal(all[i].slug, p->slug)) {
                ok = mods_turn_on(s_root, &all[i], all, n, err, sizeof(err));
                break;
            }
        if (ok) {
            if (str_equal(p->slot, "pitch"))
                str_format(msg, sizeof(msg), "%s is on - set OPTIONS > PITCHES to 2020",
                           (ULONG)p->title);
            else
                str_format(msg, sizeof(msg), "%s is on", (ULONG)p->title);
        }
    }
    if (!ok) str_format(msg, sizeof(msg), "%s: %s", (ULONG)p->title, (ULONG)err);
    remerge();
    status(msg);
}

static void do_about(void)
{
    struct EasyStruct es = {
        sizeof(struct EasyStruct), 0, (STRPTR) "About SWOS DLC Manager",
        (STRPTR) "SWOS United DLC Manager for the Amiga\n%s\n"
                 "by Ben Baker (Baker76) - baker76.com\n"
                 "https://github.com/SWOS-United/DLC-Manager\n\n"
                 "SWOS United - swosunited.com\n"
                 "Project Management: Playaveli\n"
                 "Engineering Lead: benbaker76\n"
                 "SWOS Reverse Engineering: benbaker76 (Amiga) / zkz (DOS)\n"
                 "SWOS 2020 Feature Engineering: starwindz\n"
                 "SWOS Live (swos.eu): Redhair\n"
                 "SWOS Multiplayer (GGPO-X): thd79\n"
                 "SWOS Web Online Rankings: ElMichaJ\n\n"
                 "Mods from http://%s\n"
                 "Game drawer: %s\n"
                 "Network: %s",
        (STRPTR) "OK"};
    const char *stack = net_available() ? net_stack_name() : "none (offline)";
    EasyRequest(s_win, &es, NULL, (ULONG)(version_tag + 6), (ULONG)DLC_HOST,
                (ULONG)(s_root[0] ? s_root : "(not chosen)"), (ULONG)stack);
}

/* TEST: the first new mod of each category, installed and turned on. */
static void run_test(void)
{
    struct Node *n;
    LONG c = 0;
    char line[160], ids[MODS_LIST_MAX][MOD_SLUG_LEN];
    LONG count, i;

    do_refresh();
    for (n = g_category_list.lh_Head; n->ln_Succ; n = n->ln_Succ, c++) {
        struct Category *cat = (struct Category *)n;
        struct Node *pn;
        LONG row = 0;
        select_category(c);
        for (pn = cat->packs.lh_Head; pn->ln_Succ; pn = pn->ln_Succ, row++)
            if (((struct Pack *)pn)->status == PACK_NEW) break;
        if (!pn->ln_Succ) continue;
        s_pack = row;
        update_details();
        do_download();
        if (cur_pack() && cur_pack()->status == PACK_INSTALLED) do_toggle();
    }
    count = mods_enabled(s_root, ids, MODS_LIST_MAX);
    for (i = 0; i < count; i++) {
        str_format(line, sizeof(line), "data/MODS: %s", (ULONG)ids[i]);
        status(line);
    }
    status("TEST DONE");
}

/* --- Window ------------------------------------------------------------ */

static struct Gadget *make(ULONG kind, struct Gadget *prev, struct NewGadget *ng, WORD x, WORD y,
                           WORD w, WORD h, const char *label, UWORD id, ULONG flags,
                           struct TagItem *tags)
{
    ng->ng_LeftEdge = s_off_x + x;
    ng->ng_TopEdge = s_off_y + y;
    ng->ng_Width = w;
    ng->ng_Height = h;
    ng->ng_GadgetText = (STRPTR)label;
    ng->ng_GadgetID = id;
    ng->ng_Flags = flags;
    s_gad[id] = CreateGadgetA(kind, prev, ng, tags);
    return s_gad[id];
}

static BOOL open_window(void)
{
    struct NewGadget ng;
    struct Gadget *g;
    struct TextAttr *font;
    WORD fh, rows, lv_h, row_h, btn_h, y_rows, y_btn, y_status, inner_w, inner_h;
    WORD lbl = 72, m = 8, cat_w = 168, i;
    static const char *const btn_labels[5] = {"_Refresh", "_Download", "Re_move", "_Turn on/off",
                                              "_Quit"};
    struct TagItem t_cats[] = {{GTLV_Labels, (ULONG)&g_category_list},
                               {GTLV_ShowSelected, 0},
                               {GTLV_Selected, 0},
                               {TAG_DONE, 0}};
    struct TagItem t_packs[] = {{GTLV_Labels, 0}, {GTLV_ShowSelected, 0}, {TAG_DONE, 0}};
    struct TagItem t_text[] = {{GTTX_Border, TRUE}, {TAG_DONE, 0}};
    struct TagItem t_button[] = {{GT_Underscore, '_'}, {TAG_DONE, 0}};

    s_screen = LockPubScreen(NULL);
    if (!s_screen) return FALSE;
    s_dri = GetScreenDrawInfo(s_screen);
    s_vi = GetVisualInfo(s_screen, TAG_DONE);
    if (!s_dri || !s_vi) return FALSE;

    /* The screen font if it fits this layout, topaz 8 otherwise. */
    font = s_screen->Font;
    if (!font || font->ta_YSize > 11) font = &s_topaz8;
    fh = font->ta_YSize;

    s_off_x = s_screen->WBorLeft;
    s_off_y = s_screen->WBorTop + s_screen->Font->ta_YSize + 1;
    inner_w = 600;
    row_h = fh + 6;
    btn_h = fh + 6;

    /* As many list rows as the screen allows, 6..14. Everything but the
     * lists takes a fixed height; see the y positions below. */
    {
        WORD fixed = (fh + 4) + 6 + 4 * row_h + 2 + btn_h + 6 + (fh + 4) + m;
        WORD avail = s_screen->Height - s_off_y - s_screen->WBorBottom - fixed - 4;
        rows = avail / fh;
        if (rows > 14) rows = 14;
        if (rows < 6) rows = 6;
    }
    lv_h = rows * fh + 4;
    y_rows = (fh + 4) + lv_h + 6;
    y_btn = y_rows + 4 * row_h + 2;
    y_status = y_btn + btn_h + 6;
    inner_h = y_status + fh + 4 + m;

    /* The NDK's LP1 macro declares its register variable `volatile T`, which
     * for T = struct Gadget ** qualifies the wrong level; GCC 15 rejects it. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
    g = CreateContext(&s_glist);
#pragma GCC diagnostic pop
    if (!g) return FALSE;
    memset(&ng, 0, sizeof(ng));
    ng.ng_TextAttr = font;
    ng.ng_VisualInfo = s_vi;

    t_cats[2].ti_Data = s_cat;
    t_packs[0].ti_Data = (ULONG)&cur_cat()->packs;
    g = make(LISTVIEW_KIND, g, &ng, m, fh + 4, cat_w, lv_h, "Category", GID_CATEGORIES,
             PLACETEXT_ABOVE, t_cats);
    g = make(LISTVIEW_KIND, g, &ng, m + cat_w + 8, fh + 4, inner_w - cat_w - 8 - 2 * m, lv_h,
             "Mods", GID_PACKS, PLACETEXT_ABOVE, t_packs);

    g = make(TEXT_KIND, g, &ng, lbl, y_rows, inner_w - lbl - m, fh + 4, "Author", GID_AUTHOR,
             PLACETEXT_LEFT, t_text);
    g = make(TEXT_KIND, g, &ng, lbl, y_rows + row_h, 136, fh + 4, "Version", GID_VERSION,
             PLACETEXT_LEFT, t_text);
    g = make(TEXT_KIND, g, &ng, lbl + 136 + 64, y_rows + row_h, inner_w - lbl - 136 - 64 - m,
             fh + 4, "Status", GID_STATUS, PLACETEXT_LEFT, t_text);
    g = make(TEXT_KIND, g, &ng, lbl, y_rows + 2 * row_h, inner_w - lbl - m, fh + 4, "Info",
             GID_INFO, PLACETEXT_LEFT, t_text);
    g = make(TEXT_KIND, g, &ng, lbl, y_rows + 3 * row_h, inner_w - lbl - m, fh + 4, "In use",
             GID_INUSE, PLACETEXT_LEFT, t_text);

    {
        WORD gap = 8, bw = (inner_w - 2 * m - 4 * gap) / 5;
        for (i = 0; i < 5; i++)
            g = make(BUTTON_KIND, g, &ng, m + i * (bw + gap), y_btn, bw, btn_h, btn_labels[i],
                     GID_REFRESH + i, PLACETEXT_IN, t_button);
    }

    s_bar_w = 168;
    s_bar_h = fh + 4;
    s_bar_x = inner_w - m - s_bar_w;
    s_bar_y = y_status;
    g = make(TEXT_KIND, g, &ng, m, y_status, s_bar_x - m - 8, fh + 4, NULL, GID_STATUSLINE, 0,
             t_text);
    if (!g) return FALSE;

    s_menustrip = CreateMenus(s_menu, TAG_DONE);
    if (!s_menustrip || !LayoutMenus(s_menustrip, s_vi, GTMN_NewLookMenus, TRUE, TAG_DONE))
        return FALSE;

    s_win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"SWOS United DLC Manager",
        WA_ScreenTitle, (ULONG)"SWOS United DLC Manager 0.2",
        WA_PubScreen, (ULONG)s_screen,
        WA_InnerWidth, inner_w,
        WA_InnerHeight, inner_h,
        WA_Left, (s_screen->Width - inner_w) / 2 > 0 ? (s_screen->Width - inner_w) / 2 : 0,
        WA_Top, s_screen->BarHeight + 1,
        WA_Gadgets, (ULONG)s_glist,
        WA_IDCMP, LISTVIEWIDCMP | BUTTONIDCMP | TEXTIDCMP | IDCMP_CLOSEWINDOW |
                  IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | IDCMP_VANILLAKEY,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_Activate, TRUE,
        WA_SimpleRefresh, TRUE,
        WA_NewLookMenus, TRUE,
        TAG_DONE);
    if (!s_win) return FALSE;

    SetMenuStrip(s_win, s_menustrip);
    GT_RefreshWindow(s_win, NULL);
    draw_progress();
    return TRUE;
}

static void close_window(void)
{
    if (s_win) {
        ClearMenuStrip(s_win);
        CloseWindow(s_win);
    }
    if (s_menustrip) FreeMenus(s_menustrip);
    FreeGadgets(s_glist);  /* NULL-safe */
    if (s_vi) FreeVisualInfo(s_vi);
    if (s_dri) FreeScreenDrawInfo(s_screen, s_dri);
    if (s_screen) UnlockPubScreen(NULL, s_screen);
}

/* --- Event loop -------------------------------------------------------- */

static BOOL handle_gadget(UWORD id, UWORD code)
{
    switch (id) {
    case GID_CATEGORIES: select_category(code); break;
    case GID_PACKS:
        s_pack = code;
        update_details();
        break;
    case GID_REFRESH: do_refresh(); break;
    case GID_DOWNLOAD: do_download(); break;
    case GID_REMOVE: do_remove(); break;
    case GID_TOGGLE: do_toggle(); break;
    case GID_QUIT: return FALSE;
    }
    return TRUE;
}

/* Keyboard equivalents of the underscored button letters. Disabled buttons
 * must stay disabled from the keyboard too. */
static BOOL handle_key(UWORD key)
{
    UWORD id = 0;
    if (key == 0x1b) return FALSE;   /* Esc: quit (a running job takes it as cancel) */
    switch (key | 0x20) {            /* lower-case */
    case 'r': id = GID_REFRESH; break;
    case 'd': id = GID_DOWNLOAD; break;
    case 'm': id = GID_REMOVE; break;
    case 't': id = GID_TOGGLE; break;
    case 'q': return FALSE;
    }
    if (id && !(s_gad[id]->Flags & GFLG_DISABLED)) return handle_gadget(id, 0);
    return TRUE;
}

static BOOL handle_menu(UWORD code)
{
    while (code != MENUNULL) {
        struct MenuItem *item = ItemAddress(s_menustrip, code);
        if (!item) break;
        switch ((ULONG)GTMENUITEM_USERDATA(item)) {
        case MID_REFRESH: if (!s_busy) do_refresh(); break;
        case MID_DRAWER: if (!s_busy) choose_root(); break;
        case MID_ABOUT: do_about(); break;
        case MID_QUIT: return FALSE;
        }
        code = item->NextSelect;
    }
    return TRUE;
}

static void event_loop(void)
{
    BOOL running = TRUE;

    while (running) {
        struct IntuiMessage *msg;
        WaitPort(s_win->UserPort);
        while (running && (msg = GT_GetIMsg(s_win->UserPort))) {
            ULONG cls = msg->Class;
            UWORD code = msg->Code;
            struct Gadget *gad = (struct Gadget *)msg->IAddress;
            GT_ReplyIMsg(msg);

            switch (cls) {
            case IDCMP_CLOSEWINDOW: running = FALSE; break;
            case IDCMP_GADGETUP: running = handle_gadget(gad->GadgetID, code); break;
            case IDCMP_VANILLAKEY: running = handle_key(code); break;
            case IDCMP_MENUPICK: running = handle_menu(code); break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(s_win);
                draw_progress();
                GT_EndRefresh(s_win, TRUE);
                break;
            }
        }
    }
}

int main(void)
{
    int rc = 20;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR) "intuition.library", 37);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR) "graphics.library", 37);
    GadToolsBase = OpenLibrary((CONST_STRPTR) "gadtools.library", 37);
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR) "dos.library", 37);
    AslBase = OpenLibrary((CONST_STRPTR) "asl.library", 37);   /* optional: Game drawer */

    if (IntuitionBase && GfxBase && GadToolsBase && DOSBase) {
        /* Shell arguments; from Workbench there are none. */
        extern struct WBStartup *g_wb_startup;
        if (!g_wb_startup) {
            LONG args[2] = {0, 0};
            struct RDArgs *rda = ReadArgs((STRPTR) "INDEX/K,TEST/S", args, NULL);
            if (rda) {
                if (args[0]) str_copy(s_index_url, (const char *)args[0], sizeof(s_index_url));
                s_test = args[1] != 0;
                FreeArgs(rda);
            }
        }
        catalog_init();
        load_root();
        catalog_merge(s_root);
        net_open();  /* optional: without it only the installed mods are listed */

        if (open_window()) {
            select_category(s_cat);
            if (s_test) {
                s_test_log = fio_open_write("PROGDIR:SWOS-DLC.test");
                run_test();
                if (s_test_log) fio_close(s_test_log);
                s_test_log = NULL;
            } else
            {
                char msg[128];
                if (!s_root[0])
                    str_copy(msg, "Choose the SWOS drawer: Project > Game drawer", sizeof(msg));
                else if (net_ready(msg, sizeof(msg)))
                    str_format(msg, sizeof(msg), "%s online - press Refresh",
                               (ULONG)net_stack_name());
                status(msg);
            }
            if (!s_test) event_loop();
            rc = 0;
        }
        close_window();
        net_close();
        catalog_free();
    }

    if (AslBase) CloseLibrary(AslBase);
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
