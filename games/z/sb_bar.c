#include "z_local.h"
#include "info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The in-level screen: a panel down the right with the radar, the
 * clock, the territory tally and what is selected, and a window that
 * opens beside a selected fort or factory of the player's to run its
 * production. ZED.EXE's panel pieces (NEWPANEL.DAT in HEADFX.PAC) are
 * not decoded, so the panel is drawn in the world's palette with the
 * game's own CHARS.BIN font. The window works as Z's does: STOP halts
 * what is being made, the arrows then pick a unit, and START sets the
 * factory going; it keeps making that unit until it is stopped. */

enum { SIDEBAR = 160, MARGIN = 8, RADAR = 144, POPUP_W = 156, POPUP_H = 104, PICTURE = 48, BUTTON_H = 20 };

enum {
    IT_PANEL, IT_RADAR, IT_CLOCK, IT_TALLY, IT_INFO, IT_MENU,
    IT_POPUP, IT_TITLE, IT_PICTURE, IT_NAME, IT_PROGRESS, IT_STOP, IT_PREV, IT_NEXT, IT_START,
    IT_COUNT
};

/* Team colours from the palette runs at 0x10, 0x20, 0x30, 0x40 and the
 * greys at 0x50 (Z_FORMATS.md); the rest are matched into the palette. */
static const uint8_t team_index[Z_NEUTRAL + 1] = {0x18, 0x27, 0x39, 0x4d, 0x59};
static const uint32_t team_argb[Z_NEUTRAL + 1] = {0xffe61414u, 0xff1438ffu, 0xff3cae44u, 0xfff6ce71u, 0xff959595u};
#define PANEL_FILL 0xff1c2024u
#define PANEL_EDGE 0xff5c6468u
#define BOX_FILL 0xff0c1014u
#define BUTTON_FILL 0xff2c3640u
#define BUTTON_EDGE 0xff8894a0u
#define FOCUS 0xfff4f4f4u
#define INK 0xffe8e8e8u
#define INK_DIM 0xff707880u
#define INK_TITLE 0xfff4d470u
#define INK_GOOD 0xff48d048u
#define INK_BAD 0xffe03030u

static menuitem_t items[IT_COUNT];
static menu_t hud = {.items = items, .numitems = IT_COUNT, .itemOn = -1};
static bitmapfont_t font;
static bool font_ok, laid_out;

/* The window's producer and, while it is stopped, the unit picked. Each
 * producer remembers its pick. */
static uint32_t popup_for;
static int popup_choice;
static const StaticProductDefinition *offered[32];
static int offered_count;
typedef struct { uint32_t id; int choice; } pick_t;
static pick_t picks[32];

static int remembered(uint32_t id) {
    for (size_t i = 0; i < sizeof(picks) / sizeof(*picks); ++i)
        if (picks[i].id == id) return picks[i].choice;
    return 0;
}

static void remember(uint32_t id, int choice) {
    size_t free = sizeof(picks) / sizeof(*picks) - 1;
    for (size_t i = 0; i < sizeof(picks) / sizeof(*picks); ++i) {
        if (picks[i].id == id) { picks[i].choice = choice; return; }
        if (!picks[i].id && i < free) free = i;
    }
    picks[free] = (pick_t){id, choice};
}

/* ── the font ─────────────────────────────────────────────────────────── */

/* CHARS.BIN: 128 glyphs of eight rows, one bit a pixel, the high bit
 * left. Glyphs advance by their own width plus one. */
static bool build_font(const uint8_t *bits, size_t size, bitmapfont_t *out) {
    enum { GLYPHS = 128, SIZE = 8 };
    if (size < GLYPHS * SIZE) return false;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < 256; ++i) out->glyph_index[i] = -1;
    if (!R_AllocSpriteCells(&out->sprite, GLYPHS)) return false;
    for (int ch = 0; ch < GLYPHS; ++ch) {
        uint8_t *pixels = calloc(SIZE * SIZE, 1);
        if (!pixels) { HU_FreeFont(out); return false; }
        int width = 0;
        for (int y = 0; y < SIZE; ++y)
            for (int x = 0; x < SIZE; ++x) {
                if (!(bits[ch * SIZE + y] & (0x80 >> x))) continue;
                pixels[y * SIZE + x] = 1;
                if (x + 1 > width) width = x + 1;
            }
        out->sprite.lumps[ch].indices = pixels;
        out->sprite.cells[ch].rect = (irect_t){0, 0, SIZE, SIZE};
        out->glyph_index[ch] = ch;
        out->glyph_width[ch] = (uint8_t)(width ? width + 1 : SIZE / 2);
    }
    out->sprite.indexed = true;
    out->sprite.frame_size = (isize2_t){SIZE, SIZE};
    out->sprite.palette[1] = out->sprite.source_palette[1] = 0xffffffffu;
    out->glyph_size = (isize2_t){SIZE, SIZE};
    out->line_h = SIZE + 2;
    out->own_palette = true;
    return true;
}

bool HU_LoadFont(const char *root, bitmapfont_t *out) {
    char path[1024];
    blob_t blob;
    M_PathJoin(path, sizeof(path), root && root[0] ? root : g_game_default_root, "WARDATA.PAC");
    if (!Z_ReadPac(path, "CHARS.BIN", &blob)) return false;
    bool ok = build_font(blob.bytes, blob.size, out);
    W_FreeFile(&blob);
    return ok;
}

/* Z's font speaks in capitals. */
static const char *upper(const char *text) {
    static char out[64];
    size_t i = 0;
    for (; text && text[i] && i + 1 < sizeof(out); ++i)
        out[i] = (char)((text[i] >= 'a' && text[i] <= 'z') ? text[i] - 'a' + 'A' : text[i]);
    out[i] = '\0';
    return out;
}

static int text_width(const char *text) {
    return font_ok ? V_TextWidth(&font, text) : (int)strlen(text) * 6;
}

static void draw_ink(ivec2_t at, const char *text, uint32_t ink) {
    if (!text || !text[0]) return;
    if (!font_ok) {
        V_DrawSmallText((irect_t){at.x, at.y, 640, 7}, text, ink, V_DrawSize());
        return;
    }
    uint8_t tint[256];
    V_ModulateRemap(tint, font.sprite.source_palette, ink);
    V_DrawText(at, &font, text, tint);
}

static void draw_centred(irect_t rect, int y, const char *text, uint32_t ink) {
    draw_ink((ivec2_t){rect.x + (rect.w - text_width(text)) / 2, y}, text, ink);
}

/* ── pictures ─────────────────────────────────────────────────────────── */

/* The lump of `frame` facing south, or the only one. */
static int picture_lump(const spritesheet_t *sheet, int frame) {
    if (!sheet || frame < 0) return -1;
    if (frame >= sheet->spritedef.numframes) return frame < sheet->numlumps ? frame : -1;
    const spriteframe_t *f = &sheet->spritedef.spriteframes[frame];
    if (!f->directions || f->rotations <= 0) return -1;
    const spritedirection_t *d = &f->directions[f->rotations == 8 ? 4 : 0];
    return d->layers ? d->layers[0].lump : -1;
}

static bool state_picture(int state_id, const spritesheet_t **sheet, int *lump) {
    if (state_id <= 0 || state_id >= NUMSTATES || !hudview.sprites) return false;
    const state_t *st = &states[state_id];
    *sheet = R_CacheLookup(hudview.sprites, sprnames[st->sprite]);
    *lump = picture_lump(*sheet, st->frame);
    return *sheet && *lump >= 0 && *lump < (*sheet)->numlumps;
}

/* Fit the picture in `box`, up to twice its size. */
static void draw_picture(const spritesheet_t *sheet, int lump, int team, irect_t box) {
    irect_t src = sheet->cells[lump].rect;
    if (src.w <= 0 || src.h <= 0) return;
    irect_t dst = {0, 0, src.w, src.h};
    int fit = box.w / src.w < box.h / src.h ? box.w / src.w : box.h / src.h;
    if (fit >= 1) {
        if (fit > 2) fit = 2;
        dst.w = src.w * fit;
        dst.h = src.h * fit;
    } else if (src.w * box.h > src.h * box.w) {
        dst.w = box.w;
        dst.h = src.h * box.w / src.w;
    } else {
        dst.h = box.h;
        dst.w = src.w * box.h / src.h;
    }
    dst.x = box.x + (box.w - dst.w) / 2;
    dst.y = box.y + (box.h - dst.h) / 2;
    irect_t clip = V_GetClip();
    V_SetClip(box);
    R_DrawSprite(sheet, lump, team, NULL, &dst, 0, 16);
    V_SetClip(clip);
}

static void draw_box(irect_t box) {
    V_FillRect(box, V_NearestIndex(BOX_FILL));
    V_DrawRectOutline(box, V_NearestIndex(PANEL_EDGE));
}

/* ── selection ────────────────────────────────────────────────────────── */

static bool live(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0; }

static mobj_t *selected(int n, int *count) {
    mobj_t *found = NULL;
    int seen = 0;
    for (int i = 0; i < hudview.unit_count; ++i) {
        mobj_t *mo = hudview.units ? hudview.units[i] : NULL;
        if (!live(mo) || !P_MobjIsSelected(mo)) continue;
        if (seen++ == n) found = mo;
    }
    if (count) *count = seen;
    return found;
}

/* The window opens for a fort or factory of the player's selected alone. */
static mobj_t *selected_producer(void) {
    int count;
    mobj_t *mo = selected(0, &count);
    return count == 1 && mo->owner == consoleplayer && Z_Producer(mo) ? mo : NULL;
}

static const char *short_name(const mobj_t *mo) {
    switch (mo->type_id) {
    case MT_FORT: return "FORT";
    case MT_ROBOT_FACTORY: return "ROBOT FACTORY";
    case MT_TANK_FACTORY: return "VEHICLE FACTORY";
    case MT_REPAIR: return "REPAIR FACILITY";
    case MT_RADAR: return "RADAR STATION";
    default: return mo->info ? mo->info->name : "?";
    }
}

/* ── the panel ────────────────────────────────────────────────────────── */

static void draw_radar(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)item;
    const zmap_t *map = level.native_data;
    const tileset_t *tiles = hudview.tileset;
    if (!menu->app || level.width <= 0 || level.height <= 0 || rect.w <= 0 || rect.h <= 0) return;
    irect_t clip = V_GetClip();
    V_SetClip(rect);
    uint8_t ground = V_NearestIndex(0xff303030u);
    for (int py = 0; py < rect.h; ++py)
        for (int px = 0; px < rect.w; ++px) {
            int gx = px * level.width / rect.w, gy = py * level.height / rect.h;
            uint8_t colour = ground;
            if (tiles && tiles->indices && L_Contains(&level, gx, gy)) {
                int tile = level.tile_ids[L_Index(&level, gx, gy)];
                if (tile >= 0 && tile < tiles->count)
                    colour = tiles->indices[((size_t)tile * tiles->tile_h + tiles->tile_h / 2) * tiles->tile_w + tiles->tile_w / 2];
            }
            V_DrawPoint((ivec2_t){rect.x + px, rect.y + py}, colour);
        }
    /* Territories wash in their holder's colour; the flags say who that is. */
    for (int i = 0; map && i < map->territory_count; ++i) {
        const zterritory_t *t = &map->territories[i];
        int team = Z_NEUTRAL;
        for (int u = 0; u < hudview.unit_count; ++u) {
            const mobj_t *flag = hudview.units[u];
            if (flag->type_id == MT_FLAG && !flag->remove && flag->z.territory == i + 1) {
                team = flag->team < Z_NEUTRAL ? flag->team : Z_NEUTRAL;
                break;
            }
        }
        if (team >= Z_NEUTRAL) continue;
        irect_t area = {rect.x + t->min.x * rect.w / level.width, rect.y + t->min.y * rect.h / level.height,
                        (t->max.x - t->min.x) * rect.w / level.width, (t->max.y - t->min.y) * rect.h / level.height};
        V_FillRectTranslucent(area, (team_argb[team] & 0x00ffffffu) | 0x40000000u);
    }
    R_DrawMinimapFog(&level, rect);
    for (int i = 0; i < hudview.unit_count; ++i) {
        const mobj_t *mo = hudview.units[i];
        if (!live(mo) || !P_VisibleToPlayer(mo) || mo->type_id == MT_FLAG || mo->type_id == MT_GRENADE) continue;
        bool structure = mo->type_id >= MT_FORT && mo->type_id < MT_UNITS;
        if (!structure && (Z_UnitType(mo) < 0 || mo->team >= Z_NEUTRAL)) continue;
        irect_t cells = P_MobjCells(mo);
        irect_t dot = {rect.x + cells.x * rect.w / level.width, rect.y + cells.y * rect.h / level.height,
                       structure ? cells.w * rect.w / level.width : 2, structure ? cells.h * rect.h / level.height : 2};
        V_FillRect(dot, team_index[mo->team < Z_NEUTRAL ? mo->team : Z_NEUTRAL]);
    }
    irect_t world = G_WorldViewport(menu->app);
    cell_t tl = R_ScreenToGrid(menu->app, world.x, world.y);
    cell_t br = R_ScreenToGrid(menu->app, world.x + world.w - 1, world.y + world.h - 1);
    irect_t view = {rect.x + tl.x * rect.w / level.width, rect.y + tl.y * rect.h / level.height,
                    (br.x - tl.x) * rect.w / level.width, (br.y - tl.y) * rect.h / level.height};
    if (view.w < 2) view.w = 2;
    if (view.h < 2) view.h = 2;
    V_DrawRectOutline(view, V_NearestIndex(0xffffffffu));
    V_SetClip(clip);
    V_DrawRectOutline(rect, V_NearestIndex(PANEL_EDGE));
}

static void draw_clock(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    int seconds = leveltime / RTS_TICRATE;
    char text[32];
    snprintf(text, sizeof(text), "TIME %02d:%02d", (seconds / 60) % 100, seconds % 60);
    draw_centred(rect, rect.y + 2, text, INK);
}

/* Each side's flags, in its colour, and those nobody holds. */
static void draw_tally(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    int total, free = Z_TerritoriesHeld(Z_NEUTRAL, &total);
    char text[32];
    draw_centred(rect, rect.y + 2, "TERRITORIES", INK_TITLE);
    int x = rect.x + 4;
    for (int team = 0; team < Z_NEUTRAL; ++team) {
        int held = Z_TerritoriesHeld(team, NULL);
        if (!held) continue;
        V_FillRect((irect_t){x, rect.y + 16, 10, 8}, team_index[team]);
        snprintf(text, sizeof(text), "%d", held);
        draw_ink((ivec2_t){x + 13, rect.y + 16}, text, INK);
        x += 36;
    }
    snprintf(text, sizeof(text), "FREE %d OF %d", free, total);
    draw_centred(rect, rect.y + 30, text, INK_DIM);
}

static void draw_bar(irect_t bar, int part, int whole, uint32_t argb) {
    V_FillRect(bar, V_NearestIndex(BOX_FILL));
    V_DrawRectOutline(bar, V_NearestIndex(PANEL_EDGE));
    int fill = whole > 0 ? (bar.w - 2) * part / whole : 0;
    if (fill > bar.w - 2) fill = bar.w - 2;
    if (fill > 0) V_FillRect((irect_t){bar.x + 1, bar.y + 1, fill, bar.h - 2}, V_NearestIndex(argb));
}

static void draw_info(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    int count;
    const mobj_t *mo = selected(0, &count);
    if (!mo) {
        draw_centred(rect, rect.y + 4, "NOTHING SELECTED", INK_DIM);
        return;
    }
    irect_t box = {rect.x, rect.y, PICTURE, PICTURE};
    draw_box(box);
    const spritesheet_t *sheet;
    int lump;
    if (state_picture(mo->core.state_id, &sheet, &lump)) draw_picture(sheet, lump, mo->team, box);
    int team = mo->team < Z_NEUTRAL ? mo->team : Z_NEUTRAL;
    char text[64];
    const char *name = Z_UnitType(mo) >= 0 && mo->info ? upper(mo->info->name) : short_name(mo);
    draw_ink((ivec2_t){rect.x + PICTURE + 6, rect.y + 2}, name, team_argb[team]);
    if (count > 1) {
        snprintf(text, sizeof(text), "%d SELECTED", count);
        draw_ink((ivec2_t){rect.x + PICTURE + 6, rect.y + 14}, text, INK);
    } else if (mo->team >= Z_NEUTRAL) {
        draw_ink((ivec2_t){rect.x + PICTURE + 6, rect.y + 14}, Z_UnitType(mo) >= 0 ? "EMPTY" : "NOBODY'S", INK_DIM);
    } else {
        draw_ink((ivec2_t){rect.x + PICTURE + 6, rect.y + 14},
                 mo->team == consoleplayer ? "YOURS" : "ENEMY", INK_DIM);
    }
    if (mo->owner != consoleplayer || mo->max_hp <= 0) return;
    int y = rect.y + PICTURE + 6;
    int good = mo->hp * 3 >= mo->max_hp;
    draw_bar((irect_t){rect.x, y, rect.w, 8}, mo->hp, mo->max_hp, good ? INK_GOOD : INK_BAD);
    snprintf(text, sizeof(text), "%d / %d", mo->hp, mo->max_hp);
    draw_ink((ivec2_t){rect.x, y + 12}, text, INK_DIM);
    y += 26;
    if (mo->z.grenades) {
        snprintf(text, sizeof(text), "GRENADES %d", mo->z.grenades);
        draw_ink((ivec2_t){rect.x, y}, text, INK);
        y += 12;
    }
    const StaticProductDefinition *making = Z_ProductInProduction(mo);
    if (making) {
        draw_ink((ivec2_t){rect.x, y}, "MAKING", INK_DIM);
        draw_ink((ivec2_t){rect.x, y + 12}, upper(making->label), INK);
    } else if (Z_Producer(mo)) {
        draw_ink((ivec2_t){rect.x, y}, "IDLE", INK_DIM);
    }
}

/* ── the production window ────────────────────────────────────────────── */

static void draw_product_picture(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    draw_box(rect);
    if (popup_choice < 0 || popup_choice >= offered_count) return;
    const spritesheet_t *sheet;
    int lump;
    if (state_picture(mobjinfo[offered[popup_choice]->product_type].spawnstate, &sheet, &lump))
        draw_picture(sheet, lump, consoleplayer, rect);
}

static void draw_progress(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    const mobj_t *producer = P_MobjById(popup_for);
    const production_t *queue = producer ? producer->production : NULL;
    bool busy = Z_ProductInProduction(producer) != NULL;
    draw_ink((ivec2_t){rect.x, rect.y}, busy ? "MANUFACTURING" : "STOPPED", busy ? INK_GOOD : INK_DIM);
    int done = busy && queue->time_ms > 0 ? queue->time_ms - queue->time_left_ms : 0;
    draw_bar((irect_t){rect.x, rect.y + 12, rect.w, 8}, done, busy ? queue->time_ms : 1, INK_GOOD);
    char text[32];
    if (busy) snprintf(text, sizeof(text), "%d S LEFT", (queue->time_left_ms + 999) / 1000);
    else snprintf(text, sizeof(text), "%d S", popup_choice >= 0 && popup_choice < offered_count ?
                  G_ModelProductTrainingTimeMs(offered[popup_choice]) / 1000 : 0);
    draw_ink((ivec2_t){rect.x, rect.y + 24}, text, INK_DIM);
}

static void on_menu(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action == MA_ACTIVATE && menu->app) M_StartControlPanel(menu->app);
}

static void on_stop(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu; (void)item;
    if (action != MA_ACTIVATE || !popup_for) return;
    ticcmd_t cmd = {.order = TC_CANCEL_PRODUCTION, .count = 1, .units = {popup_for}};
    G_QueueTiccmd(&cmd);
}

static void on_step(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (action != MA_ACTIVATE || offered_count <= 0) return;
    popup_choice = (popup_choice + item->step + offered_count) % offered_count;
    remember(popup_for, popup_choice);
}

static void on_start(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu; (void)item;
    mobj_t *producer = P_MobjById(popup_for);
    if (action != MA_ACTIVATE || !producer || popup_choice < 0 || popup_choice >= offered_count) return;
    G_BuildOrder(producer, offered[popup_choice]->ui_id);
}

static void button(menuitem_t *item, const char *text, int step, menuroutine_t routine) {
    *item = (menuitem_t){
        .kind = MI_BUTTON, .enabled = true, .fill = BUTTON_FILL, .border = BUTTON_EDGE, .color = FOCUS,
        .ink = INK, .align = MALIGN_CENTER, .step = step, .routine = routine, .disabled_look = true,
        .font = font_ok ? &font : NULL,
    };
    item->look[MS_DISABLED].ink = INK_DIM;
    item->look[MS_PUSHED].shift = (ivec2_t){1, 1};
    snprintf(item->text, sizeof(item->text), "%s", text);
}

static void label(menuitem_t *item, uint32_t ink, int align) {
    *item = (menuitem_t){.ink = ink, .align = align, .font = font_ok ? &font : NULL};
}

static void layout(void) {
    memset(items, 0, sizeof(items));
    items[IT_PANEL] = (menuitem_t){.fill = PANEL_FILL, .border = PANEL_EDGE};
    items[IT_RADAR] = (menuitem_t){.kind = MI_MINIMAP, .enabled = true, .ownerdraw = draw_radar};
    items[IT_CLOCK] = (menuitem_t){.ownerdraw = draw_clock};
    items[IT_TALLY] = (menuitem_t){.ownerdraw = draw_tally};
    items[IT_INFO] = (menuitem_t){.ownerdraw = draw_info};
    button(&items[IT_MENU], "MENU", 0, on_menu);
    items[IT_POPUP] = (menuitem_t){.layer = true, .fill = PANEL_FILL, .border = BUTTON_EDGE};
    label(&items[IT_TITLE], INK_TITLE, MALIGN_HCENTER);
    items[IT_PICTURE] = (menuitem_t){.ownerdraw = draw_product_picture};
    label(&items[IT_NAME], INK, MALIGN_LEFT);
    items[IT_PROGRESS] = (menuitem_t){.ownerdraw = draw_progress};
    button(&items[IT_STOP], "STOP", 0, on_stop);
    button(&items[IT_PREV], "<", -1, on_step);
    button(&items[IT_NEXT], ">", 1, on_step);
    button(&items[IT_START], "START", 0, on_start);
    for (int i = 0; i < IT_COUNT; ++i) items[i].visible = i < IT_POPUP;
}

/* The window sits beside its building, on whichever side has room, and
 * inside the world view. */
static void place_popup(const menu_t *menu, const mobj_t *producer, int scale) {
    irect_t cells = P_MobjCells(producer);
    fvec2_t a, b;
    R_MapToScreen(menu->app, &level, (float)cells.x, (float)cells.y, &a.x, &a.y);
    R_MapToScreen(menu->app, &level, (float)(cells.x + cells.w), (float)(cells.y + cells.h), &b.x, &b.y);
    irect_t world = G_WorldViewport(menu->app);
    irect_t view = {world.x / scale, world.y / scale, world.w / scale, world.h / scale};
    int x = (int)b.x / scale + 4, y = ((int)a.y + (int)b.y) / (2 * scale) - POPUP_H / 2;
    if (x + POPUP_W > view.x + view.w) x = (int)a.x / scale - 4 - POPUP_W;
    if (x + POPUP_W > view.x + view.w) x = view.x + view.w - POPUP_W;
    if (x < view.x) x = view.x;
    if (y + POPUP_H > view.y + view.h) y = view.y + view.h - POPUP_H;
    if (y < view.y) y = view.y;
    items[IT_POPUP].rect = (irect_t){x, y, POPUP_W, POPUP_H};
    items[IT_TITLE].rect = (irect_t){x, y + 4, POPUP_W, 10};
    items[IT_PICTURE].rect = (irect_t){x + 6, y + 18, PICTURE, PICTURE};
    items[IT_NAME].rect = (irect_t){x + PICTURE + 12, y + 18, POPUP_W - PICTURE - 18, 10};
    items[IT_PROGRESS].rect = (irect_t){x + PICTURE + 12, y + 32, POPUP_W - PICTURE - 18, 36};
    int row = y + POPUP_H - BUTTON_H - 6;
    items[IT_STOP].rect = (irect_t){x + 6, row, 42, BUTTON_H};
    items[IT_PREV].rect = (irect_t){x + 54, row, 20, BUTTON_H};
    items[IT_NEXT].rect = (irect_t){x + 80, row, 20, BUTTON_H};
    items[IT_START].rect = (irect_t){x + 106, row, 44, BUTTON_H};
}

static void refresh_popup(const menu_t *menu, int scale) {
    mobj_t *producer = selected_producer();
    uint32_t id = producer ? producer->id : 0;
    if (id != popup_for) {
        popup_for = id;
        popup_choice = remembered(id);
    }
    for (int i = IT_POPUP; i < IT_COUNT; ++i) items[i].visible = producer != NULL;
    offered_count = producer ? Z_ProductsFor(producer, offered, (int)(sizeof(offered) / sizeof(*offered))) : 0;
    if (!producer) {
        for (int i = IT_STOP; i <= IT_START; ++i) items[i].enabled = false;
        return;
    }
    const StaticProductDefinition *making = Z_ProductInProduction(producer);
    for (int i = 0; making && i < offered_count; ++i)
        if (offered[i] == making) popup_choice = i;
    if (popup_choice < 0 || popup_choice >= offered_count) popup_choice = 0;
    remember(id, popup_choice);
    items[IT_STOP].enabled = making != NULL;
    items[IT_PREV].enabled = items[IT_NEXT].enabled = items[IT_START].enabled = !making && offered_count > 0;
    snprintf(items[IT_TITLE].text, sizeof(items[IT_TITLE].text), "%s", short_name(producer));
    snprintf(items[IT_NAME].text, sizeof(items[IT_NAME].text), "%s",
             offered_count ? upper(offered[popup_choice]->label) : "NOTHING");
    place_popup(menu, producer, scale);
}

static void refresh(menu_t *menu) {
    int scale = R_UIScale(menu->app);
    isize2_t screen = menu->app ? (isize2_t){menu->app->win.w / scale, menu->app->win.h / scale}
                                : (isize2_t){SCREENWIDTH, SCREENHEIGHT};
    int x = screen.w - SIDEBAR + MARGIN, w = SIDEBAR - 2 * MARGIN;
    items[IT_PANEL].rect = (irect_t){screen.w - SIDEBAR, 0, SIDEBAR, screen.h};
    items[IT_RADAR].rect = (irect_t){x, MARGIN, w, RADAR};
    items[IT_CLOCK].rect = (irect_t){x, MARGIN + RADAR + 6, w, 12};
    items[IT_TALLY].rect = (irect_t){x, MARGIN + RADAR + 22, w, 44};
    items[IT_INFO].rect = (irect_t){x, MARGIN + RADAR + 72, w, screen.h - (MARGIN + RADAR + 72) - 40};
    items[IT_MENU].rect = (irect_t){x, screen.h - MARGIN - BUTTON_H, w, BUTTON_H};
    refresh_popup(menu, scale);
}

/* ── the engine's entry points ────────────────────────────────────────── */

irect_t G_WorldViewport(const app_t *app) {
    int scale = R_UIScale(app);
    int w = app ? app->win.w - SIDEBAR * scale : 1;
    return (irect_t){0, 0, w > 0 ? w : 1, app ? app->win.h : 1};
}

menu_t *G_InitHUD(app_t *app, const char *data_root) {
    if (!font_ok) font_ok = HU_LoadFont(data_root, &font);
    if (!laid_out) {
        layout();
        hud.refresh = refresh;
        laid_out = true;
    }
    popup_for = 0;
    memset(picks, 0, sizeof(picks));
    hud.app = app;
    return &hud;
}

void G_ShutdownHUD(void) {
    hudview = (hudview_t){0};
    if (font_ok) HU_FreeFont(&font);
    font_ok = false;
    laid_out = false;
}
