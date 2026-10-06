#include "engine.h"
#include "dark-reign.h"
#include "info.h"
#include "../menu/dr_menu.h"

#include <string.h>

/* The in-game interface (dkreign.exe 00467d60): chrome bitmaps around the
 * world, the MFD pages on the right and the radar. One table holds it all;
 * the routines act on the selection and the route book. */
dr_hud_t drhud;

typedef struct {
    const char *path;
    irect_t source, destination;
} hudimage_t;

static const hudimage_t images[15] = {
    { "graphics/INTFACE/IGI/TOPBTNS.BMP", {   0, 0, 147, 32 }, {   6,   0, 147,  32 } }, /* 0 */
    { "graphics/INTFACE/IGI/TOPBITS.BMP", {   6, 0, 141, 32 }, { 153,   0, 141,  32 } }, /* 1 */
    { "graphics/INTFACE/IGI/TOPBTNS.BMP", { 147, 0, 147, 32 }, { 294,   0, 147,  32 } }, /* 2 */
    { "graphics/INTFACE/IGI/MFDBTNS.BMP", {   0, 0, 192, 64 }, { 448,   0, 192,  64 } }, /* 3 */
    { "graphics/INTFACE/IGI/MFDBAC1.BMP", {   0, 0, 192,278 }, { 448,  64, 192, 278 } }, /* 4 */
    { "graphics/INTFACE/IGI/BUBLDBIT.BMP",{   0, 0, 192, 28 }, { 448, 314, 192,  28 } }, /* 5 */
    { "graphics/INTFACE/IGI/MINIMAP.BMP", {   0, 0, 140,138 }, { 448, 342, 140, 138 } }, /* 6 */
    { "graphics/INTFACE/IGI/RESOBARS.BMP",{   0, 0,  52,104 }, { 588, 376,  52, 104 } }, /* 7 */
    { "graphics/INTFACE/IGI/BUISOBOX.BMP", {0}, {0} }, /* 8 */
    { "graphics/INTFACE/IGI/BUSCRLUP.BMP", {0}, {0} }, /* 9 */
    { "graphics/INTFACE/IGI/BUSCRLDN.BMP", {0}, {0} }, /* 10 */
    { "graphics/INTFACE/IGI/TEAMPIC.BMP", {0,0,52,34}, {588,342,52,34} }, /* 11 */
    { "graphics/INTFACE/IGI/SBTNS.BMP", {0}, {0} }, /* 12 */
    { "graphics/INTFACE/IGI/BASADV.BMP", {0}, {0} }, /* 13 */
    { "graphics/INTFACE/IGI/TRAILMDE.BMP", {0}, {0} }, /* 14 */
};

typedef enum {
    H_NONE, H_MOVE, H_ATTACK, H_STOP, H_OPTIONS, H_PAGE, H_WAYPOINT, H_CLEAR, H_DELETE,
    H_GO, H_SAVE, H_DESELECT, H_MODE, H_ADVANCED
} hudorder_t;

typedef struct {
    const char *label;
    hudorder_t order;
    irect_t rect;
    int image;
    irect_t source;
    int value;
} hudaction_t;

/* Retail MFDBTNS are pages, not infantry/vehicle categories. */
static const hudaction_t category = { "BUILD", H_PAGE, {448, 0, 64, 32}, 3, {0, 0, 64, 32}, DR_PAGE_BUILD };
static const hudaction_t actions[] = {
    { "COMMS", H_NONE, {512, 0, 64, 32}, 3, {64, 0, 64, 32}, 0 },
    { "MENU", H_OPTIONS, {576, 0, 64, 32}, 3, {128, 0, 64, 32}, 0 },
    { "ORDERS", H_NONE, {448, 32, 64, 32}, 3, {0, 32, 64, 32}, 0 },
    { "PATHS", H_PAGE, {512, 32, 64, 32}, 3, {64, 32, 64, 32}, DR_PAGE_PATHS },
    { "SPECIAL", H_NONE, {576, 32, 64, 32}, 3, {128, 32, 64, 32}, 0 },
};

/* 00467d60 constructs these controls; 00490900 selects TRAILMDE states. */
static const hudaction_t path_actions[] = {
    {"Add Waypoints", H_WAYPOINT, {502,100,103,22}, 12, {284,0,103,22}, 0},
    {"Clear All", H_CLEAR, {478,125,71,22}, 12, {0,0,71,22}, 0},
    {"Delete", H_DELETE, {559,125,71,22}, 12, {0,0,71,22}, 0},
    {"Go", H_GO, {502,150,103,22}, 12, {284,0,103,22}, 0},
    {"De-Select", H_DESELECT, {468,280,71,22}, 12, {0,0,71,22}, 0},
    {"Save Path", H_SAVE, {468,305,71,22}, 12, {0,0,71,22}, 0},
    {"One Pass", H_MODE, {508,205,24,22}, 14, {0,0,24,22}, WP_ONCE},
    {"Backtrack", H_MODE, {532,205,23,22}, 14, {24,0,23,22}, WP_BACKTRACK},
    {"Loop", H_MODE, {555,205,24,22}, 14, {47,0,24,22}, WP_LOOP},
    {"Basic", H_ADVANCED, {448,64,96,32}, 13, {0}, 0},
    {"Advanced", H_ADVANCED, {544,64,96,32}, 13, {0}, 1},
};
enum { NUMACTIONS = sizeof(actions) / sizeof(*actions),
       NUMPATHACTIONS = sizeof(path_actions) / sizeof(*path_actions) };

static const hudaction_t keys[] = {
    {.order = H_PAGE, .value = DR_PAGE_PATHS}, {.order = H_MOVE}, {.order = H_ATTACK}, {.order = H_STOP},
};
static const SDL_Keycode hotkeys[] = {SDLK_p, SDLK_m, SDLK_a, SDLK_s};

/* Native menu sprites, separate from world sprite IDs. */
const dr_menuproduct_t dr_menu_products[] = {
    /* BUILD: all buildings and Construction Rig */
    { 10001, "bfhqtmn0.spr" }, { 10002, "bfhqtmn1.spr" }, { 10003, "bfhqtmn2.spr" },
    { 10004, "bfutfmn0.spr" }, { 10005, "bfutfmn1.spr" },
    { 10006, "bfvcymn0.spr" }, { 10007, "bfvcymn1.spr" },
    { 10008, "bfhspmn0.spr" }, { 10009, "bfrepmn0.spr" },
    { 10010, "bccammn0.spr" }, { 10011, "bfrrmmn0.spr" },
    { 10012, "bfaarmn0.spr" }, { 10013, "bfgdtmn0.spr" }, { 10014, "bfagtmn0.spr" },
    { 10015, "bfphfmn0.spr" }, { 10016, "bfphfmn1.spr" },
    { 10019, "bclncmn0.spr" }, { 10020, "bcpowmn0.spr" },
    { 10040, "bcsbhmn0.spr" }, { 10041, "bcsbvmn0.spr" }, { 10042, "bcsbcmn0.spr" },
    {    11, "ucfcnmn0.spr" },
    /* COMMS: infantry */
    {  9, "ufradmn0.spr" }, { 10, "ufmrcmn0.spr" }, {  8, "ufsnpmn0.spr" },
    {  6, "ufsctmn0.spr" }, {  7, "ufmedmn0.spr" }, {  3, "ufsabmn0.spr" },
    {  2, "ufmecmn0.spr" }, {  5, "ufmtrmn0.spr" }, {  4, "ucinfmn0.spr" },
    /* Vehicles */
    {  1, "ufspbmn0.spr" }, { 15, "ufratmn0.spr" }, { 20, "ufsktmn0.spr" },
    { 17, "ufthnmn0.spr" }, { 21, "ufphtmn0.spr" }, { 12, "ufflkmn0.spr" },
    { 16, "uftrtmn0.spr" }, { 19, "uffarmn0.spr" }, { 23, "ufskbmn0.spr" },
    { 24, "ufoutmn0.spr" }, { 18, "ufswvmn0.spr" }, { 30, "ucwcomn0.spr" },
    { 13, "ucfrgmn0.spr" }, { 14, "uchfrmn0.spr" },
    /* Imperium uses its own native menu images and production IDs. */
    {11001, "bihqtmn0.spr"}, {11002, "bihqtmn1.spr"}, {11003, "bihqtmn2.spr"},
    {11004, "biutfmn0.spr"}, {11005, "biutfmn1.spr"},
    {11006, "bivcymn0.spr"}, {11007, "bivcymn1.spr"},
    {11008, "bitgtmn0.spr"}, {11009, "bihspmn0.spr"},
    {11010, "birepmn0.spr"}, {11011, "bccammn0.spr"},
    {11012, "birrmmn0.spr"}, {11013, "biaarmn0.spr"},
    {11014, "bigdtmn0.spr"}, {11015, "biagtmn0.spr"},
    {11019, "bclncmn0.spr"}, {11020, "bcpowmn0.spr"}, {11021, "bitrcmn0.spr"},
    {1005, "ucfcnmn0.spr"}, {1002, "uigrdmn0.spr"},
    {1003, "uibonmn0.spr"}, {1004, "uiextmn0.spr"}, {1001, "ucinfmn0.spr"},
    {1010, "uisttmn0.spr"}, {1009, "uiittmn0.spr"}, {1011, "uipltmn0.spr"},
    {1008, "uiampmn0.spr"}, {1013, "uimadmn0.spr"}, {1019, "uirdrmn0.spr"},
    {1015, "uishrmn0.spr"}, {1014, "uihosmn0.spr"}, {1012, "uitctmn0.spr"},
    {1017, "uiiarmn0.spr"}, {1020, "uicycmn0.spr"}, {1018, "uiskymn0.spr"},
    {1016, "ucwcomn0.spr"}, {1006, "ucfrgmn0.spr"}, {1007, "uchfrmn0.spr"},
};
const int dr_menu_product_count = sizeof(dr_menu_products) / sizeof(*dr_menu_products);

/* The product grid: three columns of five 64x50 slots. */
enum { COLUMNS = 3, ROWS = 5, SLOTS = COLUMNS * ROWS };
static const irect_t grid_rect = {448, 64, 192, 250};
static const isize2_t icon_size = {64, 50};
static const irect_t path_list = {555, 257, 80, 78};
enum { PATH_ROW = 12 };

/* Fonts: money digits, text, captions and page headers. */
enum { FONT_MONEY, FONT_TEXT, FONT_CAPTION, FONT_HEADER };

/* The table: chrome, radar and money; the paths page; the page buttons; the
 * build page; hotkeys with no control of their own. */
enum {
    CHROME, MINIMAP = CHROME + 9, END_LEFT, END_RIGHT, MONEY,
    PATHS_BACK, PATHS_TABS, BASIC, ADVANCED, DIRECTION, CURRENT, SAVED, TRAIL,
    ACTIONS, CATEGORY = ACTIONS + NUMACTIONS, PATHS, LIST = PATHS + NUMPATHACTIONS,
    GRID, ICONS, SLOT = ICONS + SLOTS, PREVIOUS = SLOT + SLOTS, NEXT, UPGRADE, DECOY,
    KEYS, NUMITEMS = KEYS + 4
};
static menuitem_t items[NUMITEMS];
static void refresh(menu_t *menu);
static void drawtip(const menu_t *menu, const menuitem_t *item);
static menu_t hud = {.items = items, .numitems = NUMITEMS, .size = {640, 480}, .stretch = true,
                     .itemOn = -1, .refresh = refresh, .drawtip = drawtip};

/* ── selection and products ─────────────────────────────────────────────── */

static mobj_t *selection(void) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *u = (mobj_t *)th;
        if (u->owner == consoleplayer && u->hp > 0 && !u->remove && P_MobjIsSelected(u)) return u;
    }
    return NULL;
}

static bool makes(const mobj_t *u, const StaticProductDefinition *p) {
    if (!u || !p) return false;
    for (int j = 0; j < p->maker_count; ++j)
        if (p->makers[j] == u->type_id) return true;
    return false;
}

static mobj_t *producer_for(const StaticProductDefinition *p) {
    mobj_t *u = selection();
    return makes(u, p) ? u : G_FindProducer(consoleplayer, p);
}

static bool enabled(const StaticProductDefinition *p, mobj_t *u) {
    if (!p || !u || !G_ModelProductAvailable(NULL, consoleplayer, p) ||
        level.player_resources[consoleplayer][0] < p->cost) return false;
    if (gameinfo->states[u->core.state_id].group == 6 || !G_ModelProducerHasTech(u, p)) return false;
    const production_t *q = u->production;
    return !q || (q->product_type == p->product_type && q->product_class == p->product_class &&
                  q->queue_count < RTS_MAX_PRODUCTION_QUEUE);
}

/* A new selection starts on the first page. */
static void update_selection(void) {
    mobj_t *u = selection();
    uint32_t id = u ? u->id : 0;
    if (drhud.production_selection == id) return;
    drhud.production_selection = id;
    drhud.production_page = 0;
}

/* Indexes into dr_menu_products of what the mission lets the selection
 * make: buildings for a construction rig, units otherwise. */
static int product_list(int *list) {
    update_selection();
    const dr_mission_t *mission = level.mission;
    mobj_t *u = selection();
    bool buildings = u && (u->type_id == MT_FG_CONSTRUCTION_CREW ||
                           u->type_id == MT_IMP_CONSTRUCTION_CREW);
    int count = 0;
    int total = mission ? mission->product_count : dr_menu_product_count;
    for (int j = 0; j < total; ++j) {
        int id = mission ? mission->products[j].type : dr_menu_products[j].id;
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, id);
        if (!product || !DR_ProductInTech(product->ui_id) ||
            (product->product_class == RTS_PRODUCT_BUILDING) != buildings) continue;
        for (int i = 0; i < dr_menu_product_count; ++i)
            if (dr_menu_products[i].id == id) { list[count++] = i; break; }
    }
    return count;
}

static int page_count(void) {
    int list[dr_menu_product_count];
    int count = product_list(list);
    return count ? (count + SLOTS - 1) / SLOTS : 1;
}

/* ── routines ───────────────────────────────────────────────────────────── */

static cell_t pointer_cell(const menu_t *menu) {
    return R_ScreenToMapGrid(menu->app, &level, menu->cursor.x, menu->cursor.y);
}

static void target(menu_t *menu, menuitem_t *item) {
    const hudaction_t *a = item->userdata;
    if (a->order == H_WAYPOINT) {
        HU_PathPoint(&drhud.paths, pointer_cell(menu));
        M_MenuTarget(menu, item);
        return;
    }
    fvec2_t goal = fvec2_cell_center(pointer_cell(menu));
    if (a->order == H_MOVE) {
        HU_SelectedOrder(TC_MOVE, goal, 0);
        return;
    }
    mobjlist_t units = P_ListMobjs();
    int picked = R_PickUnit(menu->app, &level, units.items, units.count, NULL,
                            hudview.sprites, gameinfo, menu->cursor.x, menu->cursor.y, -1);
    if (picked >= 0) HU_SelectedOrder(TC_ATTACK, goal, units.items[picked]->id);
    P_FreeMobjList(&units);
}

static void action(menu_t *menu, menuitem_t *item, menuaction_t event) {
    const hudaction_t *a = item->userdata;
    if (event == MA_TARGET) { target(menu, item); return; }
    if (event != MA_ACTIVATE) return;
    switch (a->order) {
    case H_PAGE:
        drhud.page = a->value;
        if (a->value == DR_PAGE_BUILD) drhud.production_page = 0;
        M_MenuTarget(menu, NULL);
        break;
    /* MENU opens the native options screen instead of a HUD popup. */
    case H_OPTIONS: DR_OpenOptions(menu->app); break;
    case H_STOP: HU_SelectedOrder(TC_STOP, (fvec2_t){0}, 0); break;
    case H_MOVE: case H_ATTACK: M_MenuTarget(menu, item); break;
    case H_WAYPOINT: M_MenuTarget(menu, menu->target == item ? NULL : item); break;
    case H_CLEAR: HU_PathClear(&drhud.paths); break;
    case H_DELETE: HU_PathDelete(&drhud.paths); break;
    case H_MODE: drhud.paths.path.mode = a->value; break;
    case H_ADVANCED: drhud.path_advanced = a->value != 0; break;
    case H_GO: if (HU_PathGo(&drhud.paths)) M_MenuTarget(menu, NULL); break;
    case H_SAVE: HU_PathSave(&drhud.paths); break;
    case H_DESELECT:
        HU_PathSelect(&drhud.paths, -1);
        M_MenuTarget(menu, NULL);
        break;
    default: break;
    }
}

static void turn_page(menu_t *menu, menuitem_t *item, menuaction_t event) {
    if (event != MA_ACTIVATE && event != MA_WHEEL) return;
    int pages = page_count();
    int step = event == MA_WHEEL ? (menu->wheel < 0 ? 1 : pages - 1) :
        item == &items[PREVIOUS] ? pages - 1 : 1;
    drhud.production_page = (drhud.production_page + step) % pages;
}

static void product(menu_t *menu, menuitem_t *item, menuaction_t event) {
    if (event == MA_WHEEL) { turn_page(menu, item, event); return; }
    if (event != MA_ACTIVATE) return;
    const StaticProductDefinition *p = item->userdata;
    mobj_t *producer = p ? producer_for(p) : NULL;
    if (enabled(p, producer)) G_BuildOrder(producer, p->ui_id);
}

static void select_path(menu_t *menu, menuitem_t *item, menuaction_t event) {
    if (event != MA_CHANGE) return;
    HU_PathSelect(&drhud.paths, item->value);
    M_MenuTarget(menu, NULL);
}

static const char *path_row(const menuitem_t *item, int row) {
    (void)item;
    return M_va("Trail %d", row + 1);
}

/* ── drawing ────────────────────────────────────────────────────────────── */

static irect_t placed(irect_t rect) {
    return M_MenuItemRect(&hud, &(menuitem_t){.rect = rect});
}

/* 0048f990: cell coordinates, connecting lines and centred 3x3 markers. */
static void draw_path(irect_t radar, const waypoints_t *path, int line) {
    if (!drhud.icons) return;
    const uint32_t *palette = drhud.icons[0].palette;
    for (int i = 0; i < path->count; ++i) {
        ivec2_t point = ivec2_add((ivec2_t){radar.x, radar.y}, path->points[i]);
        if (i) {
            ivec2_t previous = ivec2_add((ivec2_t){radar.x, radar.y}, path->points[i - 1]);
            irect_t from = placed((irect_t){previous.x, previous.y, 0, 0});
            irect_t to = placed((irect_t){point.x, point.y, 0, 0});
            V_DrawLine((ivec2_t){from.x, from.y}, (ivec2_t){to.x, to.y}, V_NearestIndex(palette[line]));
        }
        V_FillRect(placed((irect_t){point.x - 1, point.y - 1, 3, 3}), V_NearestIndex(palette[0x8a]));
    }
}

irect_t DR_MinimapRect(const level_t *map) {
    int w = map->width < 130 ? map->width : 130;
    int h = map->height < 127 ? map->height : 127;
    return (irect_t){453 + (130-w)/2, 348 + (127-h)/2, w, h};
}

/* One radar pixel is one cell. */
static void draw_minimap(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    const app_t *app = menu->app;
    irect_t area = item->rect;
    V_FillRect(rect, V_NearestIndex(0xff000000u));
    irect_t previous = V_GetClip();
    V_SetClip(rect);
    for (int i = 0; i < hudview.unit_count; ++i) {
        const mobj_t *u = hudview.units[i];
        if (u->remove || u->hp <= 0 || !P_VisibleToPlayer(u)) continue;
        ivec2_t cell = {u->core.position.x >> FIXED_FRAC_BITS, u->core.position.y >> FIXED_FRAC_BITS};
        uint32_t rgb = u->owner == consoleplayer ? 0xffe6a028u : 0xffc82828u;
        V_FillRect(placed((irect_t){area.x + cell.x, area.y + cell.y, 1, 1}), V_NearestIndex(rgb));
        if (P_MobjIsSelected(u) && u->owner == consoleplayer)
            draw_path(area, &u->waypoints, 0x16);
    }
    if (drhud.page == DR_PAGE_PATHS) draw_path(area, &drhud.paths.path, 0x18);
    irect_t view = placed((irect_t){area.x - (int)(app->cam.x / app->cell.w),
        area.y + (int)((32*app->win.h/480 - app->cam.y) / app->cell.h),
        G_WorldViewportWidth(app) / app->cell.w, (app->win.h - 32*app->win.h/480) / app->cell.h});
    V_DrawRectOutline(view, V_NearestIndex(0xffd7d7cdu));
    V_SetClip(previous);
}

static void tip_text(ivec2_t at, const char *text, int width) {
    const bitmapfont_t *font = &drhud.fonts[FONT_TEXT];
    irect_t clip = V_GetClip();
    V_SetClip(placed((irect_t){at.x, at.y, width, font->line_h}));
    irect_t r = placed((irect_t){at.x, at.y, 0, 0});
    V_DrawText((ivec2_t){r.x, r.y}, font, text, V_RemapPalette(font->sprite.source_palette));
    V_SetClip(clip);
}

/* A box left of the pointer: the name, and for a product its cost and why
 * it cannot be built. */
static void drawtip(const menu_t *menu, const menuitem_t *item) {
    const StaticProductDefinition *p = item->routine == product ? item->userdata : NULL;
    isize2_t screen = menu->app ? menu->app->win : menu->size;
    ivec2_t mouse = {menu->cursor.x * menu->size.w / screen.w, menu->cursor.y * menu->size.h / screen.h};
    char text[160];
    if (p) {
        const char *status = !G_ModelProductAvailable(NULL, consoleplayer, p) ? "REQUIRES TECH OR PRODUCER" :
            level.player_resources[consoleplayer][0] < p->cost ? "INSUFFICIENT FUNDS" :
            !enabled(p, producer_for(p)) ? "PRODUCER BUSY" : "CLICK TO BUILD";
        snprintf(text, sizeof(text), "%d  %s", p->cost, status);
    }
    irect_t box = {mouse.x - 260, mouse.y, 250, p ? 40 : 24};
    if (box.x < 0) box.x = 0;
    if (box.y + box.h > menu->size.h) box.y = menu->size.h - box.h;
    irect_t rect = placed(box);
    V_FillRect(rect, V_NearestIndex(0xff000000u));
    V_DrawRectOutline(rect, V_NearestIndex(0xffdcdccdu));
    tip_text((ivec2_t){box.x + 6, box.y + 7}, item->tooltip, box.w - 12);
    if (p) tip_text((ivec2_t){box.x + 6, box.y + 24}, text, box.w - 12);
}

/* Normal, hover and pressed are crops stride pixels apart. */
static void set_picture(menuitem_t *item, const spritesheet_t *sheet, irect_t source, int stride,
                        bool opaque) {
    item->sheet = sheet;
    item->stretch = true;
    item->opaque = opaque;
    for (int state = 0; state < MS_STATES; ++state) {
        item->look[state] = (menulook_t){.part = source, .palette = -1};
        item->look[state].part.x += state * stride;
    }
}

static void text(menuitem_t *item, irect_t rect, const char *label, int font) {
    *item = (menuitem_t){.rect = rect, .font = &drhud.fonts[font], .link = -1};
    snprintf(item->text, sizeof(item->text), "%s", label);
    for (int state = 0; state < MS_STATES; ++state) item->look[state] = (menulook_t){.cell = -1, .palette = -1};
}

static bool path_action_visible(const hudaction_t *a) {
    return drhud.path_advanced || (a->order != H_SAVE && a->order != H_DESELECT && a->order != H_MODE);
}

/* Show the open page and the selection's products. */
static void refresh(menu_t *menu) {
    int list[dr_menu_product_count];
    int count = product_list(list);
    int pages = count ? (count + SLOTS - 1) / SLOTS : 1;
    if (drhud.production_page >= pages) drhud.production_page = 0;
    bool build = drhud.page == DR_PAGE_BUILD, paths = drhud.page == DR_PAGE_PATHS;
    bool advanced = drhud.path_advanced;

    items[MINIMAP].rect = DR_MinimapRect(&level);
    items[MINIMAP].enabled = G_ModelRadarLevel(consoleplayer) != 0;
    char value[16];
    snprintf(value, sizeof(value), "%09d", level.player_resources[consoleplayer][0]);
    for (int i = 0; i < 8 && value[i] == '0'; ++i) value[i] = ':';
    snprintf(items[MONEY].text, sizeof(items[MONEY].text), "%s", value);

    for (int i = PATHS_BACK; i <= ADVANCED; ++i) items[i].visible = paths;
    for (int i = DIRECTION; i <= TRAIL; ++i) items[i].visible = paths && advanced;
    items[PATHS_TABS].look[MS_NORMAL].part.x = advanced ? 192 : 0;
    items[BASIC].rect = (irect_t){advanced ? 462 : 461, advanced ? 78 : 75, 71, 0};
    items[ADVANCED].rect = (irect_t){advanced ? 561 : 560, advanced ? 78 : 77, 78, 0};
    snprintf(items[TRAIL].text, sizeof(items[TRAIL].text), "%s", drhud.paths.selection >= 0 ?
             M_va("Trail %d", drhud.paths.selection + 1) : "None Selected");

    for (int i = 0; i < NUMACTIONS; ++i)
        items[ACTIONS + i].value = actions[i].order == H_PAGE && drhud.page == actions[i].value;
    items[CATEGORY].value = build;
    for (int i = 0; i < NUMPATHACTIONS; ++i) {
        const hudaction_t *a = &path_actions[i];
        menuitem_t *item = &items[PATHS + i];
        item->visible = item->enabled = paths && path_action_visible(a);
        item->value = a->order == H_WAYPOINT ? menu->target == item :
            a->order == H_MODE && (int)drhud.paths.path.mode == a->value;
    }
    menuitem_t *saved = &items[LIST];
    saved->visible = saved->enabled = paths && advanced;
    M_MenuSetRows(saved, drhud.paths.saved_count);
    saved->value = drhud.paths.selection;

    items[GRID].visible = build;
    for (int slot = 0; slot < SLOTS; ++slot) {
        int index = drhud.production_page * SLOTS + slot;
        int pindex = index < count ? list[index] : -1;
        const StaticProductDefinition *p = pindex >= 0 ?
            G_ModelProductByUIId(NULL, dr_menu_products[pindex].id) : NULL;
        irect_t rect = {grid_rect.x + slot % COLUMNS * icon_size.w,
                        grid_rect.y + slot / COLUMNS * icon_size.h, icon_size.w, icon_size.h};
        menuitem_t *icon = &items[ICONS + slot], *button = &items[SLOT + slot];
        icon->visible = build && p;
        icon->text[0] = '\0';
        if (p) {
            irect_t source = drhud.icons[pindex].cells[0].rect;
            mobj_t *producer = producer_for(p);
            icon->rect = (irect_t){rect.x + 9, rect.y + 2, source.w, source.h};
            set_picture(icon, &drhud.icons[pindex], source, 0, false);
            for (int state = 0; state < MS_STATES; ++state)
                icon->look[state].palette = enabled(p, producer) ? -1 : 1;
            const production_t *q = producer ? producer->production : NULL;
            if (q && q->product_type == p->product_type && q->product_class == p->product_class)
                snprintf(icon->text, sizeof(icon->text), "%d", q->queue_count);
        }
        set_picture(button, &drhud.images[8], (irect_t){0, 0, 64, 50}, 64, false);
        button->look[MS_PUSHED] = button->look[MS_FOCUS];
        if (!p) button->look[MS_FOCUS] = button->look[MS_NORMAL];
        button->rect = rect;
        button->userdata = p;
        button->tooltip = p ? p->label : NULL;
        button->visible = button->enabled = build;
    }
    for (int i = PREVIOUS; i <= DECOY; ++i) items[i].visible = build;
}

/* ── lifecycle ──────────────────────────────────────────────────────────── */

/* PCX strips under a marker row; 00477e00's normal and header tables add
 * 2*8 and 7*8 to indices 32..41 of the TOPBITS palette. */
static bool load_font(const char *root, const char *name, int translation, bitmapfont_t *font) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/graphics/INTFACE/IGI/%s", root, name);
    spritesheet_t strip = {0}, chrome = {0};
    if (!W_LoadIndexedSheet(path, &strip)) return false;
    DR_StripFont(&strip, 1, font);
    R_FreeSprite(&strip);
    M_PathJoin(path, sizeof(path), root, "graphics/INTFACE/IGI/TOPBITS.BMP");
    if (!W_LoadIndexedSheet(path, &chrome)) return false;
    uint32_t *palette = font->sprite.source_palette;
    memcpy(palette, chrome.source_palette, 256 * sizeof(uint32_t));
    memcpy(palette + 32, chrome.source_palette + 32 + translation * 8, 10 * sizeof(uint32_t));
    R_FreeSprite(&chrome);
    font->own_palette = true;
    return font->glyph_index['0'] >= 0;
}

static void add_action(menuitem_t *item, const hudaction_t *a) {
    *item = (menuitem_t){.kind = MI_CHECK, .rect = a->rect, .visible = true, .enabled = true,
                         .routine = action, .userdata = a, .tooltip = a->label, .link = -1};
    set_picture(item, &drhud.images[a->image], a->source, 192, true);
}

static bool build_table(void) {
    for (int i = 0; i < NUMITEMS; ++i)
        items[i] = (menuitem_t){.link = -1, .look = {{.cell = -1}, {.cell = -1}, {.cell = -1}}};
    int chrome = CHROME;
    for (int i = 0; i < 15; ++i) {
        if (images[i].destination.w <= 0) continue;
        menuitem_t *item = &items[chrome++];
        *item = (menuitem_t){.rect = images[i].destination, .visible = true, .link = -1};
        set_picture(item, &drhud.images[i], images[i].source, 0, true);
    }
    items[MINIMAP] = (menuitem_t){.kind = MI_MINIMAP, .visible = true, .link = -1,
                                  .ownerdraw = draw_minimap};
    items[END_LEFT] = (menuitem_t){.rect = {0, 0, 6, 32}, .visible = true, .link = -1};
    set_picture(&items[END_LEFT], &drhud.images[1], (irect_t){0, 0, 6, 32}, 0, true);
    items[END_RIGHT] = (menuitem_t){.rect = {441, 0, 7, 32}, .visible = true, .link = -1};
    set_picture(&items[END_RIGHT], &drhud.images[1], (irect_t){147, 0, 7, 32}, 0, true);
    text(&items[MONEY], (irect_t){178, 6, 0, 0}, "", FONT_MONEY);
    items[MONEY].visible = true;

    items[PATHS_BACK] = (menuitem_t){.rect = {448, 64, 192, 278}, .link = -1};
    set_picture(&items[PATHS_BACK], &drhud.images[4], (irect_t){0, 0, 192, 278}, 0, true);
    items[PATHS_TABS] = (menuitem_t){.rect = {448, 64, 192, 32}, .link = -1};
    set_picture(&items[PATHS_TABS], &drhud.images[13], (irect_t){0, 0, 192, 32}, 0, true);
    text(&items[BASIC], (irect_t){0}, "Basic", FONT_HEADER);
    text(&items[ADVANCED], (irect_t){0}, "Advanced", FONT_HEADER);
    /* Captions stand on their anchor, two pixels above it. */
    int above = drhud.fonts[FONT_CAPTION].glyph_size.h + 2;
    text(&items[DIRECTION], (irect_t){544, 205 - above, 0, 0}, "Path Direction", FONT_CAPTION);
    items[DIRECTION].align = MALIGN_HCENTER;
    text(&items[CURRENT], (irect_t){468, 258 - above, 0, 0}, "Current Path", FONT_CAPTION);
    text(&items[SAVED], (irect_t){555, 257 - above, 0, 0}, "Saved Paths", FONT_CAPTION);
    text(&items[TRAIL], (irect_t){470, 260, 76, 0}, "", FONT_TEXT);

    for (int i = 0; i < NUMACTIONS; ++i) add_action(&items[ACTIONS + i], &actions[i]);
    add_action(&items[CATEGORY], &category);
    items[CATEGORY].hotkey = SDLK_b;
    for (int i = 0; i < NUMPATHACTIONS; ++i) {
        const hudaction_t *a = &path_actions[i];
        menuitem_t *item = &items[PATHS + i];
        add_action(item, a);
        item->tooltip = NULL;
        item->id = a->order == H_WAYPOINT ? DR_HUD_WAYPOINT : 0;
        if (a->order == H_ADVANCED) {
            item->sheet = NULL;
            continue;
        }
        set_picture(item, &drhud.images[a->image], a->source, a->image == 14 ? 71 : a->source.w, true);
        if (a->image != 14) {
            item->font = &drhud.fonts[FONT_TEXT];
            item->inset = (ivec2_t){a->rect.w == 71 ? 7 : 9, 5};
            snprintf(item->text, sizeof(item->text), "%s", a->label);
        }
    }
    /* Labels use the native variable-width font; selection and scrolling
     * are the engine's. */
    items[LIST] = (menuitem_t){.kind = MI_LIST, .rect = path_list, .row_height = PATH_ROW,
        .value = -1, .link = -1, .row = path_row, .routine = select_path,
        .font = &drhud.fonts[FONT_TEXT], .inset = {2, 0}};
    items[GRID] = (menuitem_t){.rect = grid_rect, .fill = 0xff000000u, .link = -1};
    for (int i = 0; i < SLOTS; ++i) {
        items[ICONS + i] = (menuitem_t){.font = &drhud.fonts[FONT_TEXT], .inset = {-5, 2}, .link = -1};
        items[SLOT + i] = (menuitem_t){.kind = MI_BUTTON, .routine = product, .link = -1};
    }
    items[PREVIOUS] = (menuitem_t){.kind = MI_BUTTON, .rect = {448, 316, 22, 22}, .enabled = true,
                                   .routine = turn_page, .link = -1};
    set_picture(&items[PREVIOUS], &drhud.images[9], (irect_t){0, 0, 22, 22}, 0, true);
    items[NEXT] = (menuitem_t){.kind = MI_BUTTON, .rect = {470, 316, 22, 22}, .enabled = true,
                               .routine = turn_page, .link = -1};
    set_picture(&items[NEXT], &drhud.images[10], (irect_t){0, 0, 22, 22}, 0, true);
    text(&items[UPGRADE], (irect_t){496, 316, 71, 22}, "Upgrade", FONT_TEXT);
    items[UPGRADE].inset = (ivec2_t){11, 7};
    set_picture(&items[UPGRADE], &drhud.images[12], (irect_t){213, 0, 71, 22}, 0, true);
    text(&items[DECOY], (irect_t){568, 316, 71, 22}, "Decoy", FONT_TEXT);
    items[DECOY].inset = (ivec2_t){18, 7};
    set_picture(&items[DECOY], &drhud.images[12], (irect_t){0, 0, 71, 22}, 0, true);
    for (int i = 0; i < 4; ++i)
        items[KEYS + i] = (menuitem_t){.kind = MI_BUTTON, .enabled = true, .hotkey = hotkeys[i],
            .routine = action, .userdata = &keys[i], .link = -1,
            .id = keys[i].order == H_MOVE ? DR_HUD_MOVE : keys[i].order == H_ATTACK ? DR_HUD_ATTACK : 0};
    return true;
}

menu_t *G_InitHUD(app_t *app, const char *root) {
    G_ShutdownHUD();
    bool ok = true;
    for (int i = 0; ok && i < 15; ++i) {
        char path[1024];
        M_PathJoin(path, sizeof(path), root, images[i].path);
        ok = W_LoadIndexedSheet(path, &drhud.images[i]);
        if (!ok) fprintf(stderr, "warning: failed to load UI asset %s\n", path);
    }
    drhud.icons = ok ? calloc(dr_menu_product_count, sizeof(*drhud.icons)) : NULL;
    for (int i = 0; drhud.icons && ok && i < dr_menu_product_count; ++i) {
        ok = DR_LoadMenuSprite(root, dr_menu_products[i].image, &drhud.icons[i]);
        if (!ok) fprintf(stderr, "failed to load menu image %s\n", dr_menu_products[i].image);
    }
    ok = ok && drhud.icons && load_font(root, "FONT16.PCX", 2, &drhud.fonts[FONT_MONEY]) &&
         load_font(root, "FONT12T.PCX", 2, &drhud.fonts[FONT_TEXT]) &&
         load_font(root, "FONT12W.PCX", 2, &drhud.fonts[FONT_CAPTION]) &&
         load_font(root, "FONT12T.PCX", 7, &drhud.fonts[FONT_HEADER]) && build_table();
    if (!ok) {
        G_ShutdownHUD();
        return NULL;
    }
    HU_PathReset(&drhud.paths);
    hud.app = app;
    hud.itemOn = -1;
    hud.held = hud.target = hud.editing = NULL;
    return &hud;
}

void G_ShutdownHUD(void) {
    hudview = (hudview_t){0};
    for (int i = 0; i < 15; ++i) R_FreeSprite(&drhud.images[i]);
    if (drhud.icons)
        for (int i = 0; i < dr_menu_product_count; ++i) R_FreeSprite(&drhud.icons[i]);
    free(drhud.icons);
    for (int i = 0; i < 4; ++i) HU_FreeFont(&drhud.fonts[i]);
    memset(&drhud, 0, sizeof(drhud));
    hud.app = NULL;
}
