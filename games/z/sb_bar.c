#include "z_local.h"
#include "info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The in-level screen, built from the 640x480 panel art in SHEADFX.PAC
 * (w_panel.c): a panel down the right and a bar along the bottom, laid
 * out as ZED.EXE lays them out. The panel holds the clock, the selected
 * unit's face or picture, its name, weapon, grenades and health, and the
 * radar. The bar holds buttons that step through the player's robots,
 * vehicles, buildings and guns, each side's share of the territories
 * (calc_territory_percents in ZED.EXE) and the menu button. The window
 * that opens beside a selected fort or factory to run its production is
 * drawn from SPRITES.RSC 4694..4722, as in ZED.EXE 0x24ce0.
 *
 * The panel's T, D and Z buttons do nothing: ZED.EXE's handlers for them
 * are not traced. Their third pieces are lit, not greyed, so they are
 * toggles of some kind; they are drawn up. */

enum { SIDEBAR = 96, BOTTOM = 32, POPUP_W = 112, POPUP_H = 80 };

/* NSNEWPAN.DAT pieces. Buttons come in threes: up, pressed and greyed. */
enum {
    PC_PANEL = 0,          /* 96x480 */
    PC_WEAPON = 1,         /* rifle, flame gun, laser, missile gun, machine gun */
    PC_GRENADE = 6,
    PC_BUTTON_T = 13, PC_BUTTON_D = 16, PC_BUTTON_Z = 19, PC_BUTTON_B = 22, PC_BUTTON_R = 25,
    PC_BUTTON_G = 28, PC_BUTTON_V = 31, PC_BUTTON_MENU = 34, PC_BUTTON_A = 40,
    PC_NAME_NONE = 43,     /* the red name bar with nothing on it */
    PC_LABEL_BLANK = 44,   /* a grey label with nothing on it */
    PC_WEAPON_NAME = 45,   /* in the weapons' order */
    PC_DIGIT = 50,         /* 0..9, 12x18 */
    PC_HEALTH_GOOD = 60, PC_HEALTH_POOR = 61, PC_HEALTH_NONE = 62, /* 74x10 */
    PC_BAR_THREE = 66,     /* 544x32 bottom bar with three sides' slots */
    PC_NAME_ROBOT = 79,    /* Grunt, Pyros, Laser, Tough, Psychos, Sniper, Wrong Name */
    PC_NAME_VEHICLE = 86,  /* Missile, Jeep, Howitzer, Heavy, Gattling, Mo. Missile, Gun,
                            * Crane, Light, Fire Truck, APC, Medium */
    PC_VEHICLE = 98,       /* pictures in the same order */
    PC_BAR_FOUR = 110,     /* 544x32 bottom bar with four sides' slots */
    PC_BAR_TWO = 111,      /* 544x32 bottom bar with two sides' slots */
    PC_VEHICLE_BACK = 113,
};

/* Where the panel's pieces go, in panel pixels. */
enum {
    PANEL_CLOCK_Y = 4, PANEL_PORTRAIT_Y = 42, PANEL_PORTRAIT_H = 78, PANEL_NAME_Y = 122,
    PANEL_WEAPON_Y = 144, PANEL_GRENADE_Y = 186, PANEL_HEALTH_X = 12, PANEL_HEALTH_Y = 210,
    PANEL_HEALTH_W = 74, PANEL_LABEL_Y = 228, PANEL_BUTTON_Y = 262,
    PANEL_RADAR_X = 6, PANEL_RADAR_Y = 288, PANEL_RADAR_W = 86, PANEL_RADAR_H = 104,
    BAR_BUTTON_Y = 8, BAR_MENU_X = 480,
};

enum {
    IT_PANEL, IT_BAR, IT_RADAR, IT_ATTACKED, IT_T, IT_D, IT_Z, IT_ROBOTS, IT_VEHICLES, IT_BUILDINGS, IT_GUNS, IT_MENU,
    IT_POPUP, IT_STOP, IT_PREV, IT_NEXT, IT_START,
    IT_COUNT
};

/* Team colours from the palette runs at 0x10, 0x20, 0x30, 0x40 and the
 * greys at 0x50 (Z_FORMATS.md); the rest are matched into the palette. */
static const uint8_t team_index[Z_NEUTRAL + 1] = {0x18, 0x27, 0x39, 0x4d, 0x59};
static const uint32_t team_argb[Z_NEUTRAL + 1] = {0xffe61414u, 0xff1438ffu, 0xff3cae44u, 0xfff6ce71u, 0xff959595u};
#define PANEL_FILL 0xff1c2024u
#define BUTTON_FILL 0xff2c3640u
#define BUTTON_EDGE 0xff8894a0u
#define INK 0xffe8e8e8u

static menuitem_t items[IT_COUNT];
static menu_t hud = {.items = items, .numitems = IT_COUNT, .itemOn = -1};
static bitmapfont_t font;
static zpanel_t panel;
static bool font_ok, panel_ok, laid_out;

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

/* ── pieces ───────────────────────────────────────────────────────────── */

static irect_t piece_rect(int piece) {
    return panel_ok && piece >= 0 && piece < panel.pieces.numlumps ? panel.pieces.cells[piece].rect : (irect_t){0};
}

/* A piece with its top-left at `at`; opaque pieces write their black. */
static void draw_piece(int piece, ivec2_t at, bool opaque) {
    if (!panel_ok) return;
    V_DrawSpriteCell(at, &panel.pieces, piece, NULL, opaque ? V_OPAQUE : 0);
}

/* The left `width` columns of a piece. */
static void draw_piece_part(int piece, ivec2_t at, int width) {
    irect_t src = piece_rect(piece);
    if (width < src.w) src.w = width;
    if (src.w <= 0) return;
    V_DrawSpriteCellScaled((irect_t){at.x, at.y, src.w, src.h}, &panel.pieces, piece, &src, NULL, V_OPAQUE);
}

/* `digits` of a number, in the 12x18 figures, the most significant first. */
static void draw_number(ivec2_t at, int value, int digits) {
    if (value < 0) value = 0;
    for (int i = digits - 1, divisor = 1; i >= 0; --i, divisor *= 10)
        draw_piece(PC_DIGIT + value / divisor % 10, (ivec2_t){at.x + i * 10, at.y}, false);
}

/* ── the selection ────────────────────────────────────────────────────── */

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

static bool structure(const mobj_t *mo) { return mo->type_id >= MT_FORT && mo->type_id < MT_UNITS; }

/* What the panel shows of a unit of weapon type `t`: its face
 * (SHEADBI0..4: Grunt, Tough, Pyro, Sniper, Psycho; the Laser has none
 * and borrows the Grunt's), its name label, its weapon and the vehicle
 * picture. -1 is none. */
typedef struct { int face, name, weapon, picture; } look_t;

static look_t unit_look(int t) {
    switch (t) {
    case MT_UNIT_GRUNT - MT_UNITS:   return (look_t){0, PC_NAME_ROBOT + 0, 0, -1};
    case MT_UNIT_PYRO - MT_UNITS:    return (look_t){2, PC_NAME_ROBOT + 1, 1, -1};
    case MT_UNIT_LASER - MT_UNITS:   return (look_t){0, PC_NAME_ROBOT + 2, 2, -1};
    case MT_UNIT_TOUGH - MT_UNITS:   return (look_t){1, PC_NAME_ROBOT + 3, 3, -1};
    case MT_UNIT_PSYCHO - MT_UNITS:  return (look_t){4, PC_NAME_ROBOT + 4, 4, -1};
    case MT_UNIT_SNIPER - MT_UNITS:  return (look_t){3, PC_NAME_ROBOT + 5, 0, -1};
    case MT_UNIT_MISSILE_GUN - MT_UNITS: return (look_t){-1, PC_NAME_VEHICLE + 0, -1, PC_VEHICLE + 0};
    case MT_UNIT_JEEP - MT_UNITS:    return (look_t){-1, PC_NAME_VEHICLE + 1, -1, PC_VEHICLE + 1};
    case MT_UNIT_HOWITZER - MT_UNITS: return (look_t){-1, PC_NAME_VEHICLE + 2, -1, PC_VEHICLE + 2};
    case MT_UNIT_HEAVY_TANK - MT_UNITS: return (look_t){-1, PC_NAME_VEHICLE + 3, -1, PC_VEHICLE + 3};
    case MT_UNIT_GATLING - MT_UNITS: return (look_t){-1, PC_NAME_VEHICLE + 4, -1, PC_VEHICLE + 4};
    case MT_UNIT_M_MISSILE - MT_UNITS: return (look_t){-1, PC_NAME_VEHICLE + 5, -1, PC_VEHICLE + 5};
    case MT_UNIT_LIGHT_GUN - MT_UNITS: return (look_t){-1, PC_NAME_VEHICLE + 6, -1, PC_VEHICLE + 6};
    case MT_UNIT_CRANE - MT_UNITS:   return (look_t){-1, PC_NAME_VEHICLE + 7, -1, PC_VEHICLE + 7};
    case MT_UNIT_LIGHT_TANK - MT_UNITS: return (look_t){-1, PC_NAME_VEHICLE + 8, -1, PC_VEHICLE + 8};
    case MT_UNIT_FIRE_TRUCK - MT_UNITS: return (look_t){-1, PC_NAME_VEHICLE + 9, -1, PC_VEHICLE + 9};
    case MT_UNIT_APC - MT_UNITS:     return (look_t){-1, PC_NAME_VEHICLE + 10, -1, PC_VEHICLE + 10};
    case MT_UNIT_MED_TANK - MT_UNITS: return (look_t){-1, PC_NAME_VEHICLE + 11, -1, PC_VEHICLE + 11};
    default: return (look_t){-1, -1, -1, -1};
    }
}

/* The scenery behind a face, by world: SHEADBAK.DAT holds desert, volcano,
 * a plain brown one, arctic, city and jungle. */
static int world_back(void) {
    static const int backs[Z_WORLDS] = {0, 3, 4, 1, 5};
    const zmap_t *map = level.native_data;
    return map && map->world >= 0 && map->world < Z_WORLDS ? backs[map->world] : 2;
}

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

/* Fit a world sprite in `box`, up to twice its size. */
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

/* The face or picture of the selection, its name, weapon, grenades and
 * health, down the panel from `at`. */
static void draw_selection(ivec2_t at) {
    const mobj_t *mo = selected(0, NULL);
    irect_t portrait = {at.x + 2, at.y + PANEL_PORTRAIT_Y, SIDEBAR - 4, PANEL_PORTRAIT_H};
    if (!mo) {
        draw_piece(PC_NAME_NONE, (ivec2_t){at.x, at.y + PANEL_NAME_Y}, true);
        draw_piece(PC_LABEL_BLANK, (ivec2_t){at.x, at.y + PANEL_LABEL_Y}, true);
        return;
    }
    int team = mo->team < Z_NEUTRAL ? mo->team : Z_NEUTRAL;
    look_t look = unit_look(Z_UnitType(mo));
    if (look.picture >= 0 && team < Z_NEUTRAL)
        look.face = unit_look(mo->z.driver ? mo->z.driver - 1 : MT_UNIT_GRUNT - MT_UNITS).face;
    const spritesheet_t *sheet;
    int lump;
    if (look.face >= 0 && panel_ok && panel.heads[look.face].numlumps > 2) {
        const spritesheet_t *head = &panel.heads[look.face];
        /* 0x2c131: copy the composed buffer starting at source row 4. */
        irect_t back = {0, 4, portrait.w, 75};
        V_DrawSpriteCellScaled((irect_t){portrait.x, portrait.y, back.w, back.h},
                              &panel.backs, world_back(), &back, NULL, V_OPAQUE);
        /* Neutral pose 0x000f (0x2c30c), decoded by 0x2c880. The
         * coordinates at 0x1311fa are in HEADFX source units. */
        static const ivec2_t body[Z_FACES] = {{2,26},{2,21},{2,23},{2,21},{2,23}};
        static const ivec2_t mouth[Z_FACES] = {{15,16},{15,16},{15,15},{15,16},{15,17}};
        static const ivec2_t eyes[Z_FACES] = {{11,8},{11,8},{11,7},{11,8},{11,9}};
        irect_t clip = V_GetClip();
        V_SetClip((irect_t){portrait.x + 4, portrait.y, 91, 75});
        const int parts[] = {1, 4, 20, 0};
        const ivec2_t offsets[] = {{4,3}, mouth[look.face], eyes[look.face], body[look.face]};
        for (int i = 0; i < 4; ++i)
            V_DrawSpriteCell(ivec2_add((ivec2_t){portrait.x, portrait.y - 4},
                                     (ivec2_t){offsets[i].x * 2, offsets[i].y * 2}), head, parts[i], NULL, 0);
        V_SetClip(clip);
    } else if (look.picture < 0 && !structure(mo) && state_picture(mo->core.state_id, &sheet, &lump)) {
        draw_picture(sheet, lump, mo->team, portrait);
    }
    /* A crewless vehicle, a building or a flag has no label of its own. */
    ivec2_t name_at = {at.x, at.y + PANEL_NAME_Y};
    if (look.name >= 0 && !(look.picture >= 0 && mo->team >= Z_NEUTRAL)) {
        draw_piece(look.name, name_at, true);
    } else {
        draw_piece(PC_NAME_NONE, name_at, true);
    }
    if (look.weapon >= 0) {
        draw_piece(PC_WEAPON + look.weapon, (ivec2_t){at.x, at.y + PANEL_WEAPON_Y}, false);
        draw_piece(PC_WEAPON_NAME + look.weapon, (ivec2_t){at.x, at.y + PANEL_LABEL_Y}, true);
        draw_piece(PC_GRENADE, (ivec2_t){at.x + 26, at.y + PANEL_GRENADE_Y}, false);
        draw_number((ivec2_t){at.x + 56, at.y + 188}, mo->z.grenades, 2);
    } else {
        draw_piece(PC_LABEL_BLANK, (ivec2_t){at.x, at.y + PANEL_LABEL_Y}, true);
        if (look.picture >= 0) {
            /* 0x13eb0c: vehicle images belong in the hardware panel,
             * with the driver's portrait above, rather than in its place. */
            static const uint8_t top[] = {8,8,10,4,10,2,16,6,2,10,8,2};
            draw_piece(PC_VEHICLE_BACK, (ivec2_t){at.x, at.y + PANEL_WEAPON_Y}, true);
            draw_piece(look.picture, (ivec2_t){at.x, at.y + PANEL_WEAPON_Y + top[look.picture - PC_VEHICLE]}, false);
        }
    }
    if (!structure(mo) && mo->max_hp > 0) {
        ivec2_t bar = {at.x + PANEL_HEALTH_X, at.y + PANEL_HEALTH_Y};
        draw_piece(PC_HEALTH_NONE, bar, true);
        int fill = PANEL_HEALTH_W * mo->hp / mo->max_hp;
        if (fill > 0) draw_piece_part(mo->hp * 3 >= mo->max_hp ? PC_HEALTH_GOOD : PC_HEALTH_POOR, bar, fill);
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
        bool building = structure(mo);
        if (!building && (Z_UnitType(mo) < 0 || mo->team >= Z_NEUTRAL)) continue;
        irect_t cells = P_MobjCells(mo);
        irect_t dot = {rect.x + cells.x * rect.w / level.width, rect.y + cells.y * rect.h / level.height,
                       building ? cells.w * rect.w / level.width : 2, building ? cells.h * rect.h / level.height : 2};
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
}

/* How many sides the level has, by its forts: the bottom bar has slots
 * for two, three or four. */
static int sides(void) {
    int most = 2;
    for (int i = 0; i < hudview.unit_count; ++i) {
        const mobj_t *mo = hudview.units[i];
        if (mo && mo->type_id == MT_FORT && mo->team < Z_NEUTRAL && mo->team + 1 > most) most = mo->team + 1;
    }
    return most;
}

/* Each side's share of the territories, as a number and a bar, in the
 * bottom bar's slots. */
static void draw_shares(ivec2_t at, int teams) {
    static const int two[2] = {130, 278}, three[3] = {130, 232, 334}, four[4] = {130, 204, 278, 352};
    const int *slots = teams > 3 ? four : teams > 2 ? three : two;
    int width = teams > 3 ? 40 : teams > 2 ? 54 : 80;
    int total;
    Z_TerritoriesHeld(Z_NEUTRAL, &total);
    for (int team = 0; team < teams; ++team) {
        int x = at.x + slots[team];
        int percent = total > 0 ? Z_TerritoriesHeld(team, NULL) * 100 / total : 0;
        if (percent > 99) percent = 99; /* 0x31fc2: two digits, saturating. */
        draw_number((ivec2_t){x, at.y + 10}, percent, 2);
        ivec2_t bar = {x + 22, at.y + 10};
        int base = teams > 3 ? 74 : teams > 2 ? 70 : 67;
        draw_piece_part(base + teams, bar, width);
        if (percent > 0) draw_piece_part(base + team, bar, width * percent / 100);
    }
}

static void draw_panel(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    if (!panel_ok) {
        V_FillRect(rect, V_NearestIndex(PANEL_FILL));
        return;
    }
    ivec2_t side = {rect.x, rect.y};
    draw_piece(PC_PANEL, side, true);
    int seconds = leveltime / RTS_TICRATE;
    draw_number((ivec2_t){side.x + 26, side.y + PANEL_CLOCK_Y}, seconds / 3600 % 10, 1);
    draw_number((ivec2_t){side.x + 50, side.y + PANEL_CLOCK_Y}, seconds / 60 % 60, 2);
    draw_number((ivec2_t){side.x + 74, side.y + PANEL_CLOCK_Y}, seconds % 60, 2);
    draw_selection(side);
}

static void draw_bottom(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    V_FillRect(rect, panel_ok ? 0 : V_NearestIndex(PANEL_FILL));
    if (!panel_ok) return;
    int teams = sides();
    draw_piece(teams > 3 ? PC_BAR_FOUR : teams > 2 ? PC_BAR_THREE : PC_BAR_TWO, (ivec2_t){rect.x, rect.y}, true);
    draw_shares((ivec2_t){rect.x, rect.y}, teams);
}

/* ── stepping through the player's things ─────────────────────────────── */

enum { KIND_ROBOT, KIND_VEHICLE, KIND_BUILDING, KIND_GUN, KIND_ATTACKED };

static bool attacked(const mobj_t *mo) {
    for (int i = 0; i < hudview.unit_count; ++i) {
        const mobj_t *foe = hudview.units[i];
        if (live(foe) && foe->team != mo->team && foe->attack.target == mo) return true;
    }
    return false;
}

static bool of_kind(const mobj_t *mo, int kind) {
    if (!live(mo) || mo->owner != consoleplayer) return false;
    if (kind == KIND_ATTACKED) return attacked(mo);
    if (structure(mo)) return kind == KIND_BUILDING;
    int t = Z_UnitType(mo);
    if (t < 0) return false;
    if (Z_IsRobot(t)) return kind == KIND_ROBOT;
    bool gun = t == MT_UNIT_LIGHT_GUN - MT_UNITS || t == MT_UNIT_MISSILE_GUN - MT_UNITS ||
               t == MT_UNIT_GATLING - MT_UNITS || t == MT_UNIT_HOWITZER - MT_UNITS;
    return kind == (gun ? KIND_GUN : KIND_VEHICLE);
}

/* Select the player's next thing of the kind after the one selected, and
 * look at it. */
static void on_step_kind(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    int last = -1, count = 0;
    for (int i = 0; i < hudview.unit_count; ++i)
        if (of_kind(hudview.units[i], item->id)) {
            ++count;
            if (P_MobjIsSelected(hudview.units[i])) last = i;
        }
    if (!count) return;
    mobj_t *pick = NULL;
    for (int i = last + 1; i < hudview.unit_count + last + 1 && !pick; ++i)
        if (of_kind(hudview.units[i % hudview.unit_count], item->id)) pick = hudview.units[i % hudview.unit_count];
    for (int i = 0; i < hudview.unit_count; ++i) P_MobjSetSelected(hudview.units[i], false);
    P_MobjSetSelected(pick, true);
    S_Bark(&pick, 1, SE_SELECT, true);
    if (menu->app) M_CentreView(menu->app, fixed3_xy_to_fvec2(pick->core.position));
}

/* ── the production window ────────────────────────────────────────────── */

static void window_piece(int native, ivec2_t at) {
    V_DrawSpriteCell(at, &panel.window, Z_UI_WINDOW + native - Z_UI_FIRST, NULL, 0);
}

/* Small RSC font, 0x5c684; the advances are ZED.EXE 0x148c46. */
static void window_text(ivec2_t at, const char *text) {
    static const uint8_t advance[59] = {
        4,3,4,6,4,6,6,3,3,3,7,6,3,5,4,6,4,4,4,4,4,4,4,4,4,4,3,4,4,4,4,6,
        8,5,6,5,6,5,5,5,5,4,6,6,5,6,5,5,5,6,5,5,6,5,5,6,6,6,5};
    for (; *text; ++text) {
        unsigned ch = (unsigned char)*text;
        if (ch < 32 || ch > 90) continue;
        window_piece(4690 + ch, at);
        at.x += advance[ch - 32];
    }
}

static void draw_window(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    if (!panel_ok) return;
    const mobj_t *producer = P_MobjById(popup_for);
    if (!producer) return;
    const production_t *queue = producer ? producer->production : NULL;
    bool busy = Z_ProductInProduction(producer) != NULL;
    ivec2_t at = {rect.x, rect.y};
    window_piece(4694, at);
    window_piece(producer->type_id == MT_ROBOT_FACTORY ? 4695 : producer->type_id == MT_FORT ? 4696 : 4697,
                 ivec2_add(at, (ivec2_t){8,6}));
    V_DrawSpriteCell(ivec2_add(at, (ivec2_t){6,22}), &panel.window, Z_UI_BACKGROUND, NULL, 0);
    window_piece(busy ? 4713 : 4712, ivec2_add(at, (ivec2_t){64,19}));
    int seconds = busy ? (queue->time_left_ms + 999) / 1000 : offered_count ?
                  G_ModelProductTrainingTimeMs(offered[popup_choice]) / 1000 : 0;
    char text[16];
    if (seconds > 599) seconds = 599; /* 0x2638c: saturate at 9:59. */
    snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
    window_text(ivec2_add(at, (ivec2_t){92,35}), text);
    int health = producer->max_hp > 0 ? producer->hp * 100 / producer->max_hp : 100;
    if (health < 100)
        V_FillRect((irect_t){at.x + 88, at.y + 5, 19, 10 - health / 10}, 121);
    snprintf(text, sizeof(text), "%d%%", health);
    window_text(ivec2_add(at, (ivec2_t){health < 100 ? 91 : 89,6}), text);
    if (busy) {
        ivec2_t meter = ivec2_add(at, (ivec2_t){53,21});
        window_piece(4710, meter);
        int height = queue->time_ms > 0 ? 37 * (queue->time_ms - queue->time_left_ms) / queue->time_ms : 0;
        irect_t clip = V_GetClip();
        if (height > 0) {
            V_SetClip((irect_t){meter.x, meter.y, 16, height});
            window_piece(4711, meter);
            V_SetClip(clip);
        }
    }
    if (popup_choice < 0 || popup_choice >= offered_count) return;
    int type = offered[popup_choice]->product_type;
    /* 0x12ecb9/0x12eccd: production group to authored name sprite,
     * resolved through ROBOTS.DAT to the weapon types in units.inc. */
    static const int names[] = {803,808,792,791,796,800,802,793,805,787,807,801,799,798,806,804,-1,795,789};
    int t = type - MT_UNITS;
    if (t >= 0 && t < (int)(sizeof(names) / sizeof(*names)) && names[t] >= 787)
        V_DrawSpriteCell(ivec2_add(at, (ivec2_t){3,62}), &panel.window, names[t] - 787, NULL, 0);
    const spritesheet_t *sheet;
    int lump;
    if (state_picture(mobjinfo[type].spawnstate, &sheet, &lump)) {
        irect_t src = sheet->cells[lump].rect;
        irect_t dst = {at.x + 6 + (48 - src.w) / 2, at.y + 22 + (37 - src.h) / 2, src.w, src.h};
        irect_t clip = V_GetClip();
        V_SetClip((irect_t){at.x + 6, at.y + 22, 48, 37});
        R_DrawSprite(sheet, lump, consoleplayer, NULL, &dst, 0, 16);
        V_SetClip(clip);
    }
}

static void on_menu(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action == MA_ACTIVATE && menu->app) M_StartControlPanel(menu->app);
}

static void on_stop(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu; (void)item;
    if (action != MA_ACTIVATE || !popup_for) return;
    if (Z_ProductInProduction(P_MobjById(popup_for))) {
        ticcmd_t cmd = {.order = TC_CANCEL_PRODUCTION, .count = 1, .units = {popup_for}};
        G_QueueTiccmd(&cmd);
    } else P_MobjSetSelected(P_MobjById(popup_for), false);
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
    if (!Z_ProductInProduction(producer) && !G_BuildOrder(producer, offered[popup_choice]->ui_id)) return;
    P_MobjSetSelected(producer, false);
}

/* A button drawn from the panel art: up, pressed and greyed pieces in a
 * row from `piece`. One without a routine stays up. */
static void picture_button(menuitem_t *item, int piece, SDL_Keycode hotkey, int kind, menuroutine_t routine) {
    *item = (menuitem_t){
        .kind = MI_BUTTON, .enabled = routine != NULL, .release = true, .hotkey = hotkey, .id = kind,
        .routine = routine, .disabled_look = true, .opaque = true, .sheet = panel_ok ? &panel.pieces : NULL,
        .fill = panel_ok ? 0 : BUTTON_FILL, .border = panel_ok ? 0 : BUTTON_EDGE,
    };
    for (int state = 0; state < MS_STATES; ++state) {
        item->look[state].cell = piece + (state == MS_PUSHED ? 1 : state == MS_DISABLED && routine ? 2 : 0);
        item->look[state].palette = -1;
    }
}

static void window_button(menuitem_t *item, int native, int step, menuroutine_t routine) {
    *item = (menuitem_t){
        .kind = MI_BUTTON, .enabled = true, .release = true, .step = step, .routine = routine,
        .sheet = panel_ok ? &panel.window : NULL,
    };
    for (int state = 0; state < MS_STATES; ++state)
        item->look[state] = (menulook_t){.cell = Z_UI_WINDOW + native - Z_UI_FIRST + (state == MS_PUSHED), .palette = -1};
}

static void layout(void) {
    memset(items, 0, sizeof(items));
    items[IT_PANEL] = (menuitem_t){.ownerdraw = draw_panel};
    items[IT_BAR] = (menuitem_t){.ownerdraw = draw_bottom};
    items[IT_RADAR] = (menuitem_t){.kind = MI_MINIMAP, .enabled = true, .ownerdraw = draw_radar};
    picture_button(&items[IT_ATTACKED], PC_BUTTON_A, SDLK_F5, KIND_ATTACKED, on_step_kind);
    picture_button(&items[IT_T], PC_BUTTON_T, 0, 0, NULL);
    picture_button(&items[IT_D], PC_BUTTON_D, 0, 0, NULL);
    picture_button(&items[IT_Z], PC_BUTTON_Z, 0, 0, NULL);
    picture_button(&items[IT_ROBOTS], PC_BUTTON_R, SDLK_F1, KIND_ROBOT, on_step_kind);
    picture_button(&items[IT_VEHICLES], PC_BUTTON_V, SDLK_F2, KIND_VEHICLE, on_step_kind);
    picture_button(&items[IT_BUILDINGS], PC_BUTTON_B, SDLK_F3, KIND_BUILDING, on_step_kind);
    picture_button(&items[IT_GUNS], PC_BUTTON_G, SDLK_F4, KIND_GUN, on_step_kind);
    picture_button(&items[IT_MENU], PC_BUTTON_MENU, 0, 0, on_menu);
    if (!panel_ok) {
        static const char *const names[] = {"A", "T", "D", "Z", "R", "V", "B", "G", "MENU"};
        for (int i = IT_ATTACKED; i <= IT_MENU; ++i) {
            items[i].font = font_ok ? &font : NULL;
            items[i].ink = INK;
            items[i].align = MALIGN_CENTER;
            snprintf(items[i].text, sizeof(items[i].text), "%s", names[i - IT_ATTACKED]);
        }
    }
    items[IT_POPUP] = (menuitem_t){.layer = true, .ownerdraw = draw_window};
    window_button(&items[IT_STOP], 4702, 0, on_stop);
    window_button(&items[IT_PREV], 4706, -1, on_step);
    window_button(&items[IT_NEXT], 4708, 1, on_step);
    window_button(&items[IT_START], 4704, 0, on_start);
    for (int i = 0; i < IT_COUNT; ++i) items[i].visible = i < IT_POPUP;
}

/* 0x24ffc: anchor at the building's native top-left minus (96,48).
 * Keep the controls reachable at the edges of our resizable viewport. */
static void place_popup(const menu_t *menu, const mobj_t *producer, int scale) {
    irect_t cells = P_MobjCells(producer);
    fvec2_t a;
    R_MapToScreen(menu->app, &level, (float)cells.x, (float)cells.y, &a.x, &a.y);
    irect_t world = G_WorldViewport(menu->app);
    irect_t view = {world.x / scale, world.y / scale, world.w / scale, world.h / scale};
    int x = (int)a.x / scale - 96, y = (int)a.y / scale - 48;
    if (x + POPUP_W > view.x + view.w) x = view.x + view.w - POPUP_W;
    if (x < view.x) x = view.x;
    if (y + POPUP_H > view.y + view.h) y = view.y + view.h - POPUP_H;
    if (y < view.y) y = view.y;
    items[IT_POPUP].rect = (irect_t){x, y, POPUP_W, POPUP_H};
    items[IT_STOP].rect = (irect_t){x + 67, y + 47, 40, 14};
    items[IT_PREV].rect = (irect_t){x + 50, y + 21, 16, 8};
    items[IT_NEXT].rect = (irect_t){x + 50, y + 64, 16, 8};
    items[IT_START].rect = (irect_t){x + 67, y + 62, 40, 15};
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
    items[IT_STOP].enabled = true;
    items[IT_START].enabled = offered_count > 0;
    items[IT_PREV].enabled = items[IT_NEXT].enabled = !making && offered_count > 0;
    items[IT_PREV].visible = items[IT_NEXT].visible = items[IT_POPUP].visible && !making;
    place_popup(menu, producer, scale);
}

static void refresh(menu_t *menu) {
    int scale = R_UIScale(menu->app);
    isize2_t screen = menu->app ? (isize2_t){menu->app->win.w / scale, menu->app->win.h / scale}
                                : (isize2_t){SCREENWIDTH, SCREENHEIGHT};
    if (panel_ok && hudview.tileset) Z_PanelPalette(&panel, hudview.tileset->palette);
    int side = screen.w - SIDEBAR, bar = screen.h - BOTTOM;
    items[IT_PANEL].rect = (irect_t){side, 0, SIDEBAR, screen.h};
    items[IT_BAR].rect = (irect_t){0, bar, side, BOTTOM};
    int radar_w = PANEL_RADAR_W, radar_h = PANEL_RADAR_H;
    if (level.width > 0 && level.height > 0) {
        if (level.width * radar_h > level.height * radar_w) radar_h = level.height * radar_w / level.width;
        else radar_w = level.width * radar_h / level.height;
    }
    items[IT_RADAR].rect = (irect_t){side + PANEL_RADAR_X + (PANEL_RADAR_W - radar_w) / 2,
        PANEL_RADAR_Y + (PANEL_RADAR_H - radar_h) / 2, radar_w, radar_h};
    items[IT_ATTACKED].rect = (irect_t){side + 6, 6, 24, 20};
    for (int i = 0; i < 3; ++i) items[IT_T + i].rect = (irect_t){side + 6 + 30 * i, PANEL_BUTTON_Y, 24, 20};
    for (int i = 0; i < 4; ++i) items[IT_ROBOTS + i].rect = (irect_t){6 + 30 * i, bar + BAR_BUTTON_Y, 24, 20};
    items[IT_MENU].rect = (irect_t){BAR_MENU_X, bar + BAR_BUTTON_Y, 56, 20};
    refresh_popup(menu, scale);
}

/* ── the engine's entry points ────────────────────────────────────────── */

irect_t G_WorldViewport(const app_t *app) {
    int scale = R_UIScale(app);
    int w = app ? app->win.w - SIDEBAR * scale : 1, h = app ? app->win.h - BOTTOM * scale : 1;
    return (irect_t){0, 0, w > 0 ? w : 1, h > 0 ? h : 1};
}

menu_t *G_InitHUD(app_t *app, const char *data_root) {
    if (!font_ok) font_ok = HU_LoadFont(data_root, &font);
    if (!panel_ok) panel_ok = Z_LoadPanel(data_root, &panel);
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
    if (panel_ok) Z_FreePanel(&panel);
    font_ok = panel_ok = false;
    laid_out = false;
}
