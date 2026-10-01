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
    MINIMAP = FIRST_CONTROL + NUMCONTROLS, STATUS, LABEL, MESSAGE, NUMITEMS
};

/* MAINE text fields: in_text 79 (the pointer's label above Build), 148 (the
 * message strip), 234 (days, set by 0x437824) and scount 75 (money). */
enum { FIELD_LABEL, FIELD_MESSAGE, FIELD_DAYS, FIELD_MONEY, NUMFIELDS };
static const int field_ids[NUMFIELDS] = {79, 148, 234, 75};

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
    int targeting; /* control whose order waits for a target on the map */
    waypoints_t waypoints;
    /* What the routines and ownerdraws act on, set by each call. */
    app_t *app;
    const level_t *map;
    mobj_t *const *units;
    int unit_count;
    const spritecache_t *images;
    const hudtext_t *messages;
    const mobj_t *selected;
    bool product_mode;
} dc_hud_t;

static menuitem_t *control(dc_hud_t *hud, int id) {
    return &hud->items[FIRST_CONTROL + id];
}

static irect_t ui_rect(const app_t *app, int x, int y, int w, int h) {
    int win_w = app && app->win.w > 0 ? app->win.w : 640;
    int win_h = app && app->win.h > 0 ? app->win.h : 480;
    /* The sidebar column keeps its 480-pixel background at the top-right
     * corner; only the message strip below the world follows the bottom edge. */
    irect_t r = {
        x >= 516 ? win_w - (640 - x) : x,
        x < 516 && y >= 455 ? win_h - (480 - y) : y,
        w,
        h,
    };
    if (r.w < 1 && w > 0) r.w = 1;
    if (r.h < 1 && h > 0) r.h = 1;
    return r;
}

int DC_SB_WorldViewportWidth(const app_t *app) {
    if (!app) return 0;
    int w = app->win.w - 124;
    return w > 0 ? w : 1;
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

static void finish_waypoints(dc_hud_t *hud) {
    hud->waypoints.mode = WP_LOOP;
    if (hud->waypoints.count && !G_PathOrder(hud->units, hud->unit_count, &hud->waypoints)) return;
    hud->waypoints = (waypoints_t){0};
    hud->targeting = 0;
}

static void command(menu_t *menu, menuitem_t *item, menuaction_t action) {
    dc_hud_t *hud = menu->owner;
    if (action != MA_ACTIVATE) return;
    int id = (int)(item - hud->items) - FIRST_CONTROL;
    switch (id) {
    case 0: case 1: case 2:
        hud->tab = id;
        hud->targeting = 0;
        break;
    case 19: G_QueueTiccmd(&(ticcmd_t){.order = TC_SUBMIT}); break;
    case 62: DC_OpenQuitDialog(hud->app); break;
    case 64: DC_OpenOptionsDialog(hud->app); break;
    case 196: G_QueueTiccmd(&(ticcmd_t){.order = TC_PAUSE}); break;
    case 63: case 151: case 202: break;
    case 150: case 138:
        G_SelectedTiccmd(TC_STOP, hud->units, hud->unit_count, (fvec2_t){0}, 0);
        break;
    case 33: case 35:
        G_SelectedTiccmd(TC_MODE, hud->units, hud->unit_count, (fvec2_t){0}, id == 33);
        break;
    case 139: case 140: case 37:
        G_SelectedTiccmd(TC_DEPLOY, hud->units, hud->unit_count, (fvec2_t){0}, 0);
        break;
    default:
        hud->targeting = id;
        hud->waypoints = (waypoints_t){0};
    }
}

/* A left click reserves one of the product; a right click gives one back.
 * Retail then needs Build (control 19) or Space; BUILD_IMMEDIATELY submits
 * on the click. Every netplay peer must be built with the same choice. */
static void purchase(menu_t *menu, menuitem_t *item, menuaction_t action) {
    dc_hud_t *hud = menu->owner;
    if (action != MA_ACTIVATE && action != MA_SECONDARY) return;
    G_QueueTiccmd(&(ticcmd_t){.order = TC_PURCHASE,
        .product = (int)(item - hud->items) - FIRST_CONTROL, .target = action == MA_SECONDARY});
#ifdef BUILD_IMMEDIATELY
    if (action == MA_ACTIVATE) G_QueueTiccmd(&(ticcmd_t){.order = TC_SUBMIT});
#endif
}

static void center_camera(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    dc_hud_t *hud = menu->owner;
    if (action != MA_ACTIVATE) return;
    app_t *app = hud->app;
    /* 0x4097f8: native minimap uses (519,90), 96x84, bottom-up Y. */
    fvec2_t position = {
        (float)hud->map->width * (2 * (menu->cursor.x - (app->win.w - 121)) + 1) / 192.0f,
        (float)hud->map->height * (2 * (90 - menu->cursor.y) + 1) / 168.0f
    };
    fvec2_t screen;
    R_MapToScreen(app, hud->map, position.x, position.y, &screen.x, &screen.y);
    app->cam = fvec2_add(app->cam, fvec2_sub(
        (fvec2_t){DC_SB_WorldViewportWidth(app) / 2.0f, (app->win.h - 25) / 2.0f}, screen));
}

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
    int world_right = app->win.w - 124;
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

static void draw_minimap(const menu_t *menu, const menuitem_t *item) {
    const dc_hud_t *hud = menu->owner;
    irect_t r = item->rect;
    dc_ui_draw_minimap(hud->app, hud->map, hud->units, hud->unit_count,
                       (irect_t){r.x + 2, r.y + 2, r.w - 4, r.h - 4});
}

/* An in_text field's text: one line, left or centred in its characters. */
static void draw_field(const dc_hud_t *hud, const hudfield_t *field, const char *text) {
    const bitmapfont_t *font = &hud->font;
    ivec2_t at = {field->rect.x, field->rect.y};
    if (field->centered)
        at.x += (field->rect.w - HU_TextWidth(font, text, 1)) / 2;
    /* The RMP row picks font palette entries; the screen holds the terrain
     * palette, so each entry's colour is matched into it. */
    const uint8_t *row = R_PaletteMap(&font->sprite, field->palette);
    uint8_t remap[256];
    for (int i = 0; i < 256; ++i)
        remap[i] = V_NearestIndex(font->sprite.source_palette[row ? row[i] : i] | 0xff000000u);
    HU_DrawText(at, font, text, remap, 1);
}

/* The money (scount 75: right-aligned digit pictures), the day dial and the
 * day count (in_text 234). */
static void draw_status(const menu_t *menu, const menuitem_t *item) {
    (void)item;
    const dc_hud_t *hud = menu->owner;
    const level_t *map = hud->map;
    char text[32];
    const hudfield_t *money = &hud->fields[FIELD_MONEY];
    const spritesheet_t *buttons = R_CacheLookup(hud->images, "INTRFACE/MAINBUT.SPR");
    int resources = map->player_resources[consoleplayer][0];
    snprintf(text, sizeof(text), "%d", resources < 0 ? 0 : resources);
    if (buttons && money->picture >= 0 && money->picture + 9 < buttons->numlumps) {
        int x = money->rect.x + money->rect.w;
        for (int i = (int)strlen(text) - 1; i >= 0; --i) {
            int frame = money->picture + text[i] - '0';
            irect_t src = buttons->cells[frame].rect;
            x -= src.w;
            if (x < money->rect.x) break;
            irect_t dst = {x, money->rect.y, src.w, src.h};
            R_DrawSprite(buttons, frame, money->palette, &src, &dst, 0, 16);
        }
    }

    const spritesheet_t *dial = R_CacheLookup(hud->images, "SPRITES/CLOC.SPR");
    if (dial && dial->numlumps >= 2 && map->daylight.duration > 0) {
        int half = dial->numlumps / 2;
        int frame = (int)((int64_t)map->daylight.tics * half / map->daylight.duration);
        if (frame >= half) frame = half - 1;
        frame += map->daylight.phase * half;
        irect_t src = dial->cells[frame].rect;
        /* 0x4377e3–0x437806 cancels the SPR displacement at (608,450). */
        irect_t dst = ui_rect(hud->app, 608, 450, src.w, src.h);
        R_DrawSprite(dial, frame, -1, &src, &dst, 0, 16);
    }
    uint64_t clock = (uint64_t)leveltime * 1000 / (WORLD_CLOCK_MS * RTS_TICRATE);
    int days = map->daylight.duration > 0 ?
        (int)(clock / (uint64_t)map->daylight.duration / 2u) : 0;
    if (days > 999) days = 999;
    snprintf(text, sizeof(text), "%03d", days);
    draw_field(hud, &hud->fields[FIELD_DAYS], text);
}

/* in_text 79, above Build: the control under the pointer, else what the
 * selected building is doing. */
static void draw_label(const menu_t *menu, const menuitem_t *item) {
    (void)item;
    const dc_hud_t *hud = menu->owner;
    int id = menu->itemOn - FIRST_CONTROL;
    char line[40] = "";
    if (id >= 0 && id < NUMCONTROLS && hud->labels[id][0]) {
        snprintf(line, sizeof(line), "%s", hud->labels[id]);
    } else if (hud->product_mode) {
        const production_t *making = hud->selected ? hud->selected->production : NULL;
        if (making && making->queue_count > 0 && making->time_ms > 0) {
            int percent = (making->time_ms - making->time_left_ms) * 100 / making->time_ms;
            snprintf(line, sizeof(line), "Training x%d %d%%", making->queue_count,
                     percent < 0 ? 0 : percent > 100 ? 100 : percent);
        } else {
            snprintf(line, sizeof(line), "%s", dc_selected_building_label(hud->selected));
        }
    }
    draw_field(hud, &hud->fields[FIELD_LABEL], line);
}

/* in_text 148, the strip under the world: the newest HUD message. */
static void draw_message(const menu_t *menu, const menuitem_t *item) {
    (void)item;
    const dc_hud_t *hud = menu->owner;
    if (!hud->messages || hud->messages->count <= 0) return;
    char line[96];
    snprintf(line, sizeof(line), "%.61s", hud->messages->messages[hud->messages->count - 1].text);
    draw_field(hud, &hud->fields[FIELD_MESSAGE], line);
}

/* Show the controls of the open tab and of the selection. */
static void refresh(dc_hud_t *hud) {
    static const int options[] = {62, 63, 64, 151, 196, 202};
    const ProductButton *products[16] = {0};
    int product_count = dc_available_products(hud->units, hud->unit_count, hud->tab, products);
    hud->selected = dc_first_selected_unit(hud->units, hud->unit_count);
    hud->product_mode = hud->tab == 1 || (hud->tab == 0 &&
        (!hud->selected || dc_selected_unit_is_player_building(hud->selected)));
    for (int id = 3; id < NUMCONTROLS; ++id) control(hud, id)->visible = false;
    control(hud, 3 + hud->tab)->visible = true; /* The tab strip's title picture. */
    control(hud, 19)->visible = true;
    if (hud->tab == 2) {
        for (int i = 0; i < 6; ++i) control(hud, options[i])->visible = true;
    } else if (hud->product_mode) {
        int money = hud->map->player_resources[consoleplayer][0];
        for (int i = 0; i < product_count; ++i) {
            menuitem_t *item = control(hud, products[i]->ui_id);
            int quantity = hud->map->purchases[consoleplayer][products[i]->row_id].selected;
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
        int count = dc_commands(hud->units, hud->unit_count, ids);
        for (int i = 0; i < count; ++i) control(hud, ids[i])->visible = true;
    }
    /* A hotkey works whether or not its control is on the open tab. */
    for (int id = 0; id < NUMCONTROLS; ++id) {
        menuitem_t *item = control(hud, id);
        item->enabled = item->kind != MI_STATIC && (item->visible || item->hotkey);
    }
}

/* The font draws through PALETTE.RMP's (intensity*8+remap) maps, like MAINBUT. */
static bool load_font(dc_hud_t *hud, const char *data_root) {
    blob_t rmp;
    if (!HU_LoadFont(data_root, &hud->font) ||
        !W_ReadFile(M_va("%s/PALETTE.RMP", data_root), &rmp)) return false;
    spritepalettemap_t *maps = rmp.size >= 256 * 256 ? calloc(256, sizeof(*maps)) : NULL;
    for (int i = 0; maps && i < 256; ++i) {
        maps[i].id = i;
        memcpy(maps[i].indices, rmp.bytes + i * 256, 256);
    }
    W_FreeFile(&rmp);
    if (!maps) return false;
    free(hud->font.sprite.palette_maps);
    hud->font.sprite.palette_maps = maps;
    hud->font.sprite.palette_map_count = 256;
    return true;
}

static bool load_script(dc_hud_t *hud, const app_t *app, const char *data_root) {
    char path[1024], line[512];
    M_PathJoin(path, sizeof(path), data_root, "INTRFACE/MAINE");
    FILE *file = fopen(path, "r");
    if (!file) return false;
    int bright_pushed = 0, bright_highlight = 0;
    /* The brightness lines may follow a control that uses them. */
    struct { int normal, pushed; } frames[NUMCONTROLS];
    for (int id = 0; id < NUMCONTROLS; ++id) frames[id].normal = frames[id].pushed = -1;
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
            strcpy(hud->labels[id], label);
            continue;
        }
        int normal = -1, pushed = -1;
        if (sscanf(line, "%15s %d %d %d %d %d %d %d %d", kind, &id, &description,
                   &rect.x, &rect.y, &rect.w, &rect.h, &normal, &pushed) < 7) continue;
        if (!strcmp(kind, "in_text") || !strcmp(kind, "scount")) {
            for (int f = 0; f < NUMFIELDS; ++f) {
                if (field_ids[f] != id) continue;
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
                    rect.w *= hud->font.glyph_size.w + 1;
                    rect.h *= hud->font.line_h;
                }
                hud->fields[f] = (hudfield_t){
                    .rect = ui_rect(app, rect.x, rect.y, rect.w, rect.h),
                    .palette = (light > 31 ? 31 : light) * 8 + r,
                    .picture = kind[0] == 's' ? normal : -1,
                    .centered = strstr(line, "align centre") != NULL,
                };
            }
            continue;
        }
        if (id < 0 || id >= NUMCONTROLS) continue;
        bool count = !strcmp(kind, "count"), check = !strcmp(kind, "checkb");
        bool picture = !strcmp(kind, "picture");
        if (!count && !check && !picture && strcmp(kind, "pushb")) continue;
        menuitem_t *item = control(hud, id);
        *item = (menuitem_t){
            .kind = picture ? MI_STATIC : check ? MI_CHECK : MI_BUTTON,
            .rect = ui_rect(app, rect.x, rect.y, rect.w, rect.h),
            .routine = count ? purchase : command,
            .font = &hud->font,
            .ink = 0xffe7c25eu,
        };
        frames[id].normal = normal;
        frames[id].pushed = pushed;
        /* count N ... offset X Y: where the reserved quantity is written. */
        const char *offset = count ? strstr(line, "offset") : NULL;
        if (offset) sscanf(offset, "offset %d %d", &item->inset.x, &item->inset.y);
    }
    bool ok = !ferror(file);
    fclose(file);
    for (int id = 0; id < NUMCONTROLS; ++id)
        DC_ControlLooks(control(hud, id), frames[id].normal, frames[id].pushed, 7,
                        bright_pushed, bright_highlight);
    return ok;
}

void *DC_SB_Init(app_t *app, const char *data_root) {
    if (!app || !data_root) return NULL;
    dc_hud_t *hud = calloc(1, sizeof(*hud));
    if (!hud) return NULL;
    char path[1024];
    M_PathJoin(path, sizeof(path), data_root, "INTRFACE/INTRFACE.GIF");
    if (!load_font(hud, data_root) || !W_LoadGIFTexture(path, &hud->background) ||
        !load_script(hud, app, data_root)) {
        fprintf(stderr, "warning: failed to load the Dark Colony HUD\n");
        DC_SB_Shutdown(hud);
        return NULL;
    }
    hud->menu = (menu_t){.items = hud->items, .numitems = NUMITEMS, .itemOn = -1, .owner = hud};
    /* The sidebar column keeps its 480 rows at the top-right corner; a taller
     * screen is black under it, not world. */
    irect_t column = ui_rect(app, 516, 0, 124, 480);
    hud->items[COLUMN_REST] = (menuitem_t){.visible = app->win.h > column.h, .fill = 0xff000000u,
        .rect = {column.x, column.h, column.w, app->win.h - column.h}};
    hud->items[COLUMN] = (menuitem_t){.visible = true, .opaque = true, .rect = column, .sheet = &hud->background,
        .look = {{.part = {516, 0, 124, 480}, .palette = -1}}};
    hud->items[STRIP] = (menuitem_t){.visible = true, .opaque = true, .rect = ui_rect(app, 0, 455, 516, 25),
        .sheet = &hud->background, .look = {{.part = {0, 455, 516, 25}, .palette = -1}}};
    hud->items[MINIMAP] = (menuitem_t){.kind = MI_BUTTON, .visible = true, .enabled = true,
        .rect = ui_rect(app, 520, 5, 96, 84), .routine = center_camera, .ownerdraw = draw_minimap};
    hud->items[STATUS] = (menuitem_t){.visible = true, .rect = ui_rect(app, 524, 456, 72, 17),
        .ownerdraw = draw_status};
    hud->items[LABEL] = (menuitem_t){.visible = true, .rect = hud->fields[FIELD_LABEL].rect,
        .ownerdraw = draw_label};
    hud->items[MESSAGE] = (menuitem_t){.visible = true, .rect = ui_rect(app, 50, 462, 427, 11),
        .ownerdraw = draw_message};
    for (int id = 0; id < 3; ++id) control(hud, id)->visible = true;
    static const struct { int id; SDL_Keycode key; } hotkeys[] = {
        {150, SDLK_s}, {33, SDLK_m}, {35, SDLK_a}, {36, SDLK_w}, {19, SDLK_SPACE}, {196, SDLK_t},
    };
    for (int i = 0; i < 6; ++i) control(hud, hotkeys[i].id)->hotkey = hotkeys[i].key;
    /* Move Only and Move & Attack are one choice; units start in the second. */
    control(hud, 33)->group = control(hud, 35)->group = 1;
    control(hud, 35)->value = 1;
    return hud;
}

bool DC_SB_Responder(void *sb, app_t *app, level_t *map,
                     mobj_t *const *units, int unit_count, const SDL_Event *e) {
    dc_hud_t *hud = sb;
    if (!hud || !app || !map || !e) return false;
    hud->app = app;
    hud->map = map;
    hud->units = units;
    hud->unit_count = unit_count;
    refresh(hud);
    if (e->type == SDL_KEYDOWN && !e->key.repeat && !(e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT)) &&
        (e->key.keysym.sym == SDLK_RETURN || e->key.keysym.sym == SDLK_KP_ENTER)) {
        if (hud->targeting == 36) finish_waypoints(hud);
        else G_SelectedTiccmd(TC_DEPLOY, units, unit_count, (fvec2_t){0}, 0);
        return true;
    }
    bool down = e->type == SDL_MOUSEBUTTONDOWN;
    if (down && e->button.button == SDL_BUTTON_RIGHT && hud->targeting) {
        if (hud->targeting == 36) finish_waypoints(hud);
        else hud->targeting = 0;
        return true;
    }
    if (M_MenuResponder(&hud->menu, app, e)) return true;
    /* While an order waits for its target, left clicks on the world are its. */
    if (!hud->targeting || (!down && e->type != SDL_MOUSEBUTTONUP) ||
        e->button.button != SDL_BUTTON_LEFT) return false;
    if (down && hud->targeting == 36) {
        cell_t cell = R_ScreenToMapGrid(app, map, hud->menu.cursor.x, hud->menu.cursor.y);
        int count = hud->waypoints.count;
        if (count == 7 || (count && (ivec2_equal(cell, hud->waypoints.points[0]) ||
                                     ivec2_equal(cell, hud->waypoints.points[count - 1]))))
            finish_waypoints(hud);
        else hud->waypoints.points[hud->waypoints.count++] = cell;
    }
    return true;
}

void DC_SB_Drawer(void *sb, app_t *app, const level_t *map,
                  mobj_t *const *units, int unit_count,
                  const spritecache_t *sprites, const hudtext_t *messages) {
    dc_hud_t *hud = sb;
    if (!hud || !app || !map) return;
    hud->app = app;
    hud->map = map;
    hud->units = units;
    hud->unit_count = unit_count;
    hud->images = sprites ? sprites->ui : NULL;
    hud->messages = messages;
    refresh(hud);
    /* The level's image cache owns the button sheet. */
    const spritesheet_t *buttons = R_CacheLookup(hud->images, "INTRFACE/MAINBUT.SPR");
    for (int id = 0; id < NUMCONTROLS; ++id) control(hud, id)->sheet = buttons;
    M_MenuDrawer(&hud->menu);
}

void DC_SB_Shutdown(void *sb) {
    dc_hud_t *hud = sb;
    if (!hud) return;
    R_FreeSprite(&hud->background);
    HU_FreeFont(&hud->font);
    free(hud);
}
