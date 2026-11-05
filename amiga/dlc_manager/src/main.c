/* main.c -- SWOS United DLC Manager for AmigaOS 2.04+ (Intuition + GadTools).
 *
 * A demo of the PC manager's layout on the Workbench screen: categories on
 * the left, that category's packs on the right, the selected pack's details
 * below, then the actions and a status line with a progress bar.
 *
 * What is real: the UI, the catalogue model and its New / Installed /
 * Update / Removed merge, and Refresh, which fetches the live
 * <type>-info.csv from the DLC server over bsdsocket.library (Roadshow).
 *
 * What is a placeholder: Download (a timed progress bar, nothing is fetched
 * or unpacked), the installed list (memory only), and Apply (recorded, not
 * written anywhere). Search for PLACEHOLDER.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/gadgetclass.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>

#include "catalog.h"
#include "net.h"
#include "util.h"

/* For the Version command. The About box prints it, which also keeps
 * --gc-sections from discarding it as unreferenced. */
static const char version_tag[] = "$VER: SWOS-DLC 0.1 (04.11.2026)";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase;

enum {
    GID_CATEGORIES = 1,
    GID_PACKS,
    GID_AUTHOR,
    GID_VERSION,
    GID_STATUS,
    GID_INFO,
    GID_APPLIED,
    GID_REFRESH,
    GID_DOWNLOAD,
    GID_REMOVE,
    GID_APPLY,
    GID_QUIT,
    GID_STATUSLINE,
    GID_COUNT
};

enum { MID_REFRESH = 1, MID_REFRESH_ALL, MID_ABOUT, MID_QUIT };

static struct NewMenu s_menu[] = {
    {NM_TITLE, (STRPTR) "Project", 0, 0, 0, 0},
    {NM_ITEM, (STRPTR) "Refresh category", (STRPTR) "R", 0, 0, (APTR)MID_REFRESH},
    {NM_ITEM, (STRPTR) "Refresh all", (STRPTR) "A", 0, 0, (APTR)MID_REFRESH_ALL},
    {NM_ITEM, NM_BARLABEL, 0, 0, 0, 0},
    {NM_ITEM, (STRPTR) "About...", (STRPTR) "?", 0, 0, (APTR)MID_ABOUT},
    {NM_ITEM, NM_BARLABEL, 0, 0, 0, 0},
    {NM_ITEM, (STRPTR) "Quit", (STRPTR) "Q", 0, 0, (APTR)MID_QUIT},
    {NM_END, 0, 0, 0, 0, 0},
};

static struct TextAttr s_topaz8 = {(STRPTR) "topaz.font", 8, FS_NORMAL, FPF_ROMFONT};

/* --- UI state ---------------------------------------------------------- */

static struct Screen *s_screen;
static struct DrawInfo *s_dri;
static APTR s_vi;
static struct Window *s_win;
static struct Gadget *s_glist;
static struct Gadget *s_gad[GID_COUNT];
static struct Menu *s_menustrip;

static WORD s_off_x, s_off_y;                  /* window border offsets */
static WORD s_bar_x, s_bar_y, s_bar_w, s_bar_h; /* progress bar, inner coords */

static LONG s_cat = 1;    /* selected category: Pitch, as the PC manager opens */
static LONG s_pack = -1;  /* selected row in the pack list, -1 for none */

/* Text gadgets point at these, so they must outlive every GT_SetGadgetAttrs. */
static char s_txt_author[96], s_txt_version[24], s_txt_status[32];
static char s_txt_info[128], s_txt_applied[96], s_txt_statusline[128];

/* PLACEHOLDER download: progress advanced by IDCMP_INTUITICKS. */
static struct {
    BOOL active;
    LONG cat;
    char title[TITLE_LEN];
    WORD percent;
} s_dl;

/* --- Helpers ----------------------------------------------------------- */

static struct Category *cur_cat(void) { return &g_categories[s_cat]; }
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
}

static void busy(BOOL on)
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
    fill = s_dl.active ? (WORD)(((LONG)iw * s_dl.percent) / 100) : 0;
    if (fill > 0) {
        SetAPen(rp, s_dri->dri_Pens[FILLPEN]);
        RectFill(rp, x + 2, y + 1, x + 1 + fill, y + s_bar_h - 2);
    }
    if (fill < iw) {
        SetAPen(rp, s_dri->dri_Pens[BACKGROUNDPEN]);
        RectFill(rp, x + 2 + fill, y + 1, x + 1 + iw, y + s_bar_h - 2);
    }
}

/* --- Refreshing gadgets from the model --------------------------------- */

static void update_buttons(void)
{
    struct Pack *p = cur_pack();
    BOOL can_dl = p && (p->status == PACK_NEW || p->status == PACK_UPDATE);
    BOOL on_disk = p && p->status != PACK_NEW;

    set_disabled(GID_DOWNLOAD, s_dl.active || !can_dl);
    set_disabled(GID_REMOVE, s_dl.active || !on_disk);
    set_disabled(GID_APPLY, s_dl.active || !on_disk);
    set_disabled(GID_REFRESH, s_dl.active);
}

static void update_details(void)
{
    struct Pack *p = cur_pack();
    struct Category *cat = cur_cat();

    if (p) {
        str_copy(s_txt_author, p->author[0] ? p->author : "-", sizeof(s_txt_author));
        str_copy(s_txt_version, p->version[0] ? p->version : "-", sizeof(s_txt_version));
        str_copy(s_txt_status, pack_status_name(p->status), sizeof(s_txt_status));
        str_copy(s_txt_info, p->desc[0] ? p->desc : p->title, sizeof(s_txt_info));
    } else {
        s_txt_author[0] = s_txt_version[0] = s_txt_status[0] = 0;
        str_format(s_txt_info, sizeof(s_txt_info), "%ld packs, %ld new or updated%s",
                   (LONG)cat->count, (LONG)catalog_new_count(cat),
                   (ULONG)(cat->online ? "" : "  (placeholder list)"));
    }
    str_copy(s_txt_applied, cat->applied[0] ? cat->applied : "(none)", sizeof(s_txt_applied));

    set_text(GID_AUTHOR, s_txt_author);
    set_text(GID_VERSION, s_txt_version);
    set_text(GID_STATUS, s_txt_status);
    set_text(GID_INFO, s_txt_info);
    set_text(GID_APPLIED, s_txt_applied);
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

/* Re-select a pack by title after a merge rebuilt the list. */
static void reselect(const char *title)
{
    s_pack = title ? catalog_index_of(cur_cat(), title) : -1;
}

/* --- Actions ----------------------------------------------------------- */

static BOOL refresh_category(struct Category *cat)
{
    char path[96], err[80], msg[128];
    char *body;
    ULONG len;

    str_format(path, sizeof(path), DLC_BASEPATH "/%s/%s-info.csv", (ULONG)cat->id,
               (ULONG)cat->id);
    if (!net_http_get(DLC_HOST, path, &body, &len, err, sizeof(err))) {
        str_format(msg, sizeof(msg), "%s: %s", (ULONG)cat->name, (ULONG)err);
        status(msg);
        return FALSE;
    }
    if (!catalog_load_csv(cat, body)) {
        /* Every sprite_* catalogue is unpublished today (HTTP 301 to HTML). */
        str_format(msg, sizeof(msg), "%s: server sent something that is not a catalogue",
                   (ULONG)cat->name);
        status(msg);
        FreeVec(body);
        return FALSE;
    }
    FreeVec(body);
    return TRUE;
}

static void do_refresh(BOOL all)
{
    char msg[128], keep[TITLE_LEN];
    struct Pack *p = cur_pack();
    LONG i, ok = 0, tried = 0;

    /* Checked on every Refresh: the network may have come up (or gone down)
     * since the window opened. */
    net_open();
    if (!net_ready(msg, sizeof(msg))) {
        status(msg);
        return;
    }
    keep[0] = 0;
    if (p) str_copy(keep, p->title, sizeof(keep));

    busy(TRUE);
    detach_lists();
    for (i = 0; i < NUM_CATEGORIES; i++) {
        struct Category *cat = &g_categories[i];
        if (!all && i != s_cat) continue;
        str_format(msg, sizeof(msg), "Fetching %s catalogue...", (ULONG)cat->name);
        status(msg);
        tried++;
        if (refresh_category(cat)) ok++;
    }
    reselect(keep[0] ? keep : NULL);
    attach_lists(FALSE);
    busy(FALSE);
    update_details();

    if (ok == tried) {
        if (all)
            str_format(msg, sizeof(msg), "All %ld catalogues up to date (%s)", ok,
                       (ULONG)net_stack_name());
        else
            str_format(msg, sizeof(msg), "%s: %ld packs from %s", (ULONG)cur_cat()->name,
                       (LONG)cur_cat()->count, (ULONG)DLC_HOST);
        status(msg);
    } else if (all) {
        str_format(msg, sizeof(msg), "%ld of %ld catalogues fetched; the rest kept as they were",
                   ok, tried);
        status(msg);
    }
    /* A single failed refresh already left its reason in the status line. */
}

static void start_download(void)
{
    struct Pack *p = cur_pack();
    char msg[128];
    if (!p || s_dl.active) return;

    /* PLACEHOLDER: the real thing fetches DLC_BASEPATH/<type>/<name>.7z (and
     * <name>_preview.7z) with net_http_get-style streaming to a file, then
     * unpacks it. Here a timer just fills the bar. */
    s_dl.active = TRUE;
    s_dl.cat = s_cat;
    s_dl.percent = 0;
    str_copy(s_dl.title, p->title, sizeof(s_dl.title));
    str_format(msg, sizeof(msg), "Downloading %s... (simulated)", (ULONG)p->title);
    status(msg);
    update_buttons();
    draw_progress();
}

static void finish_download(BOOL cancelled)
{
    struct Category *cat = &g_categories[s_dl.cat];
    char msg[128];
    LONG idx = catalog_index_of(cat, s_dl.title);
    struct Pack *p = catalog_pack_at(cat, idx);
    char keep[TITLE_LEN];
    struct Pack *sel = cur_pack();

    s_dl.active = FALSE;
    keep[0] = 0;
    if (sel) str_copy(keep, sel->title, sizeof(keep));

    if (cancelled || !p) {
        str_format(msg, sizeof(msg), "Download of %s cancelled", (ULONG)s_dl.title);
    } else {
        detach_lists();
        catalog_mark_installed(cat, p);
        catalog_merge(cat);
        reselect(keep[0] ? keep : NULL);
        attach_lists(FALSE);
        str_format(msg, sizeof(msg), "Installed %s", (ULONG)s_dl.title);
    }
    draw_progress();
    update_details();
    status(msg);
}

static void do_remove(void)
{
    struct Pack *p = cur_pack();
    struct Category *cat = cur_cat();
    char msg[128], keep[TITLE_LEN];
    if (!p) return;

    str_copy(keep, p->title, sizeof(keep));
    detach_lists();
    catalog_remove(cat, p);  /* PLACEHOLDER: would delete <type>/<name>/ too */
    catalog_merge(cat);      /* p may be gone after this (a Removed row) */
    reselect(keep);
    attach_lists(FALSE);
    update_details();
    str_format(msg, sizeof(msg), "Removed %s", (ULONG)keep);
    status(msg);
}

static void do_apply(void)
{
    struct Pack *p = cur_pack();
    struct Category *cat = cur_cat();
    char msg[128];
    if (!p) return;

    /* PLACEHOLDER: the PC build writes "[DLC] <type>=<title>" to
     * ini/gameLauncher.ini and copies files into the game before launch. */
    str_copy(cat->applied, p->title, sizeof(cat->applied));
    update_details();
    str_format(msg, sizeof(msg), "%s will use %s", (ULONG)cat->name, (ULONG)p->title);
    status(msg);
}

static void do_about(void)
{
    struct EasyStruct es = {
        sizeof(struct EasyStruct), 0, (STRPTR) "About SWOS DLC Manager",
        (STRPTR) "SWOS United DLC Manager (demo)\n%s\n\n"
                 "Packs from http://%s\n"
                 "Network: %s\n\n"
                 "Download, Remove and Apply are placeholders.",
        (STRPTR) "OK"};
    const char *stack = net_available() ? net_stack_name() : "none (offline)";
    EasyRequest(s_win, &es, NULL, (ULONG)(version_tag + 6), (ULONG)DLC_HOST, (ULONG)stack);
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
    static const char *const btn_labels[5] = {"_Refresh", "_Download", "Re_move", "_Apply",
                                              "_Quit"};
    struct TagItem t_cats[] = {{GTLV_Labels, (ULONG)&g_category_list},
                               {GTLV_ShowSelected, 0},
                               {GTLV_Selected, 0},
                               {TAG_DONE, 0}};
    struct TagItem t_packs[] = {{GTLV_Labels, (ULONG)&g_categories[0].packs},
                                {GTLV_ShowSelected, 0},
                                {TAG_DONE, 0}};
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
             "Packs", GID_PACKS, PLACETEXT_ABOVE, t_packs);

    g = make(TEXT_KIND, g, &ng, lbl, y_rows, inner_w - lbl - m, fh + 4, "Author", GID_AUTHOR,
             PLACETEXT_LEFT, t_text);
    g = make(TEXT_KIND, g, &ng, lbl, y_rows + row_h, 96, fh + 4, "Version", GID_VERSION,
             PLACETEXT_LEFT, t_text);
    g = make(TEXT_KIND, g, &ng, lbl + 96 + 64, y_rows + row_h, 176, fh + 4, "Status",
             GID_STATUS, PLACETEXT_LEFT, t_text);
    g = make(TEXT_KIND, g, &ng, lbl, y_rows + 2 * row_h, inner_w - lbl - m, fh + 4, "Info",
             GID_INFO, PLACETEXT_LEFT, t_text);
    g = make(TEXT_KIND, g, &ng, lbl, y_rows + 3 * row_h, inner_w - lbl - m, fh + 4, "Applied",
             GID_APPLIED, PLACETEXT_LEFT, t_text);

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
        WA_ScreenTitle, (ULONG)"SWOS United DLC Manager 0.1 (demo)",
        WA_PubScreen, (ULONG)s_screen,
        WA_InnerWidth, inner_w,
        WA_InnerHeight, inner_h,
        WA_Left, (s_screen->Width - inner_w) / 2 > 0 ? (s_screen->Width - inner_w) / 2 : 0,
        WA_Top, s_screen->BarHeight + 1,
        WA_Gadgets, (ULONG)s_glist,
        WA_IDCMP, LISTVIEWIDCMP | BUTTONIDCMP | TEXTIDCMP | IDCMP_CLOSEWINDOW |
                  IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | IDCMP_VANILLAKEY |
                  IDCMP_INTUITICKS,
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
    case GID_REFRESH: do_refresh(FALSE); break;
    case GID_DOWNLOAD: start_download(); break;
    case GID_REMOVE: do_remove(); break;
    case GID_APPLY: do_apply(); break;
    case GID_QUIT: return FALSE;
    }
    return TRUE;
}

/* Keyboard equivalents of the underscored button letters. Disabled buttons
 * must stay disabled from the keyboard too. */
static BOOL handle_key(UWORD key)
{
    UWORD id = 0;
    switch (key | 0x20) {  /* lower-case */
    case 'r': id = GID_REFRESH; break;
    case 'd': id = GID_DOWNLOAD; break;
    case 'm': id = GID_REMOVE; break;
    case 'a': id = GID_APPLY; break;
    case 'q': return FALSE;
    }
    if (key == 0x1b) {  /* Esc: cancel a download, otherwise quit */
        if (s_dl.active) {
            finish_download(TRUE);
            return TRUE;
        }
        return FALSE;
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
        case MID_REFRESH: if (!s_dl.active) do_refresh(FALSE); break;
        case MID_REFRESH_ALL: if (!s_dl.active) do_refresh(TRUE); break;
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
            case IDCMP_INTUITICKS:
                /* ~10 a second while the window is active. PLACEHOLDER pacing:
                 * a real download reports bytes received instead. */
                if (s_dl.active) {
                    s_dl.percent += 4;
                    if (s_dl.percent >= 100) {
                        s_dl.percent = 100;
                        finish_download(FALSE);
                    } else {
                        draw_progress();
                    }
                }
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

    if (IntuitionBase && GfxBase && GadToolsBase) {
        catalog_init();
        net_open();  /* optional: without it the placeholder lists stay */

        if (open_window()) {
            select_category(s_cat);
            {
                char msg[128];
                if (net_ready(msg, sizeof(msg))) {
                    str_format(msg, sizeof(msg), "%s online - press Refresh",
                               (ULONG)net_stack_name());
                }
                status(msg);
            }
            event_loop();
            rc = 0;
        }
        close_window();
        net_close();
        catalog_free();
    }

    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
