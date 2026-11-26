#include "engine.h"
#include "info.h"
#include "rustfront.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sprites.inc"

/* The sidebar, after C&C's: credits, a radar, and the selected producer's build list as
 * cameo rows (art/icons.py), laid out in 640 x 480 like every open-rts HUD. Clicking a row,
 * queueing, placing a structure and paging use the engine's product pieces (hud/hu_bar.c);
 * this file draws the rows and the radar. */

enum { SB_PANEL, SB_CREDITS, SB_POWER, SB_RADAR, SB_PRODUCTS, SB_REPAIR, SB_SELL, SB_PAGE,
       SB_CMD0, SB_CMD1, SB_CMD2, SB_COUNT };
enum { ROW_H = 52, ROWS = 5, SIDEBAR_W = 640 - RF_SIDEBAR_X, PRODUCT_MAX = 64 };
enum { BUTTONS_Y = 144 + ROWS * ROW_H + 4, COMMANDS_Y = BUTTONS_Y + 26, COMMAND_W = (SIDEBAR_W - 12) / 3 };

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

/* A structure is built on its own site, one at a time: the queue that shows its progress is
 * the site's. */
static const production_t *queue_for(const mobj_t *producer, const StaticProductDefinition *p) {
    if (p->product_class != RTS_PRODUCT_BUILDING) return producer->production;
    const mobj_t *site = RF_SiteInProgress(consoleplayer);
    return site && site->type_id == p->product_type ? site->production : NULL;
}

static bool product_enabled(const mobj_t *producer, const StaticProductDefinition *p) {
    const production_t *queue = producer->production;
    if (p->product_class == RTS_PRODUCT_BUILDING && RF_SiteInProgress(consoleplayer)) return false;
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
        const production_t *queue = queue_for(producer, p);
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
            if (queue->queue_count > 1)
                snprintf(line, sizeof(line), "BUILDING +%d", queue->queue_count - 1);
            else
                snprintf(line, sizeof(line), "BUILDING");
            text((irect_t){tx, cell.y + 31 * s, tw, 7 * s}, line, 0xff8fd0ffu);
        }
    }
}

/* ── radar ─────────────────────────────────────────────────────────────── */

static const uint32_t TEAM_ARGB[RF_TEAM_COUNT] = {0xff3c78ebu, 0xffe1322au, 0xff46af3cu, 0xffebc328u};

static void radar_dot(irect_t map, irect_t cells, int team) {
    if (team < 0 || team >= RF_TEAM_COUNT) team = 0;
    irect_t dot = {map.x + cells.x * map.w / level.width, map.y + cells.y * map.h / level.height,
                   cells.w * map.w / level.width, cells.h * map.h / level.height};
    if (dot.w < 2) dot.w = 2;
    if (dot.h < 2) dot.h = 2;
    V_FillRect(dot, V_NearestIndex(TEAM_ARGB[team]));
}

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
    /* The fog: black where nothing of yours has been, dimmed where nothing sees now. */
    R_DrawMinimapFog(&level, map);
    /* Remembered structures in the fog, then what is in sight now. */
    const sightmemory_t *memory = NULL;
    int remembered = P_SightMemory(&memory);
    for (int i = 0; i < remembered; ++i) {
        const mobjtype_t *type = P_ActorType(memory[i].type_id);
        if (!type) continue;
        ivec2_t corner = fixed2_footprint_corner(fixed3_xy(memory[i].core.position), type->footprint);
        radar_dot(map, (irect_t){corner.x, corner.y, type->footprint.w, type->footprint.h}, memory[i].team);
    }
    for (int i = 0; i < hudview.unit_count; ++i) {
        const mobj_t *mo = hudview.units[i];
        if (!mo || mo->remove || mo->hp <= 0 || mo->type_id >= MT_FX_HIT || !P_VisibleToPlayer(mo)) continue;
        irect_t cells = mo->info && mo->info->footprint.w > 0 && !(mo->traits & MF_MOBILE) ?
            P_MobjCells(mo) : (irect_t){mo->core.position.x >> FIXED_FRAC_BITS, mo->core.position.y >> FIXED_FRAC_BITS, 1, 1};
        radar_dot(map, cells, mo->team);
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

/* ── power, repair and sell ────────────────────────────────────────────── */

/* Power drawn against power made, as "used/made" over a bar whose tick is the supply: green with room to spare, yellow near the limit, red when
 * low (production at half speed, defences at half rate; g_base.c). */
static void draw_power(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    int made, used;
    RF_OwnerPower(consoleplayer, &made, &used);
    int s = rect.w / 64 > 0 ? rect.w / 64 : 1;
    uint32_t ink = used > made ? 0xffe1322au : used * 10 > made * 8 ? 0xffebc328u : 0xff46c83cu;
    char line[24];
    snprintf(line, sizeof(line), "%d/%d", used, made);
    text((irect_t){rect.x, rect.y, rect.w, 7 * s}, line, ink);
    irect_t bar = {rect.x, rect.y + 10 * s, rect.w, 4 * s};
    V_FillRect(bar, V_NearestIndex(0xff0a0c0du));
    int scale = made > used ? made : used;
    if (scale > 0) {
        V_FillRect((irect_t){bar.x, bar.y, (int)((int64_t)bar.w * used / scale), bar.h}, V_NearestIndex(ink));
        int mark = (int)((int64_t)bar.w * made / scale);
        V_FillRect((irect_t){bar.x + mark - s, bar.y - s, s, bar.h + 2 * s}, V_NearestIndex(0xffdce6dcu));
    }
}

static bool structure_selected(bool damaged) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (th->function == P_MobjThinker && unit->owner == consoleplayer && unit->hp > 0 && !unit->remove &&
            P_MobjIsSelected(unit) && RF_IsStructure(unit) && (!damaged || unit->hp < unit->max_hp))
            return true;
    }
    return false;
}

static void structure_button(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (action == MA_ACTIVATE) RF_StructureOrder(item->value);
}

static void draw_structure_button(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu;
    int s = rect.w / 50 > 0 ? rect.w / 50 : 1;
    bool on = structure_selected(item->value == RF_ORDER_REPAIR);
    text((irect_t){rect.x + 4 * s, rect.y + (rect.h - 7 * s) / 2, rect.w - 8 * s, 7 * s},
         item->value == RF_ORDER_SELL ? "SELL" : "REPAIR", on ? 0xffdce6dcu : 0xff5a6064u);
}

/* The page button is narrow beside them: "PAGE 1/2". */
static void draw_page(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    const menuitem_t *list = item->link >= 0 && item->link < menu->numitems ? &menu->items[item->link] : NULL;
    if (!list || list->row_height <= 0) return;
    int rows = list->rect.h / list->row_height;
    if (rows <= 0 || list->rows <= rows) return;
    int s = rect.w / 50 > 0 ? rect.w / 50 : 1;
    char line[24];
    snprintf(line, sizeof(line), "PG %d/%d", list->first_row / rows + 1, (list->rows + rows - 1) / rows);
    text((irect_t){rect.x + 3 * s, rect.y + (rect.h - 7 * s) / 2, rect.w - 6 * s, 7 * s}, line, 0xffc8d2b4u);
}

/* ── unit commands and the Rites ─────────────────────────────────────────────
 * Three buttons under the build list (Z, C, V) hold what the selection can do (g_units.c):
 * deploy a Sentry Truck or Drill Rig, pack a Sentry Post up, load soldiers into the nearest APC
 * with room, unload an APC. With none of those selected, a Concord player sees its hand of
 * Rites there; a card played on a point waits for a click on the map. */
typedef enum { CMD_NONE, CMD_DEPLOY, CMD_PACK, CMD_LOAD, CMD_UNLOAD, CMD_RITE } cmdkind_t;
typedef struct { cmdkind_t kind; int slot; } command_t;

static bool own_selected(const mobj_t *u) {
    return u->owner == consoleplayer && u->hp > 0 && !u->remove && P_MobjIsSelected(u);
}

/* What the buttons hold now, in a fixed order; their count. */
static int commands(command_t out[3]) {
    bool has[CMD_RITE] = {0};
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function != P_MobjThinker || !own_selected(u)) continue;
        if (u->type_id == MT_SENTRY_TRUCK || u->type_id == MT_CL_DRILL) has[CMD_DEPLOY] = true;
        if (u->type_id == MT_SENTRY) has[CMD_PACK] = true;
        if (RF_IsInfantry(u->type_id) && !u->rf_boarded) has[CMD_LOAD] = true;
        if (RF_IsTransport(u) && RF_Passengers(u, NULL, 0) > 0) has[CMD_UNLOAD] = true;
    }
    int n = 0;
    for (int k = CMD_DEPLOY; k < CMD_RITE && n < 3; ++k)
        if (has[k]) out[n++] = (command_t){(cmdkind_t)k, 0};
    if (n == 0 && consoleplayer < MAXPLAYERS && RF_NextDrawMs(consoleplayer) >= 0)
        for (int i = 0; i < RF_HAND; ++i) out[n++] = (command_t){CMD_RITE, i};
    return n;
}

/* The nearest APC of the player's with a seat, to the soldiers selected. */
static const mobj_t *nearest_transport(void) {
    int64_t sx = 0, sy = 0, n = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function != P_MobjThinker || !own_selected(u) || !RF_IsInfantry(u->type_id)) continue;
        sx += u->core.position.x; sy += u->core.position.y; ++n;
    }
    if (!n) return NULL;
    fixed2_t at = {(fixed_t)(sx / n), (fixed_t)(sy / n)};
    const mobj_t *best = NULL;
    int64_t best_d = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function != P_MobjThinker || u->owner != consoleplayer || u->hp <= 0 || u->remove ||
            !RF_IsTransport(u) || RF_Passengers(u, NULL, 0) >= RF_APC_SEATS) continue;
        int64_t d = fixed2_distance_squared64(fixed3_xy(u->core.position), at);
        if (!best || d < best_d) { best = u; best_d = d; }
    }
    return best;
}

static fixed2_t cursor_ground(const menu_t *menu) {
    ivec2_t cell = R_ScreenToMapGrid(menu->app, &level, menu->cursor.x, menu->cursor.y);
    return fixed2_cell_center(cell);
}

static void command_button(menu_t *menu, menuitem_t *item, menuaction_t action) {
    command_t list[3];
    int n = commands(list);
    static int pending_slot = -1;
    if (action == MA_TARGET) {
        if (pending_slot >= 0) RF_RiteOrder(pending_slot, cursor_ground(menu));
        pending_slot = -1;
        return;
    }
    if (action == MA_CANCEL) { pending_slot = -1; return; }
    if (action != MA_ACTIVATE || item->value >= n) return;
    command_t c = list[item->value];
    switch (c.kind) {
    case CMD_DEPLOY: case CMD_PACK: HU_SelectedOrder(TC_DEPLOY, (fvec2_t){0}, 0); break;
    case CMD_UNLOAD: HU_SelectedOrder(TC_UNLOAD, (fvec2_t){0}, 0); break;
    case CMD_LOAD: {
        const mobj_t *apc = nearest_transport();
        if (apc) HU_SelectedOrder(TC_BOARD, fixed3_xy_to_fvec2(apc->core.position), apc->id);
        break;
    }
    case CMD_RITE: {
        rf_rite_t rite = RF_HandCard(consoleplayer, c.slot);
        if (!rite) break;
        if (!RF_RiteTargeted(rite)) { RF_RiteOrder(c.slot, (fixed2_t){0}); break; }
        pending_slot = c.slot;
        M_MenuTarget(menu, item);
        break;
    }
    default: break;
    }
}

/* While a card waits for its point: the ring it will cover, under the cursor. */
static void draw_rite_target(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)rect;
    command_t list[3];
    int n = commands(list);
    if (item->value >= n || list[item->value].kind != CMD_RITE) return;
    fixed_t r = RF_RiteRadius(RF_HandCard(consoleplayer, list[item->value].slot));
    if (!r) return;
    fixed2_t at = cursor_ground(menu);
    uint8_t ink = V_NearestIndex(0xff7ff0ffu);
    ivec2_t prev = {0, 0};
    for (int k = 0; k <= 32; ++k) {
        float a = (float)k * 6.2831853f / 32.0f;
        fixed2_t p = {at.x + (fixed_t)(r * cosf(a)), at.y + (fixed_t)(r * sinf(a))};
        float sx, sy;
        R_MapPositionToScreen(menu->app, &level, fixed3_from_fixed2(p, 0), &sx, &sy);
        ivec2_t q = {(int)sx, (int)sy};
        if (k) V_DrawLine(prev, q, ink);
        prev = q;
    }
}

static void draw_command(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu;
    static const char *const LABEL[] = {"", "DEPLOY", "PACK UP", "LOAD", "UNLOAD", ""};
    static const char KEY[3] = {'Z', 'C', 'V'};
    command_t list[3];
    int n = commands(list);
    int s = rect.w / COMMAND_W > 0 ? rect.w / COMMAND_W : 1;
    if (item->value >= n) return;
    command_t c = list[item->value];
    const char *label = LABEL[c.kind];
    char line[24];
    uint32_t ink = 0xffdce6dcu;
    if (c.kind == CMD_RITE) {
        rf_rite_t rite = RF_HandCard(consoleplayer, c.slot);
        if (rite) {
            label = RF_RiteName(rite);
            ink = 0xff7ff0ffu;
            V_FillRectTranslucent((irect_t){rect.x + s, rect.y + s, rect.w - 2 * s, rect.h - 2 * s}, 0x5030a0c0u);
        } else {
            /* the first empty slot counts down to the next card */
            bool first = true;
            for (int i = 0; i < c.slot; ++i) first &= RF_HandCard(consoleplayer, i) != RF_RITE_NONE;
            int ms = RF_NextDrawMs(consoleplayer);
            if (first && ms >= 0) snprintf(line, sizeof(line), "%ds", (ms + 999) / 1000);
            else line[0] = 0;
            label = line;
            ink = 0xff5a6064u;
        }
    }
    char key[2] = {KEY[item->value], 0};
    text((irect_t){rect.x + 3 * s, rect.y + 4 * s, rect.w - 6 * s, 7 * s}, key, 0xff8a9496u);
    text((irect_t){rect.x + 3 * s, rect.y + (rect.h - 7 * s) / 2 + 4 * s, rect.w - 6 * s, 7 * s}, label, ink);
}

/* ── the sidebar ───────────────────────────────────────────────────────── */

static menuitem_t items[SB_COUNT] = {
    [SB_PANEL] = {.visible = true, .rect = {RF_SIDEBAR_X, 0, SIDEBAR_W, 480},
                  .fill = 0xff161a1cu, .border = 0xff4a5458u},
    [SB_CREDITS] = {.visible = true, .rect = {RF_SIDEBAR_X + 8, 6, SIDEBAR_W - 16, 22}, .ink = 0xffe6d750u,
                    .align = MALIGN_RIGHT, .ownerdraw = HU_DrawCounter},
    [SB_POWER] = {.visible = true, .rect = {RF_SIDEBAR_X + 8, 8, 64, 18}, .ownerdraw = draw_power},
    [SB_RADAR] = {.kind = MI_MINIMAP, .visible = true, .enabled = true,
                  .rect = {RF_SIDEBAR_X + 6, 34, SIDEBAR_W - 12, 104}, .ownerdraw = draw_radar},
    [SB_PRODUCTS] = {.kind = MI_LIST, .visible = true, .enabled = true,
                     .rect = {RF_SIDEBAR_X + 4, 144, SIDEBAR_W - 8, ROWS * ROW_H}, .row_height = ROW_H, .value = -1,
                     .routine = HU_ProductList, .ownerdraw = draw_products, .drawtarget = HU_DrawProductPlacement},
    /* R repairs the selected structures (again: stops), X sells them. */
    [SB_REPAIR] = {.kind = MI_BUTTON, .visible = true, .enabled = true, .hotkey = SDLK_r, .value = RF_ORDER_REPAIR,
                   .rect = {RF_SIDEBAR_X + 4, BUTTONS_Y, 54, 22}, .fill = 0xff20282bu, .border = 0xff4a5458u,
                   .routine = structure_button, .ownerdraw = draw_structure_button, .tooltip = "Repair (R)"},
    [SB_SELL] = {.kind = MI_BUTTON, .visible = true, .enabled = true, .hotkey = SDLK_x, .value = RF_ORDER_SELL,
                 .rect = {RF_SIDEBAR_X + 60, BUTTONS_Y, 44, 22}, .fill = 0xff20282bu, .border = 0xff4a5458u,
                 .routine = structure_button, .ownerdraw = draw_structure_button, .tooltip = "Sell (X)"},
    [SB_PAGE] = {.kind = MI_BUTTON, .visible = true, .enabled = true,
                 .rect = {RF_SIDEBAR_X + 106, BUTTONS_Y, SIDEBAR_W - 110, 22},
                 .fill = 0xff20282bu, .border = 0xff4a5458u, .link = SB_PRODUCTS,
                 .routine = HU_ProductPage, .ownerdraw = draw_page},
#define COMMAND(i, key) \
    [SB_CMD0 + i] = {.kind = MI_BUTTON, .visible = true, .enabled = true, .hotkey = key, .value = i, \
                     .rect = {RF_SIDEBAR_X + 4 + i * (COMMAND_W + 2), COMMANDS_Y, COMMAND_W, 480 - COMMANDS_Y - 6}, \
                     .fill = 0xff20282bu, .border = 0xff4a5458u, .routine = command_button, \
                     .ownerdraw = draw_command, .drawtarget = draw_rite_target}
    COMMAND(0, SDLK_z), COMMAND(1, SDLK_c), COMMAND(2, SDLK_v),
#undef COMMAND
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
