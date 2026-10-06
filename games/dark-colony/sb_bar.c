#define _DEFAULT_SOURCE
#include "engine.h"
#include "dark-colony.h"
#include "info.h"
#include "gamestat.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The HUD is the MAINE screen script. Its controls keep their native IDs,
 * after the chrome that draws under them and before the items that have no
 * control of their own. */
enum {
    COLUMN_REST, COLUMN, STRIP, FIRST_CONTROL,
    NUMCONTROLS = 207,
    MINIMAP = FIRST_CONTROL + NUMCONTROLS, STATUS, LABEL, MESSAGE, DAYS, ENTER, KP_ENTER, NUMITEMS
};

/* MAINE text fields: in_text 79 (the pointer's label above Build), 148 (the
 * message strip), 234 (days, set by 0x437824) and scount 75 (money). */
enum { FIELD_LABEL, FIELD_MESSAGE, FIELD_DAYS, FIELD_MONEY, NUMFIELDS };
static const int field_ids[NUMFIELDS] = {79, 148, 234, 75};
static const int field_items[NUMFIELDS] = {LABEL, MESSAGE, DAYS, STATUS};

typedef struct {
    irect_t rect;
    int palette;  /* PALETTE.RMP row: intens * 8 + remap */
    int picture;  /* scount: the frame of digit 0 */
    bool centered;
} hudfield_t;

typedef StaticProductDefinition ProductButton;

typedef struct {
    menuitem_t items[NUMITEMS];
    menu_t menu;
    char labels[NUMCONTROLS][40]; /* textmsg: shown while the pointer is on the control */
    hudfield_t fields[NUMFIELDS];
    bitmapfont_t font;
    spritesheet_t background;
    int tab;
    bool allies;
    uint8_t recipients;
    waypoints_t waypoints;
    const mobj_t *selected;
    bool product_mode;
} dc_hud_t;

static dc_hud_t *hud;

static menuitem_t *control(dc_hud_t *h, int id) {
    return &h->items[FIRST_CONTROL + id];
}

/* The sidebar column keeps its place at the top-right corner of a larger
 * screen; only the message strip below the world follows the bottom edge. */
static int anchor_at(irect_t rect) {
    return rect.x >= 516 ? MANCHOR_RIGHT : rect.y >= 455 ? MANCHOR_BOTTOM : 0;
}

static irect_t placed(const menu_t *menu, irect_t rect) {
    return M_MenuItemRect(menu, &(menuitem_t){.rect = rect, .anchor = anchor_at(rect)});
}

irect_t G_WorldViewport(const app_t *app) {
    int scale = R_UIScale(app);
    int w = app ? app->win.w - 124 * scale : 0, h = app ? app->win.h - 25 * scale : 0;
    return (irect_t){0, 0, w > 0 ? w : 1, h > 0 ? h : 1};
}

static void dc_ui_fill(irect_t rect, uint32_t argb) {
    V_FillRect(rect, V_NearestIndex(argb));
}

static void dc_ui_stroke(irect_t rect, uint32_t argb) {
    V_DrawRectOutline(rect, V_NearestIndex(argb));
}

static const mobj_t *dc_first_selected_unit(mobj_t *const *units, int unit_count) {
    if (!units) return NULL;
    for (int i = 0; i < unit_count; ++i) {
        if (P_MobjIsSelected(units[i]) && !units[i]->remove) return units[i];
    }
    return NULL;
}

static bool dc_selected_unit_is_player_building(const mobj_t *selected) {
    return selected && selected->owner == consoleplayer && !selected->remove && selected->hp > 0 &&
        !(selected->traits & MF_MOBILE) &&
        selected->type_id >= MT_EXCOPOD;
}

static int dc_available_products(mobj_t *const *units, int unit_count, int tab,
                                  const ProductButton *out[16]) {
    static ProductButton products[64];
    int count = 0;
    bool research = tab == 1;
    int source_count = G_ModelGetProducts(NULL, consoleplayer, products, 64);
    for (int i = 0; i < source_count && count < 16; ++i) {
        const ProductButton *product = &products[i];
        if ((product->product_class == RTS_PRODUCT_UPGRADE) != research) continue;
        if (!level.purchases[consoleplayer][product->row_id].selected &&
            !G_ModelProductAvailableForUnits(units, unit_count, product)) continue;
        if (product->product_class == RTS_PRODUCT_BUILDING) {
            bool exists = false;
            for (int j = 0; j < unit_count; ++j)
                if (units[j]->owner == consoleplayer && !units[j]->remove && units[j]->hp > 0 &&
                    DC_ProductActorMatches(units[j]->type_id, G_ModelActorIdForProduct(product))) exists = true;
            if (exists) continue;
        }
        out[count++] = product;
    }
    return count;
}

static const char *dc_selected_building_label(const mobj_t *selected) {
    if (!selected) return "";
    switch (selected->type_id) {
    case MT_EXCOPOD: return "Exo-Ctr";
    case MT_BRRKPOD: return "Barracks";
    case MT_ROBOPOD: return "Robo-Ftr";
    case MT_ROBOPOD2: return "Robo-Ftr+";
    case MT_SCNCPOD: return "Sci-Pod";
    case MT_SCNCPOD2: return "Sci-Pod+";
    case MT_RSCHPOD: return "Rsch-Bay";
    case MT_ALIEN_MINDHIVE: return "Mind-Hive";
    case MT_ALIEN_WARHIVE: return "War. Fold";
    case MT_ALIEN_BRDRHIVE: return "Gene-Sac";
    case MT_ALIEN_BRDRHIVE2: return "Gene-Upgrd";
    case MT_ALIEN_MINDHIVE2: return "Breed-Pod";
    case MT_ALIEN_MINDHIVE3: return "Pod-Upgrd";
    case MT_ALIEN_RSCHIVE: return "Neur-Hive";
    default: return "";
    }
}

static int dc_commands(mobj_t *const *units, int count, int ids[6]) {
    const int primary[] = {-1,138,139,140,141,142,142,37};
    const int secondary[] = {-1,144,145,146,197,143,198};
    int first = 0, second = 0;
    ids[0] = 150; ids[1] = 33; ids[2] = 35; ids[3] = 36;
    for (int i = 0; i < count; ++i) {
        const mobj_t *actor = units[i];
        if (!P_MobjIsSelected(actor) || actor->owner != consoleplayer || actor->remove ||
            actor->hp <= 0 || actor->native_type_id >= GAMESTAT_UNIT_COUNT) continue;
        const DcGamestatUnit *type = &dc_gamestat_units[actor->native_type_id];
        unsigned flags = type->values[27]; /* DC type +0x104. */
        if (!(flags & 0x80) || ((flags & 0x40) && actor->ability_charge > 32) || actor->ability_charge == 255) {
            int ability = flags & 0x3f;
            if (ability) first = !first || first == ability ? ability : 7;
        }
        flags = type->values[28]; /* DC type +0x108. */
        if (!(flags & 0x80) || actor->ability_charge == 255) {
            int ability = flags & 0x3f;
            if (ability) second = !second || second == ability ? ability : 5;
        }
    }
    int n = 4;
    if (first > 0 && first < 8) ids[n++] = primary[first];
    if (second > 0 && second < 7) ids[n++] = secondary[second];
    return n;
}

/* ── routines ───────────────────────────────────────────────────────────── */

/* Returns whether the route still waits for points: a refused order keeps
 * them for another try. */
static bool finish_waypoints(dc_hud_t *h) {
    h->waypoints.mode = WP_LOOP;
    if (h->waypoints.count && !G_PathOrder(hudview.units, hudview.unit_count, &h->waypoints))
        return true;
    h->waypoints = (waypoints_t){0};
    return false;
}

static void command(menu_t *menu, menuitem_t *item, menuaction_t action) {
    dc_hud_t *h = menu->owner;
    int id = item->id;
    if (action == MA_TARGET) {
        /* Waypoints: seven at most; clicking the first or last again ends
         * the route. Other orders keep waiting until the right button. */
        if (id == 36) {
            cell_t cell = R_ScreenToMapGrid(menu->app, &level, menu->cursor.x, menu->cursor.y);
            int count = h->waypoints.count;
            if (count == 7 || (count && (ivec2_equal(cell, h->waypoints.points[0]) ||
                                         ivec2_equal(cell, h->waypoints.points[count - 1])))) {
                if (!finish_waypoints(h)) return;
            } else h->waypoints.points[h->waypoints.count++] = cell;
        }
        M_MenuTarget(menu, item);
        return;
    }
    if (action == MA_CANCEL) {
        if (id == 36 && finish_waypoints(h)) M_MenuTarget(menu, item);
        return;
    }
    if (action != MA_ACTIVATE) return;
    if (id == 204) {
        ticcmd_t chat = {.order = TC_CHAT, .target = h->recipients | (1u << consoleplayer)};
        snprintf(chat.text, sizeof(chat.text), "%s", item->text);
        if (!chat.text[0] || G_QueueTiccmd(&chat)) M_MenuEdit(menu, NULL);
        return;
    }
    switch (id) {
    case 0: case 1: case 2:
        h->tab = id;
        h->allies = false;
        M_MenuTarget(menu, NULL);
        break;
    case 19: G_QueueTiccmd(&(ticcmd_t){.order = TC_SUBMIT}); break;
    case 62: DC_OpenQuitDialog(menu->app); break;
    case 64: DC_OpenOptionsDialog(menu->app); break;
    case 63: DC_OpenSave(menu->app); break;
    case 151: h->allies = true; break;
    case 202: DC_OpenObjectives(menu->app); break;
    case 196: G_QueueTiccmd(&(ticcmd_t){.order = TC_PAUSE}); break;
    case 150: case 138:
        G_SelectedTiccmd(TC_STOP, hudview.units, hudview.unit_count, (fvec2_t){0}, 0);
        break;
    case 33: case 35:
        G_SelectedTiccmd(TC_MODE, hudview.units, hudview.unit_count, (fvec2_t){0}, id == 33);
        break;
    case 139: case 140: case 37:
        G_SelectedTiccmd(TC_DEPLOY, hudview.units, hudview.unit_count, (fvec2_t){0}, 0);
        break;
    default:
        if (id >= 154 && id <= 195) {
            int row = (id - 154) / 6, column = (id - 154) % 6;
            int player = row >= consoleplayer ? row + 1 : row;
            if (!DC_PlayerActive(player)) break;
            if (column < 2) {
                bool offer = !(level.alliance_offers[column][consoleplayer] & (1u << player));
                G_QueueTiccmd(&(ticcmd_t){.order = column ? TC_SHARE_SIGHT : TC_ALLY,
                    .target = player, .product = offer});
            } else if (column == 2) h->recipients ^= 1u << player;
            else if (column == 4) G_QueueTiccmd(&(ticcmd_t){.order = TC_GIVE, .target = player});
            break;
        }
        M_MenuTarget(menu, item);
        h->waypoints = (waypoints_t){0};
    }
}

/* Enter: Shift (or the Allies page) opens the chat line, a route being drawn
 * ends, otherwise the selection deploys. */
static void enter(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    dc_hud_t *h = menu->owner;
    if (action != MA_ACTIVATE) return;
    menuitem_t *chat = control(h, 204);
    if (h->allies || (menu->keymod & KMOD_SHIFT)) {
        chat->visible = chat->enabled = true;
        chat->text[0] = '\0';
        M_MenuEdit(menu, chat);
    } else if (menu->target == control(h, 36)) {
        if (!finish_waypoints(h)) M_MenuTarget(menu, NULL);
    } else G_SelectedTiccmd(TC_DEPLOY, hudview.units, hudview.unit_count, (fvec2_t){0}, 0);
}

/* A left click reserves one of the product; a right click gives one back.
 * Retail then needs Build (control 19) or Space; BUILD_IMMEDIATELY submits
 * on the click. Every netplay peer must be built with the same choice. */
static void purchase(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (action != MA_ACTIVATE && action != MA_SECONDARY) return;
    G_QueueTiccmd(&(ticcmd_t){.order = TC_PURCHASE,
        .product = item->id, .target = action == MA_SECONDARY});
#ifdef BUILD_IMMEDIATELY
    if (action == MA_ACTIVATE) G_QueueTiccmd(&(ticcmd_t){.order = TC_SUBMIT});
#endif
}

/* ── drawing ────────────────────────────────────────────────────────────── */

static void dc_ui_draw_minimap(app_t *app, const level_t *map, mobj_t *const *units, int unit_count,
                               irect_t rect) {
    if (!app || !map || map->width <= 0 || map->height <= 0) return;
    dc_ui_fill(rect, 0xff040809u);
    irect_t clip = { rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6 };
    if (clip.w <= 0 || clip.h <= 0) return;
    for (int py = 0; py < clip.h; ++py) {
        int screen_y = py * map->height / clip.h;
        int gy = L_ScreenY(map, screen_y);
        for (int px = 0; px < clip.w; ++px) {
            int gx = px * map->width / clip.w;
            uint32_t color = map->cell_colors ? map->cell_colors[L_Index(map, gx, gy)] : 0xff202820u;
            uint8_t r = (uint8_t)(color >> 16);
            uint8_t g = (uint8_t)(color >> 8);
            uint8_t b = (uint8_t)color;
            int light = P_SightBrightness(map, (ivec2_t){gx, gy});
            V_DrawPoint((ivec2_t){clip.x + px, clip.y + py},
                        V_NearestIndex(0xff000000u |
                                       ((uint32_t)(r * light / 16) << 16) |
                                       ((uint32_t)(g * light / 16) << 8) |
                                       (uint32_t)(b * light / 16)));
        }
    }
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!P_SightBrightness(map, vent->cell)) continue;
        int x = clip.x + vent->cell.x * clip.w / map->width;
        int y = clip.y + (int)(L_ScreenY(map, vent->cell.y) * clip.h / map->height);
        irect_t dot = { x - 1, y - 1, 3, 3 };
        dc_ui_fill(dot, vent->active ? 0xff59e2b8u : 0xff445654u);
    }
    for (int i = 0; i < unit_count; ++i) {
        fvec2_t position = fixed3_xy_to_fvec2(units[i]->core.position);
        if (!P_VisibleToPlayer(units[i]) || units[i]->remove ||
            position.x < 0.0f || position.y < 0.0f) continue;
        int x = clip.x + (int)(position.x * (float)clip.w / (float)map->width);
        int y = clip.y + (int)(L_ScreenYF(map, position.y) *
                                (float)clip.h / (float)map->height);
        irect_t dot = { x - 1, y - 1, 2, 2 };
        dc_ui_fill(dot, units[i]->owner == consoleplayer ? 0xffdad687u : 0xffcc4448u);
    }
    int world_right = G_WorldViewportWidth(app);
    cell_t tl = R_ScreenToGrid(app, 0, 0);
    cell_t br = R_ScreenToGrid(app, world_right, app->win.h);
    int vx = clip.x + tl.x * clip.w / map->width;
    int vy = clip.y + tl.y * clip.h / map->height;
    int vw = (br.x - tl.x) * clip.w / map->width;
    int vh = (br.y - tl.y) * clip.h / map->height;
    if (vw < 3) vw = 3;
    if (vh < 3) vh = 3;
    irect_t view = { vx, vy, vw, vh };
    dc_ui_stroke(view, 0xdca4eccbu);
    dc_ui_stroke(rect, 0xff485b58u);
}

/* 0x4097f8: the click area is (519,7), 96x84; the picture sits 3 pixels in. */
static void draw_minimap(const menu_t *menu, const menuitem_t *item, irect_t r) {
    (void)item;
    dc_ui_draw_minimap(menu->app, &level, hudview.units, hudview.unit_count,
                       (irect_t){r.x + 3, r.y, r.w - 4, r.h - 4});
}

/* The money (scount 75: right-aligned digit pictures) and the day dial. */
static void draw_status(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)item; (void)rect;
    const dc_hud_t *h = menu->owner;
    const spritecache_t *images = hudview.sprites ? hudview.sprites->ui : NULL;
    char text[32];
    const hudfield_t *money = &h->fields[FIELD_MONEY];
    irect_t field = placed(menu, money->rect);
    const spritesheet_t *buttons = R_CacheLookup(images, "INTRFACE/MAINBUT.SPR");
    int resources = level.player_resources[consoleplayer][0];
    snprintf(text, sizeof(text), "%d", resources < 0 ? 0 : resources);
    if (buttons && money->picture >= 0 && money->picture + 9 < buttons->numlumps) {
        int x = field.x + field.w;
        for (int i = (int)strlen(text) - 1; i >= 0; --i) {
            int frame = money->picture + text[i] - '0';
            irect_t src = buttons->cells[frame].rect;
            x -= src.w;
            if (x < field.x) break;
            irect_t dst = {x, field.y, src.w, src.h};
            R_DrawSprite(buttons, frame, money->palette, &src, &dst, 0, 16);
        }
    }
    const spritesheet_t *dial = R_CacheLookup(images, "SPRITES/CLOC.SPR");
    if (dial && dial->numlumps >= 2 && level.daylight.duration > 0) {
        int half = dial->numlumps / 2;
        int frame = (int)((int64_t)level.daylight.tics * half / level.daylight.duration);
        if (frame >= half) frame = half - 1;
        frame += level.daylight.phase * half;
        irect_t src = dial->cells[frame].rect;
        /* 0x4377e3–0x437806 cancels the SPR displacement at (608,450). */
        irect_t dst = placed(menu, (irect_t){608, 450, src.w, src.h});
        R_DrawSprite(dial, frame, -1, &src, &dst, 0, 16);
    }
}

/* ── the table ──────────────────────────────────────────────────────────── */

/* Show the controls of the open tab and of the selection, and the text of
 * the label, message and day fields. */
static void refresh(menu_t *menu) {
    static const int options[] = {62, 63, 64, 151, 196, 202};
    dc_hud_t *h = menu->owner;
    mobj_t *const *units = hudview.units;
    int unit_count = hudview.unit_count;
    const ProductButton *products[16] = {0};
    int product_count = dc_available_products(units, unit_count, h->tab, products);
    h->selected = dc_first_selected_unit(units, unit_count);
    h->product_mode = h->tab == 1 || (h->tab == 0 &&
        (!h->selected || dc_selected_unit_is_player_building(h->selected)));
    for (int id = 3; id < NUMCONTROLS; ++id) control(h, id)->visible = false;
    control(h, 3 + h->tab)->visible = true; /* The tab strip's title picture. */
    control(h, 19)->visible = true;
    if (chat_text.count) {
        control(h, 203)->visible = true;
        snprintf(control(h, 203)->text, sizeof(control(h, 203)->text), "%.72s",
                 chat_text.messages[chat_text.count - 1].text);
    }
    control(h, 204)->visible = menu->editing == control(h, 204);
    if (h->allies) {
        control(h, 152)->visible = true;
        for (int row = 0; row < 7; ++row) {
            int player = row >= consoleplayer ? row + 1 : row;
            if (!DC_PlayerActive(player)) continue;
            int base = 154 + row * 6;
            for (int col = 0; col < 6; ++col) control(h, base + col)->visible = true;
            snprintf(control(h, base + 3)->text, sizeof(control(h, base + 3)->text),
                     "%.16s", DC_PlayerName(player));
            for (int col = 0; col < 2; ++col) {
                bool sent = level.alliance_offers[col][consoleplayer] & (1u << player);
                bool received = level.alliance_offers[col][player] & (1u << consoleplayer);
                int cell = sent && received ? 119 : sent || received ? 120 : 124;
                for (int state = 0; state < MS_STATES; ++state)
                    control(h, base + col)->look[state].cell = cell;
            }
            control(h, base + 2)->value = !!(h->recipients & (1u << player));
            for (int state = 0; state < MS_STATES; ++state)
                control(h, base + 5)->look[state].palette = 16 * 8 +
                    (level.player_teams ? level.player_colors[player] : player);
        }
    } else if (h->tab == 2) {
        for (int i = 0; i < 6; ++i) control(h, options[i])->visible = true;
    } else if (h->product_mode) {
        int money = level.player_resources[consoleplayer][0];
        for (int i = 0; i < product_count; ++i) {
            menuitem_t *item = control(h, products[i]->ui_id);
            int quantity = level.purchases[consoleplayer][products[i]->row_id].selected;
            item->visible = true;
            /* Engine behaviour: a product the player cannot pay for is dark. */
            item->light = money < products[i]->cost ? 9 : 16;
            for (int state = 0; state < MS_STATES; ++state)
                item->look[state].cell = products[i]->icon_frame;
            if (quantity) snprintf(item->text, sizeof(item->text), "%d", quantity);
            else item->text[0] = '\0';
        }
    } else {
        int ids[6];
        int count = dc_commands(units, unit_count, ids);
        for (int i = 0; i < count; ++i) control(h, ids[i])->visible = true;
    }
    /* A hotkey works whether or not its control is on the open tab. */
    for (int id = 0; id < NUMCONTROLS; ++id) {
        menuitem_t *item = control(h, id);
        item->enabled = item->kind != MI_STATIC && (item->visible || item->hotkey);
    }
    /* The level's image cache owns the button sheet. */
    const spritesheet_t *buttons = hudview.sprites ?
        R_CacheLookup(hudview.sprites->ui, "INTRFACE/MAINBUT.SPR") : NULL;
    if (buttons)
        for (int id = 0; id < NUMCONTROLS; ++id) control(h, id)->sheet = buttons;

    /* in_text 79, above Build: the control last under the pointer, until
     * the pointer moves, else what the selected building is doing. */
    char *label = h->items[LABEL].text;
    const menuitem_t *hover = menu->itemOn >= 0 ? &menu->items[menu->itemOn] : NULL;
    label[0] = '\0';
    if (hover && hover->tooltip && *hover->tooltip) {
        snprintf(label, 40, "%s", hover->tooltip);
    } else if (h->product_mode) {
        const production_t *making = h->selected ? h->selected->production : NULL;
        if (making && making->queue_count > 0 && making->time_ms > 0) {
            int percent = (making->time_ms - making->time_left_ms) * 100 / making->time_ms;
            snprintf(label, 40, "Training x%d %d%%", making->queue_count,
                     percent < 0 ? 0 : percent > 100 ? 100 : percent);
        } else {
            snprintf(label, 40, "%s", dc_selected_building_label(h->selected));
        }
    }
    /* in_text 148, the strip under the world: the newest HUD message. */
    const hudtext_t *messages = hudview.messages;
    char *message = h->items[MESSAGE].text;
    if (messages && messages->count > 0)
        snprintf(message, 62, "%s", messages->messages[messages->count - 1].text);
    else message[0] = '\0';
    /* in_text 234: days since the start, set by 0x437824. */
    uint64_t clock = (uint64_t)leveltime * 1000 / (WORLD_CLOCK_MS * RTS_TICRATE);
    int days = level.daylight.duration > 0 ?
        (int)(clock / (uint64_t)level.daylight.duration / 2u) : 0;
    snprintf(h->items[DAYS].text, sizeof(h->items[DAYS].text), "%03d", days > 999 ? 999 : days);
}

/* The font draws through PALETTE.RMP's (intensity*8+remap) maps, like
 * MAINBUT, and its colours are matched into the level's palette. */
static bool load_font(dc_hud_t *h, const char *data_root) {
    blob_t rmp;
    if (!HU_LoadFont(data_root, &h->font) ||
        !W_ReadFile(M_va("%s/PALETTE.RMP", data_root), &rmp)) return false;
    spritepalettemap_t *maps = rmp.size >= 256 * 256 ? calloc(256, sizeof(*maps)) : NULL;
    for (int i = 0; maps && i < 256; ++i) {
        maps[i].id = i;
        memcpy(maps[i].indices, rmp.bytes + i * 256, 256);
    }
    W_FreeFile(&rmp);
    if (!maps) return false;
    free(h->font.sprite.palette_maps);
    h->font.sprite.palette_maps = maps;
    h->font.sprite.palette_map_count = 256;
    h->font.own_palette = true;
    return true;
}

static bool load_script(dc_hud_t *h, const char *data_root) {
    char path[1024], line[512];
    M_PathJoin(path, sizeof(path), data_root, "INTRFACE/MAINE");
    FILE *file = fopen(path, "r");
    if (!file) return false;
    int bright_pushed = 0, bright_highlight = 0;
    /* The brightness lines may follow a control that uses them. */
    typedef struct { int normal, pushed, textpalette; } controlframes_t;
    controlframes_t frames[NUMCONTROLS];
    for (int id = 0; id < NUMCONTROLS; ++id)
        frames[id] = (controlframes_t){-1, -1, -1};
    while (fgets(line, sizeof(line), file)) {
        char kind[16], label[40];
        int id, description;
        irect_t rect;
        sscanf(line, "bright_pushed %d", &bright_pushed);
        sscanf(line, "bright_highlight %d", &bright_highlight);
        if (sscanf(line, "textmsg %d %39[^\r\n]", &id, label) == 2) {
            if (id < 0 || id >= NUMCONTROLS) continue;
            size_t length = strlen(label);
            while (length > 0 && isspace((unsigned char)label[length - 1])) label[--length] = '\0';
            strcpy(h->labels[id], label);
            continue;
        }
        int normal = -1, pushed = -1;
        if (sscanf(line, "%15s %d %d %d %d %d %d %d %d", kind, &id, &description,
                   &rect.x, &rect.y, &rect.w, &rect.h, &normal, &pushed) < 7) continue;
        if (!strcmp(kind, "in_text") || !strcmp(kind, "scount")) {
            bool field = false;
            for (int f = 0; f < NUMFIELDS; ++f) {
                if (field_ids[f] != id) continue;
                field = true;
                const char *remap = strstr(line, "remap "), *intens = strstr(line, "intens ");
                int r = 7, light = 16;
                if (remap) sscanf(remap, "remap %d", &r);
                if (intens) sscanf(intens, "intens %d", &light);
                /* Retail draws in_text 79 (remap 2) and 234 (remap 0) in the
                 * default remap 7's cyan; the path that drops MAINE's text
                 * remap is not traced, so the screenshots decide. */
                if (kind[0] == 'i') r = 7;
                /* in_text sizes count characters and lines. */
                if (kind[0] == 'i') {
                    rect.w *= h->font.glyph_size.w + 1;
                    rect.h *= h->font.line_h;
                }
                h->fields[f] = (hudfield_t){
                    .rect = rect,
                    .palette = (light > 31 ? 31 : light) * 8 + r,
                    .picture = kind[0] == 's' ? normal : -1,
                    .centered = strstr(line, "align centre") != NULL,
                };
            }
            if (field) continue;
        }
        if (id < 0 || id >= NUMCONTROLS) continue;
        bool count = !strcmp(kind, "count"), check = !strcmp(kind, "checkb");
        bool text = !strcmp(kind, "in_text");
        bool picture = !strcmp(kind, "picture");
        if (!count && !check && !picture && !text && strcmp(kind, "pushb")) continue;
        if (text) {
            rect.w *= h->font.glyph_size.w + 1;
            rect.h *= h->font.line_h;
            int light = 16;
            const char *intens = strstr(line, "intens ");
            if (intens) sscanf(intens, "intens %d", &light);
            /* Like in_text 79/234, the supplied retail Allies screenshot
             * uses default remap 7's cyan, despite the script's remap 4. */
            frames[id].textpalette = (light > 31 ? 31 : light) * 8 + 7;
        }
        menuitem_t *item = control(h, id);
        *item = (menuitem_t){
            .kind = picture || text ? MI_STATIC : check ? MI_CHECK : MI_BUTTON,
            .id = id,
            .rect = rect,
            .anchor = anchor_at(rect),
            .routine = count ? purchase : command,
            .font = &h->font,
            .ink = text ? 0 : 0xffe7c25eu,
            .tooltip = description >= 0 && description < NUMCONTROLS ? h->labels[description] : NULL,
        };
        frames[id].normal = text ? -1 : normal;
        frames[id].pushed = text ? -1 : pushed;
        /* count N ... offset X Y: where the reserved quantity is written. */
        const char *offset = count ? strstr(line, "offset") : NULL;
        if (offset) sscanf(offset, "offset %d %d", &item->inset.x, &item->inset.y);
    }
    bool ok = !ferror(file);
    fclose(file);
    for (int id = 0; id < NUMCONTROLS; ++id)
        DC_ControlLooks(control(h, id), frames[id].normal, frames[id].pushed, 7,
                            bright_pushed, bright_highlight);
    for (int id = 0; id < NUMCONTROLS; ++id)
        if (frames[id].textpalette >= 0)
            for (int state = 0; state < MS_STATES; ++state)
                control(h, id)->look[state].palette = frames[id].textpalette;
    control(h, 204)->kind = MI_TEXTFIELD;
    control(h, 204)->maxchars = 72;
    return ok;
}

menu_t *G_InitHUD(app_t *app, const char *data_root) {
    if (!app || !data_root) return NULL;
    G_ShutdownHUD();
    dc_hud_t *h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    hud = h;
    h->recipients = UINT8_MAX; /* 0x41d391..0x41d3a8: all seven chat checks start set. */
    char path[1024];
    M_PathJoin(path, sizeof(path), data_root, "INTRFACE/INTRFACE.GIF");
    if (!load_font(h, data_root) || !W_LoadGIFTexture(path, &h->background) ||
        !load_script(h, data_root)) {
        fprintf(stderr, "warning: failed to load the Dark Colony HUD\n");
        G_ShutdownHUD();
        return NULL;
    }
    h->menu = (menu_t){.items = h->items, .numitems = NUMITEMS, .size = {640, 480},
                       .itemOn = -1, .app = app, .refresh = refresh, .owner = h};
    /* The sidebar column keeps its 480 rows at the top-right corner; a taller
     * screen is black under it, not world. */
    h->items[COLUMN_REST] = (menuitem_t){.visible = true, .fill = 0xff000000u,
        .rect = {516, 480, 124, 0}, .anchor = MANCHOR_RIGHT | MANCHOR_GROW};
    h->items[COLUMN] = (menuitem_t){.visible = true, .opaque = true, .rect = {516, 0, 124, 480},
        .anchor = MANCHOR_RIGHT, .sheet = &h->background,
        .look = {{.part = {516, 0, 124, 480}, .palette = -1}}};
    h->items[STRIP] = (menuitem_t){.visible = true, .opaque = true, .rect = {0, 455, 516, 25},
        .anchor = MANCHOR_BOTTOM, .sheet = &h->background,
        .look = {{.part = {0, 455, 516, 25}, .palette = -1}}};
    h->items[MINIMAP] = (menuitem_t){.kind = MI_MINIMAP, .visible = true, .enabled = true,
        .rect = {519, 7, 96, 84}, .anchor = MANCHOR_RIGHT, .ownerdraw = draw_minimap};
    h->items[STATUS] = (menuitem_t){.visible = true, .rect = {524, 456, 72, 17},
        .anchor = MANCHOR_RIGHT, .ownerdraw = draw_status};
    for (int f = 0; f < NUMFIELDS; ++f) {
        if (field_items[f] == STATUS) continue;
        const hudfield_t *field = &h->fields[f];
        menuitem_t *item = &h->items[field_items[f]];
        *item = (menuitem_t){.visible = true, .rect = field->rect, .anchor = anchor_at(field->rect),
            .font = &h->font, .align = field->centered ? MALIGN_HCENTER : 0};
        for (int state = 0; state < MS_STATES; ++state)
            item->look[state] = (menulook_t){.cell = -1, .palette = field->palette};
    }
    h->items[ENTER] = (menuitem_t){.kind = MI_BUTTON, .enabled = true, .quiet = true,
        .hotkey = SDLK_RETURN, .routine = enter};
    h->items[KP_ENTER] = h->items[ENTER];
    h->items[KP_ENTER].hotkey = SDLK_KP_ENTER;
    for (int id = 0; id < 3; ++id) control(h, id)->visible = true;
    static const struct { int id; SDL_Keycode key; } hotkeys[] = {
        {150, SDLK_s}, {33, SDLK_m}, {35, SDLK_a}, {36, SDLK_w}, {19, SDLK_SPACE}, {196, SDLK_t},
        {62, SDLK_q}, {63, SDLK_F11}, {64, SDLK_o}, {202, SDLK_j},
    };
    for (size_t i = 0; i < sizeof(hotkeys) / sizeof(*hotkeys); ++i)
        control(h, hotkeys[i].id)->hotkey = hotkeys[i].key;
    /* Move Only and Move & Attack are one choice; units start in the second. */
    control(h, 33)->group = control(h, 35)->group = 1;
    control(h, 35)->value = 1;
    return &h->menu;
}

void G_ShutdownHUD(void) {
    hudview = (hudview_t){0};
    if (!hud) return;
    M_MenuEdit(&hud->menu, NULL);
    R_FreeSprite(&hud->background);
    HU_FreeFont(&hud->font);
    free(hud);
    hud = NULL;
}
