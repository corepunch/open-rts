#include "engine.h"
#include "info.h"
#include "rustfront.h"

#include <stdio.h>
#include <string.h>

#include "sprites.inc"

/* The sidebar, after C&C's: credits, a radar, and the selected producer's build list as
 * cameo rows (art/icons.py), laid out in 640 x 480 like every open-rts HUD. Clicking a row,
 * queueing, placing a structure and paging use the engine's product pieces (hud/hu_bar.c);
 * this file draws the rows and the radar. */

enum { SB_PANEL, SB_CREDITS, SB_RADAR, SB_PRODUCTS, SB_PAGE, SB_COUNT };
enum { ROW_H = 52, SIDEBAR_W = 640 - RF_SIDEBAR_X, PRODUCT_MAX = 64 };

static spritesheet_t icons;
static bool icons_loaded;

/* ── build rows ────────────────────────────────────────────────────────── */

static mobj_t *selected_producer(void) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (unit->owner == consoleplayer && unit->hp > 0 && !unit->remove && P_MobjIsSelected(unit))
            return unit;
    }
    return NULL;
}

/* The products the producer makes, in catalog order: the same rows HU_ProductList counts. */
static int products_of(const mobj_t *producer, StaticProductDefinition out[PRODUCT_MAX]) {
    if (!producer) return 0;
    int count = G_ModelGetProducts(NULL, consoleplayer, out, PRODUCT_MAX), n = 0;
    for (int i = 0; i < count; ++i)
        for (int j = 0; j < out[i].maker_count; ++j)
            if (out[i].makers[j] == producer->type_id) { out[n++] = out[i]; break; }
    return n;
}

static bool product_enabled(const mobj_t *producer, const StaticProductDefinition *p) {
    const production_t *queue = producer->production;
    return G_ModelProductAvailable(NULL, consoleplayer, p) && R_CanAfford(consoleplayer, p) &&
        (!queue || queue->queue_count == 0 ||
         (queue->product_type == p->product_type && queue->product_class == p->product_class &&
          queue->queue_count < RTS_MAX_PRODUCTION_QUEUE));
}

/* Icons are drawn in the blue ramp; other teams see theirs, as their units do. */
static const uint8_t *team_remap(int team) {
    static uint8_t map[256];
    if (team <= 0 || team >= RF_TEAM_COUNT) return NULL;
    for (int i = 0; i < 256; ++i) map[i] = (uint8_t)i;
    for (int i = 0; i < RF_TEAM_SHADES; ++i)
        map[RF_TEAM_BASE + i] = (uint8_t)(RF_TEAM_BASE + team * RF_TEAM_SHADES + i);
    return map;
}

static void text(irect_t box, const char *s, uint32_t argb) {
    V_DrawSmallText(box, s, argb, V_DrawSize());
}

static void draw_products(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu;
    StaticProductDefinition products[PRODUCT_MAX];
    mobj_t *producer = selected_producer();
    int count = products_of(producer, products);
    int s = rect.w / SIDEBAR_W > 0 ? rect.w / SIDEBAR_W : 1; /* framebuffer pixels per layout pixel */
    if (!count) {
        text((irect_t){rect.x + 6 * s, rect.y + 8 * s, rect.w - 12 * s, 7 * s},
             producer ? "NOTHING TO BUILD HERE" : "SELECT A STRUCTURE", 0xffc8d2b4u);
        return;
    }
    uint8_t border = V_NearestIndex(0xff3c4a50u);
    const uint8_t *remap = team_remap(consoleplayer < MAXPLAYERS ? consoleplayer : 0);
    int rows = item->row_height > 0 ? rect.h / (item->row_height * s) : 1;
    for (int row = 0; row < rows; ++row) {
        int index = item->first_row + row;
        if (index >= count) break;
        const StaticProductDefinition *p = &products[index];
        irect_t cell = {rect.x, rect.y + row * item->row_height * s, rect.w, (item->row_height - 2) * s};
        irect_t icon = {cell.x + 3 * s, cell.y + 1 * s, RF_ICON_W * s, RF_ICON_H * s};
        V_DrawRectOutline(cell, border);
        if (icons_loaded && p->icon_frame >= 0) {
            irect_t src = {p->icon_frame * RF_ICON_W, 0, RF_ICON_W, RF_ICON_H};
            V_DrawSpriteCellScaled(icon, &icons, 0, &src, remap, V_OPAQUE);
        }
        bool enabled = product_enabled(producer, p);
        const production_t *queue = producer->production;
        bool building = queue && queue->queue_count > 0 && queue->product_type == p->product_type &&
                        queue->product_class == p->product_class;
        if (!enabled && !building) V_FillRectTranslucent(icon, 0xa0000000u);
        if (building && queue->time_ms > 0) {
            /* C&C's clock as a curtain: the part still to build stays dark. */
            int left = queue->time_left_ms > 0 ? queue->time_left_ms : 0;
            int h = icon.h * left / queue->time_ms;
            V_FillRectTranslucent((irect_t){icon.x, icon.y, icon.w, h}, 0x90000000u);
        }
        uint32_t ink = enabled || building ? 0xffdce6dcu : 0xff707070u;
        int tx = icon.x + icon.w + 4 * s, tw = cell.x + cell.w - tx - 3 * s;
        text((irect_t){tx, cell.y + 5 * s, tw, 7 * s}, p->label, ink);
        char line[32];
        snprintf(line, sizeof(line), "%d", p->cost);
        text((irect_t){tx, cell.y + 18 * s, tw, 7 * s}, line, 0xffe6d750u);
        if (building) {
            if (p->product_class == RTS_PRODUCT_BUILDING && queue->time_left_ms <= 0)
                snprintf(line, sizeof(line), "PLACING");
            else if (queue->queue_count > 1)
                snprintf(line, sizeof(line), "BUILDING +%d", queue->queue_count - 1);
            else
                snprintf(line, sizeof(line), "BUILDING");
            text((irect_t){tx, cell.y + 31 * s, tw, 7 * s}, line, 0xff8fd0ffu);
        }
    }
}

/* ── radar ─────────────────────────────────────────────────────────────── */

static const uint32_t TEAM_ARGB[RF_TEAM_COUNT] = {0xff3c78ebu, 0xffe1322au, 0xff46af3cu, 0xffebc328u};

static void draw_radar(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)item;
    const tileset_t *tiles = hudview.tileset;
    if (!menu->app || level.width <= 0 || level.height <= 0 || rect.w <= 0 || rect.h <= 0) return;
    /* Keep the map's aspect: cells are 48 x 32 on screen. */
    int mw = level.width * 3, mh = level.height * 2;
    int w = rect.w, h = rect.w * mh / mw;
    if (h > rect.h) { h = rect.h; w = rect.h * mw / mh; }
    irect_t map = {rect.x + (rect.w - w) / 2, rect.y + (rect.h - h) / 2, w, h};
    V_FillRect(rect, V_NearestIndex(0xff0a0c0du));
    irect_t clip = V_GetClip();
    V_SetClip(map);
    for (int py = 0; py < map.h; ++py)
        for (int px = 0; px < map.w; ++px) {
            int gx = px * level.width / map.w, gy = py * level.height / map.h;
            uint8_t colour = 0;
            int tile = level.tile_ids[L_Index(&level, gx, gy)];
            if (tiles && tiles->indices && tile >= 0 && tile < tiles->count)
                colour = tiles->indices[((size_t)tile * tiles->tile_h + tiles->tile_h / 2) * tiles->tile_w +
                                        tiles->tile_w / 2];
            V_DrawPoint((ivec2_t){map.x + px, map.y + py}, colour);
        }
    for (int i = 0; i < hudview.unit_count; ++i) {
        const mobj_t *mo = hudview.units[i];
        if (!mo || mo->remove || mo->hp <= 0) continue;
        int team = mo->team >= 0 && mo->team < RF_TEAM_COUNT ? mo->team : 0;
        uint8_t colour = V_NearestIndex(TEAM_ARGB[team]);
        irect_t cells = mo->info && mo->info->footprint.w > 0 && !(mo->traits & MF_MOBILE) ?
            P_MobjCells(mo) : (irect_t){mo->core.position.x >> FIXED_FRAC_BITS, mo->core.position.y >> FIXED_FRAC_BITS, 1, 1};
        irect_t dot = {map.x + cells.x * map.w / level.width, map.y + cells.y * map.h / level.height,
                       cells.w * map.w / level.width, cells.h * map.h / level.height};
        if (dot.w < 2) dot.w = 2;
        if (dot.h < 2) dot.h = 2;
        V_FillRect(dot, colour);
    }
    /* The world view's outline. */
    irect_t view = G_WorldViewport(menu->app);
    ivec2_t a = R_ScreenToMapGrid(menu->app, &level, view.x, view.y);
    ivec2_t b = R_ScreenToMapGrid(menu->app, &level, view.x + view.w, view.y + view.h);
    a.x = a.x < 0 ? 0 : a.x; a.y = a.y < 0 ? 0 : a.y;
    b.x = b.x > level.width ? level.width : b.x; b.y = b.y > level.height ? level.height : b.y;
    V_DrawRectOutline((irect_t){map.x + a.x * map.w / level.width, map.y + a.y * map.h / level.height,
                                (b.x - a.x) * map.w / level.width, (b.y - a.y) * map.h / level.height},
                      V_NearestIndex(0xffffffffu));
    V_SetClip(clip);
}

/* ── the sidebar ───────────────────────────────────────────────────────── */

static menuitem_t items[SB_COUNT] = {
    [SB_PANEL] = {.visible = true, .rect = {RF_SIDEBAR_X, 0, SIDEBAR_W, 480},
                  .fill = 0xff161a1cu, .border = 0xff4a5458u},
    [SB_CREDITS] = {.visible = true, .rect = {RF_SIDEBAR_X + 8, 6, SIDEBAR_W - 16, 22}, .ink = 0xffe6d750u,
                    .align = MALIGN_RIGHT, .ownerdraw = HU_DrawCounter},
    [SB_RADAR] = {.kind = MI_MINIMAP, .visible = true, .enabled = true,
                  .rect = {RF_SIDEBAR_X + 6, 34, SIDEBAR_W - 12, 104}, .ownerdraw = draw_radar},
    [SB_PRODUCTS] = {.kind = MI_LIST, .visible = true, .enabled = true,
                     .rect = {RF_SIDEBAR_X + 4, 144, SIDEBAR_W - 8, 6 * ROW_H}, .row_height = ROW_H, .value = -1,
                     .routine = HU_ProductList, .ownerdraw = draw_products, .drawtarget = HU_DrawProductPlacement},
    [SB_PAGE] = {.kind = MI_BUTTON, .visible = true, .enabled = true,
                 .rect = {RF_SIDEBAR_X + 4, 144 + 6 * ROW_H + 4, SIDEBAR_W - 8, 22},
                 .fill = 0xff20282bu, .border = 0xff4a5458u, .link = SB_PRODUCTS,
                 .routine = HU_ProductPage, .ownerdraw = HU_DrawProductPage},
};
static menu_t sidebar = {.items = items, .numitems = SB_COUNT, .itemOn = -1, .refresh = HU_RefreshProducts};

menu_t *RF_InitHUD(app_t *app, const char *root) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/assets/icons.pcx", root ? root : g_game_default_root);
    if (!icons_loaded) {
        icons_loaded = W_LoadIndexedSheet(path, &icons);
        if (!icons_loaded) fprintf(stderr, "rustfront: cannot load %s; the build list has no icons\n", path);
    }
    HU_InitProducts(&sidebar, root);
    sidebar.app = app;
    return &sidebar;
}

void RF_ShutdownHUD(void) {
    hudview = (hudview_t){0};
    if (icons_loaded) R_FreeSprite(&icons);
    icons_loaded = false;
}
