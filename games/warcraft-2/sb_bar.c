#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Left column from the retail 640x480 layout: menu, minimap, info, commands,
 * then the resource and status bars over the map. Coordinates match the
 * measured MAINDAT pieces. */

enum {
    IT_MENUBACK, IT_MAPFRAME, IT_MAP, IT_INFO, IT_PANEL,
    IT_RESOURCE, IT_STATUS, IT_FILLER, IT_MENU, IT_GOLD, IT_LUMBER, IT_OIL, IT_TRAIN,
    IT_CMD, IT_SLOT = IT_CMD + 9, IT_COUNT = IT_SLOT + 9
};

enum {
    CK_NONE, CK_MOVE, CK_STOP, CK_ATTACK, CK_PATROL, CK_STAND,
    CK_REPAIR, CK_HARVEST, CK_RETURN, CK_PAGE, CK_CANCEL, CK_TRAIN, CK_PLACE, CK_CANCEL_BUILD
};

typedef struct {
    int kind, icon, arg, gold, wood, oil;
    SDL_Keycode key;
    const char *tip;
} cmd_t;

typedef struct {
    int pud, icon;
    SDL_Keycode key;
    const char *tip, *tip_orc;
} bld_t;

static const uint8_t unit_icon[W2_TYPE_COUNT] = {
    2, 3, 0, 1, 16, 17, 8, 9, 4, 5,
    14, 15, 10, 11, 12, 13, 0, 1, 6, 7,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 18, 19, 20, 21,
    22, 23, 24, 25, 0xff, 0xff, 0xff, 0xff, 26, 27,
    28, 29, 30, 31, 0xff, 0xff, 0xff, 0xff, 0xff, 36,
    32, 33, 34, 35, 0xff, 0xff, 37, 0xff, 38, 39,
    42, 43, 62, 63, 60, 61, 56, 57, 58, 59,
    72, 73, 48, 49, 40, 41, 44, 45, 52, 53,
    64, 65, 46, 47, 50, 51, 54, 55, 66, 67,
    70, 71, 74, 79, 0xff, 0xff, 75, 77, 76, 78,
    81, 80, 82, 92, 93,
};
_Static_assert(sizeof(unit_icon) == W2_TYPE_COUNT, "unit icon table");

/* Pos 6 of the basic page is empty; prices come from mobjinfo[].
 * Orc buildings are the next PUD type and the next icon. */
static const bld_t basic_page[9] = {
    { 58, 38, SDLK_f, "Build farm", "Build pig farm" },
    { 60, 42, SDLK_b, "Build barracks", "Build barracks" },
    { 74, 40, SDLK_h, "Build town hall", "Build great hall" },
    { 76, 44, SDLK_l, "Build lumber mill", "Build lumber mill" },
    { 82, 46, SDLK_s, "Build blacksmith", "Build blacksmith" },
    { -1, -1, 0, NULL, NULL },
    { 64, 60, SDLK_t, "Build tower", "Build tower" },
    { 103, 92, SDLK_w, "Build wall", "Build wall" },
    { -1, 91, SDLK_ESCAPE, "Cancel", NULL },
};
static const bld_t advanced_page[9] = {
    { 72, 48, SDLK_s, "Build shipyard", "Build shipyard" },
    { 78, 52, SDLK_f, "Build foundry", "Build foundry" },
    { 84, 50, SDLK_r, "Build refinery", "Build refinery" },
    { 68, 58, SDLK_i, "Build inventor", "Build alchemist" },
    { 66, 56, SDLK_a, "Build stables", "Build ogre mound" },
    { 80, 64, SDLK_m, "Build mage tower", "Build temple" },
    { 62, 62, SDLK_c, "Build church", "Build altar of storms" },
    { 70, 72, SDLK_g, "Build gryphon aviary", "Build dragon roost" },
    { -1, 91, SDLK_ESCAPE, "Cancel", NULL },
};

static menuitem_t items[IT_COUNT];
static menu_t hud = { .items = items, .numitems = IT_COUNT, .itemOn = -1, .size = {640, 480} };
static w2_hud_art_t art;
static cmd_t shown[9];
static char note[96];
static char status_line[96];
static char root_copy[1024];
static mobj_t *slot_unit[9];
static const mobj_t *portrait;
static int portrait_count;
static int page;
static uint32_t command_id;
static bool laid_out;

void w2_draw_selection(const unitoverlaycontext_t *ctx) {
    const mobj_t *unit = ctx->unit;
    if (!(unit->traits & MF_SELECTABLE) || !P_MobjIsSelected(unit) || unit->hp <= 0) return;
    isize2_t footprint = mobjinfo[unit->type_id].w2.footprint;
    fvec2_t corner = fvec2_sub(fixed3_xy_to_fvec2(unit->core.position),
                              (fvec2_t){footprint.w * 0.5f, footprint.h * 0.5f});
    fvec2_t screen;
    R_MapToScreen(ctx->app, &level, corner.x, corner.y, &screen.x, &screen.y);
    V_DrawRectOutline((irect_t){(int)lroundf(screen.x), (int)lroundf(screen.y),
                               footprint.w * ctx->app->cell.w, footprint.h * ctx->app->cell.h},
                      V_NearestIndex(0xff00fc00u));
}

static void on_menu(menu_t *menu, menuitem_t *item, menuaction_t action);
static void on_command(menu_t *menu, menuitem_t *item, menuaction_t action);
static void on_slot(menu_t *menu, menuitem_t *item, menuaction_t action);
static void draw_minimap(const menu_t *menu, const menuitem_t *item, irect_t rect);
static void draw_info(const menu_t *menu, const menuitem_t *item, irect_t rect);
static void draw_resources(const menu_t *menu, const menuitem_t *item, irect_t rect);
static void draw_status(const menu_t *menu, const menuitem_t *item, irect_t rect);
static void draw_slot(const menu_t *menu, const menuitem_t *item, irect_t rect);

irect_t G_WorldViewport(const app_t *app) {
    int scale = R_UIScale(app);
    int w = app ? app->win.w - 192 * scale : 1;
    int h = app ? app->win.h - 32 * scale : 1;
    return (irect_t){176 * scale, 16 * scale, w > 0 ? w : 1, h > 0 ? h : 1};
}

/* Art for a type that first appears mid-game, such as an upgraded hall. */
void W2_EnsureUnitSprite(int pud) {
    if (hudview.sprites && root_copy[0])
        w2_cache_unit_sprite(root_copy, (spritecache_t *)hudview.sprites, pud);
}

static int icon_of(int pud) {
    if (pud < 0 || pud >= W2_TYPE_COUNT || unit_icon[pud] == 0xff) return -1;
    return unit_icon[pud];
}

static bool orc_side(void) {
    const w2_pud_t *pud = level.native_data;
    return pud && consoleplayer >= 0 && consoleplayer < 16 && pud->sides[consoleplayer] == 1;
}

static int *stock(void) {
    if (consoleplayer < 0 || consoleplayer >= 8) return NULL;
    return level.player_resources[consoleplayer];
}

static void set_note(const char *text) {
    snprintf(note, sizeof(note), "%s", text ? text : "");
}

static void draw_ink(int x, int y, const char *text) {
    if (!text || !text[0]) return;
    if (art.font.sprite.numlumps > 0) {
        V_DrawText((ivec2_t){ x, y }, &art.font, text, V_RemapPalette(art.font.sprite.source_palette));
        return;
    }
    V_DrawSmallText((irect_t){ x, y, 180, 8 }, text, 0xffffe84au,
                    V_DrawSize());
}

/* Wargus panel ~| marks the label's right edge, before the colon. */
static void draw_stat(ivec2_t anchor, const char *label, const char *value) {
    char text[64];
    snprintf(text, sizeof(text), "%s: %s", label, value);
    draw_ink(anchor.x - V_TextWidth(&art.font, label), anchor.y, text);
}

static void pretty_name(const char *src, char *dst, size_t n) {
    bool cap = true;
    size_t o = 0;
    if (!src) src = "unit";
    for (const char *p = src; *p && o + 1 < n; ++p) {
        unsigned char c = (unsigned char)(*p == '-' ? ' ' : *p);
        if (c == ' ') cap = true;
        else if (cap) { c = (unsigned char)toupper(c); cap = false; }
        dst[o++] = (char)c;
    }
    dst[o] = '\0';
}

static void use_sheet(menuitem_t *item, const spritesheet_t *sheet) {
    item->sheet = sheet && sheet->numlumps ? sheet : NULL;
    item->opaque = true;
    int cell = item->sheet ? 0 : -1;
    for (int s = 0; s < MS_STATES; ++s) item->look[s].cell = cell;
}

static void layout(void) {
    memset(items, 0, sizeof(items));
    static const irect_t chrome[] = {
        { 0, 0, 176, 24 }, { 0, 24, 176, 136 }, { 24, 26, 128, 128 },
        { 0, 160, 176, 176 }, { 0, 336, 176, 144 },
        { 176, 0, 448, 16 }, { 176, 464, 448, 16 }, { 624, 0, 16, 480 },
    };
    for (int i = 0; i < 8; ++i) {
        items[i].visible = true;
        items[i].rect = chrome[i];
        for (int s = 0; s < MS_STATES; ++s) items[i].look[s].cell = -1;
    }
    items[IT_MENU].kind = MI_BUTTON;
    items[IT_MENU].visible = true;
    items[IT_MENU].enabled = true;
    items[IT_MENU].rect = (irect_t){24, 2, 128, 20};
    items[IT_MENU].routine = on_menu;
    snprintf(items[IT_MENU].text, sizeof(items[IT_MENU].text), "Menu (F10)");
    items[IT_MENU].align = MALIGN_CENTER;
    items[IT_MENU].ink = 0xffffe84au;
    items[IT_PANEL].anchor = MANCHOR_GROW;
    items[IT_PANEL].stretch = true;
    items[IT_RESOURCE].anchor = MANCHOR_WIDE;
    items[IT_RESOURCE].stretch = true;
    items[IT_STATUS].anchor = MANCHOR_BOTTOM | MANCHOR_WIDE;
    items[IT_STATUS].stretch = true;
    items[IT_FILLER].anchor = MANCHOR_RIGHT | MANCHOR_GROW;
    items[IT_FILLER].stretch = true;
    for (int i = 0; i < 3; ++i) {
        menuitem_t *icon = &items[IT_GOLD + i];
        icon->visible = true;
        icon->rect = (irect_t){176 + i * 75, 0, 14, 14};
        for (int s = 0; s < MS_STATES; ++s) icon->look[s].cell = i;
    }
    items[IT_MAP].kind = MI_MINIMAP;
    items[IT_MAP].enabled = true;
    items[IT_MAP].ownerdraw = draw_minimap;
    items[IT_INFO].ownerdraw = draw_info;
    items[IT_RESOURCE].ownerdraw = draw_resources;
    items[IT_STATUS].ownerdraw = draw_status;
    items[IT_TRAIN].rect = (irect_t){110, 241, 46, 38};
    items[IT_TRAIN].frame.outer = 0xff000000u;
    items[IT_TRAIN].frame.inner = 0xfffcfcfcu;
    static const int col_x[3] = { 9, 65, 121 };
    static const int cmd_y[3] = { 340, 387, 434 };
    static const int slot_y[3] = { 169, 223, 277 };
    for (int i = 0; i < 9; ++i) {
        menuitem_t *cmd = &items[IT_CMD + i];
        menuitem_t *slot = &items[IT_SLOT + i];
        *cmd = (menuitem_t){
            .kind = MI_BUTTON, .routine = on_command,
            .rect = { col_x[i % 3], cmd_y[i / 3], 46, 38 },
            .frame = {0xff000000u, 0xfffcfcfcu}, .color = 0xff00fc00u,
        };
        *slot = (menuitem_t){
            .kind = MI_BUTTON, .id = i, .routine = on_slot,
            .ownerdraw = draw_slot,
            .rect = { col_x[i % 3], slot_y[i / 3], 46, 38 },
            .frame = {0xff000000u, 0xfffcfcfcu}, .color = 0xff00fc00u,
        };
        for (int s = 0; s < MS_STATES; ++s) {
            cmd->look[s].cell = -1;
            slot->look[s].cell = -1;
        }
    }
}

static bool advanced_ok(void) {
    static const uint16_t need[] = { 77, 78, 89, 90, 91, 92 };
    for (int i = 0; i < 6; ++i)
        if (G_ModelHasActorType(NULL, consoleplayer, need[i])) return true;
    return false;
}

static void put_cmd(int slot, int kind, int icon, int arg, int gold, int wood, int oil,
                    SDL_Keycode key, const char *tip) {
    if (slot < 0 || slot >= 9) return;
    shown[slot] = (cmd_t){ kind, icon, arg, gold, wood, oil, key, tip };
}

static void fill_page(const bld_t *page_in, bool orc) {
    for (int i = 0; i < 9; ++i) {
        const bld_t *bld = &page_in[i];
        if (bld->pud < 0 && bld->icon < 0) continue;
        if (bld->pud < 0) {
            put_cmd(i, CK_CANCEL, bld->icon, 0, 0, 0, 0, bld->key, "Cancel");
            continue;
        }
        int pud = bld->pud + (orc ? 1 : 0);
        int icon = bld->icon + (orc ? 1 : 0);
        const char *tip = orc && bld->tip_orc ? bld->tip_orc : bld->tip;
        const int *cost = mobjinfo[pud + 1].w2.costs.resources;
        put_cmd(i, CK_PLACE, icon, pud, cost[0], cost[1], cost[2], bld->key, tip);
    }
}

/* A catalog product as a command, priced from the catalog. */
static void put_product(int slot, int ui, SDL_Keycode key, const char *tip) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui);
    if (!product) return;
    put_cmd(slot, CK_TRAIN, product->icon_frame, ui, product->cost,
            W2_ProductLumber(product), W2_ProductOil(product), key, tip ? tip : product->label);
}

/* The next tier of a research line, while one is open to the player. */
static void put_research(int slot, int first_ui, SDL_Keycode key) {
    for (int ui = first_ui; ui <= first_ui + 1; ++ui) {
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui);
        if (!product || !G_ModelProductAvailable(NULL, consoleplayer, product)) continue;
        put_product(slot, ui, key, NULL);
        return;
    }
}

static void fill_train(const mobj_t *unit) {
    int type = unit->type_id;
    /* A site offers only to stop: the price comes back and the builder steps out. */
    if (W2_UnderConstruction(unit)) {
        put_cmd(8, CK_CANCEL_BUILD, 91, 0, 0, 0, 0, SDLK_ESCAPE, "Cancel construction");
        return;
    }
    /* Wargus check-no-research: a busy building offers no more research. */
    bool busy = unit->production && unit->production->queue_count > 0;
    if (type == 75 || type == 89 || type == 91 || type == 76 || type == 90 || type == 92) {
        bool orc = type == 76 || type == 90 || type == 92;
        int pud = orc ? 3 : 2;
        const int *cost = mobjinfo[pud + 1].w2.costs.resources;
        put_cmd(0, CK_TRAIN, icon_of(pud), orc ? 4 : 3, cost[0], cost[1], cost[2], SDLK_p,
                orc ? "Train peon" : "Train peasant");
        static const struct { int type, ui; } halls[] = {
            { MT_TOWN_HALL, W2_UI_KEEP }, { MT_KEEP, W2_UI_CASTLE },
            { MT_GREAT_HALL, W2_UI_STRONGHOLD }, { MT_STRONGHOLD, W2_UI_FORTRESS },
        };
        for (int i = 0; i < 4 && !busy; ++i) {
            const StaticProductDefinition *product = G_ModelProductByUIId(NULL, halls[i].ui);
            if (halls[i].type == type && product && G_ModelProductAvailable(NULL, consoleplayer, product))
                put_product(1, halls[i].ui, SDLK_u, NULL);
        }
        return;
    }
    if (type == MT_HUMAN_BLACKSMITH || type == MT_ORC_BLACKSMITH) {
        bool orc = type == MT_ORC_BLACKSMITH;
        if (busy) return;
        put_research(0, orc ? W2_UI_AXE1 : W2_UI_SWORD1, SDLK_w);
        put_research(1, orc ? W2_UI_ORC_SHIELD1 : W2_UI_HUMAN_SHIELD1, SDLK_a);
        return;
    }
    if (type == MT_ELVEN_LUMBER_MILL || type == MT_TROLL_LUMBER_MILL) {
        if (!busy) put_research(0, type == MT_TROLL_LUMBER_MILL ? W2_UI_THROWING_AXE1 : W2_UI_ARROW1, SDLK_u);
        return;
    }
    if (type != 61 && type != 62) return;
    bool orc = type == 62;
    static const int human_pud[] = { 0, 8, 4, 6 };
    static const int human_ui[] = { 1, 5, 7, 9 };
    static const SDL_Keycode keys[] = { SDLK_f, SDLK_a, SDLK_b, SDLK_k };
    static const char *human_tip[] = { "Train footman", "Train archer", "Train ballista", "Train knight" };
    static const char *orc_tip[] = { "Train grunt", "Train axethrower", "Train catapult", "Train ogre" };
    for (int i = 0; i < 4; ++i) {
        int pud = human_pud[i] + (orc ? 1 : 0);
        int ui = human_ui[i] + (orc ? 1 : 0);
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui);
        put_cmd(i, CK_TRAIN, icon_of(pud), ui, product ? product->cost : 0, W2_ProductLumber(product), 0,
                keys[i], orc ? orc_tip[i] : human_tip[i]);
    }
}

static void fill_mobile(const mobj_t *unit, bool orc) {
    bool harvest = (unit->traits & MF_HARVESTER) != 0;
    put_cmd(0, CK_MOVE, orc ? 84 : 83, 0, 0, 0, 0, SDLK_m, "Move");
    put_cmd(1, CK_STOP, orc ? 167 : 164, 0, 0, 0, 0, SDLK_s, "Stop");
    put_cmd(2, CK_ATTACK, orc ? 119 : 116, 0, 0, 0, 0, SDLK_a, "Attack");
    if (harvest) {
        put_cmd(3, CK_REPAIR, 85, 0, 0, 0, 0, SDLK_r, "Repair");
        put_cmd(4, CK_HARVEST, 86, 0, 0, 0, 0, SDLK_h, "Harvest");
        if (unit->harvest.cargo)
            put_cmd(5, CK_RETURN, orc ? 90 : 89, 0, 0, 0, 0, SDLK_g, "Return goods");
        put_cmd(6, CK_PAGE, 87, 1, 0, 0, 0, SDLK_b, "Build basic structure");
        if (advanced_ok())
            put_cmd(7, CK_PAGE, 88, 2, 0, 0, 0, SDLK_v, "Build advanced structure");
        return;
    }
    put_cmd(3, CK_PATROL, orc ? 179 : 178, 0, 0, 0, 0, SDLK_p, "Patrol");
    put_cmd(4, CK_STAND, orc ? 181 : 180, 0, 0, 0, 0, SDLK_t, "Stand ground");
}

static void apply_commands(void) {
    for (int i = 0; i < 9; ++i) {
        menuitem_t *item = &items[IT_CMD + i];
        const cmd_t *cmd = &shown[i];
        if (!cmd->kind) {
            item->visible = item->enabled = false;
            item->hotkey = 0;
            item->tooltip = NULL;
            item->sheet = NULL;
            for (int s = 0; s < MS_STATES; ++s) item->look[s].cell = -1;
            continue;
        }
        int frame = cmd->icon;
        item->visible = item->enabled = true;
        item->sheet = art.icons.numlumps ? &art.icons : NULL;
        if (!item->sheet || frame < 0 || frame >= item->sheet->numlumps) frame = -1;
        for (int s = 0; s < MS_STATES; ++s) item->look[s].cell = frame;
        item->hotkey = cmd->key;
        item->tooltip = cmd->tip;
        item->opaque = false;
    }
}

static int living_selected(mobj_t **own) {
    int count = 0;
    *own = NULL;
    portrait = NULL;
    memset(slot_unit, 0, sizeof(slot_unit));
    for (int i = 0; i < hudview.unit_count; ++i) {
        mobj_t *unit = hudview.units ? hudview.units[i] : NULL;
        if (!unit || !P_MobjIsSelected(unit) || unit->remove || unit->hp <= 0) continue;
        if (count < 9) slot_unit[count] = unit;
        if (count == 0) portrait = unit;
        if (!*own && unit->owner == consoleplayer) *own = unit;
        count++;
    }
    portrait_count = count;
    return count;
}

static void refresh(menu_t *menu) {
    mobj_t *own = NULL;
    int count = living_selected(&own);
    uint32_t id = own ? own->id : 0;
    if (id != command_id) {
        command_id = id;
        page = 0;
    }
    if (page == 2 && !advanced_ok()) page = 0;
    memset(shown, 0, sizeof(shown));
    if (own && page == 1) fill_page(basic_page, orc_side());
    else if (own && page == 2) fill_page(advanced_page, orc_side());
    else if (own && !(own->traits & MF_MOBILE)) fill_train(own);
    else if (own) fill_mobile(own, orc_side());
    apply_commands();
    int info_frame = count == 1 ? 1 : 0;
    bool training = count == 1 && portrait->production && portrait->production->queue_count;
    if (training || (count == 1 && W2_UnderConstruction(portrait))) info_frame = 3;
    for (int s = 0; s < MS_STATES; ++s) items[IT_INFO].look[s].cell = info_frame;
    items[IT_TRAIN].visible = training;
    items[IT_TRAIN].sheet = &art.icons;
    int train_icon = -1;
    if (training) {
        const production_t *prod = portrait->production;
        const StaticProductDefinition *product = G_ModelProductByClassType(
            NULL, prod->product_class, prod->product_type);
        train_icon = prod->product_class == RTS_PRODUCT_UNIT ? icon_of(prod->actor_id - 1) :
                     product ? product->icon_frame : -1;
    }
    for (int s = 0; s < MS_STATES; ++s) items[IT_TRAIN].look[s].cell = train_icon;
    for (int i = 0; i < 9; ++i) {
        menuitem_t *slot = &items[IT_SLOT + i];
        bool show = count > 0 && slot_unit[i];
        slot->visible = show;
        slot->enabled = show && count > 1;
        int frame = -1;
        if (show) frame = icon_of((int)slot_unit[i]->type_id - 1);
        slot->sheet = show && art.icons.numlumps ? &art.icons : NULL;
        if (!slot->sheet || frame < 0 || frame >= slot->sheet->numlumps) frame = -1;
        for (int s = 0; s < MS_STATES; ++s) slot->look[s].cell = frame;
    }
    const menuitem_t *hover = M_MenuHover(menu);
    if (hover && hover->tooltip && hover->tooltip[0])
        snprintf(status_line, sizeof(status_line), "%s", hover->tooltip);
    else
        snprintf(status_line, sizeof(status_line), "%s", note);
}

static fvec2_t cursor_goal(const menu_t *menu) {
    cell_t cell = R_ScreenToMapGrid(menu->app, &level, menu->cursor.x, menu->cursor.y);
    return (fvec2_t){ cell.x + 0.5f, cell.y + 0.5f };
}

static void order_at(const menu_t *menu, int kind) {
    fvec2_t goal = cursor_goal(menu);
    if (kind == CK_ATTACK) {
        int hit = R_PickUnit(menu->app, &level, hudview.units, hudview.unit_count, NULL,
                             hudview.sprites, &game_info, menu->cursor.x, menu->cursor.y, -1);
        if (hit >= 0 && hudview.units[hit]->owner != consoleplayer && hudview.units[hit]->hp > 0) {
            G_SelectedTiccmd(TC_ATTACK, hudview.units, hudview.unit_count, goal,
                             hudview.units[hit]->id);
            return;
        }
        G_SelectedTiccmd(TC_MOVE, hudview.units, hudview.unit_count, goal, 0);
        return;
    }
    G_SelectedTiccmd(kind == CK_HARVEST ? TC_HARVEST : TC_MOVE,
                     hudview.units, hudview.unit_count, goal, 0);
}

/* The selected worker is sent to build; the price is paid when it arrives
 * (W2_ConstructOrder). The cursor cell is the footprint's top-left. */
static void place_building(menu_t *menu, menuitem_t *item, const cmd_t *cmd) {
    int pud = cmd->arg;
    if (pud < 0 || pud >= W2_TYPE_COUNT || !W2_Buildable((uint16_t)(pud + 1))) {
        set_note("Walls are not built.");
        M_MenuTarget(menu, NULL);
        return;
    }
    int *res = stock();
    if (!res || res[0] < cmd->gold || res[1] < cmd->wood || res[2] < cmd->oil) {
        set_note("Not enough resources.");
        M_MenuTarget(menu, NULL);
        return;
    }
    mobj_t *builder = NULL;
    for (int i = 0; i < hudview.unit_count && !builder; ++i) {
        mobj_t *unit = hudview.units[i];
        if (unit && P_MobjIsSelected(unit) && unit->owner == consoleplayer && unit->hp > 0 &&
            !unit->remove && (unit->traits & MF_HARVESTER)) builder = unit;
    }
    cell_t cell = R_ScreenToMapGrid(menu->app, &level, menu->cursor.x, menu->cursor.y);
    if (!builder || !W2_CanPlace((uint16_t)(pud + 1), (ivec2_t){cell.x, cell.y}, builder) ||
        !G_ConstructOrder(builder, pud + 1, (ivec2_t){cell.x, cell.y})) {
        set_note("Cannot build there.");
        M_MenuTarget(menu, item);
        return;
    }
    S_Bark(&builder, 1, SE_ACK, true);
    set_note("");
    page = 0;
    M_MenuTarget(menu, NULL);
}

static void train_product(mobj_t *producer, const cmd_t *cmd) {
    int *res = stock();
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, cmd->arg);
    if (!producer || !res || !product || res[0] < product->cost ||
        res[1] < cmd->wood || res[2] < cmd->oil) {
        set_note("Not enough resources.");
        return;
    }
    if (!G_BuildOrder(producer, product->ui_id)) {
        set_note("Cannot train.");
        return;
    }
    set_note("");
}

static void on_menu(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action == MA_ACTIVATE && menu->app) M_StartControlPanel(menu->app);
}

static void on_slot(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (action != MA_ACTIVATE || item->id < 0 || item->id >= 9) return;
    mobj_t *unit = slot_unit[item->id];
    if (!unit || unit->remove || unit->hp <= 0) return;
    for (int i = 0; i < hudview.unit_count; ++i)
        P_MobjSetSelected(hudview.units[i], false);
    P_MobjSetSelected(unit, true);
}

static void on_command(menu_t *menu, menuitem_t *item, menuaction_t action) {
    int slot = (int)(item - &items[IT_CMD]);
    if (slot < 0 || slot >= 9) return;
    const cmd_t *cmd = &shown[slot];
    if (action == MA_TARGET) {
        if (cmd->kind == CK_PLACE) place_building(menu, item, cmd);
        else if (cmd->kind == CK_MOVE || cmd->kind == CK_ATTACK || cmd->kind == CK_HARVEST)
            order_at(menu, cmd->kind);
        return;
    }
    if (action != MA_ACTIVATE || !cmd->kind) return;
    switch (cmd->kind) {
    case CK_MOVE: case CK_ATTACK: case CK_HARVEST: case CK_PLACE:
        M_MenuTarget(menu, item);
        break;
    case CK_STOP:
        G_SelectedTiccmd(TC_STOP, hudview.units, hudview.unit_count, (fvec2_t){ 0 }, 0);
        break;
    case CK_RETURN: {
        G_SelectedTiccmd(TC_RETURN_GOODS, hudview.units, hudview.unit_count, (fvec2_t){0}, 0);
        break;
    }
    case CK_PAGE:
        page = cmd->arg;
        M_MenuTarget(menu, NULL);
        break;
    case CK_CANCEL:
        page = 0;
        M_MenuTarget(menu, NULL);
        break;
    case CK_CANCEL_BUILD: {
        mobj_t *own = NULL;
        living_selected(&own);
        if (own && !G_CancelConstructionOrder(own)) set_note("Cannot cancel.");
        break;
    }
    case CK_TRAIN: {
        mobj_t *own = NULL;
        living_selected(&own);
        train_product(own, cmd);
        break;
    }
    case CK_PATROL: set_note("Patrol is not available."); break;
    case CK_STAND: set_note("Stand ground is not available."); break;
    case CK_REPAIR: set_note("Repair is not available."); break;
    default: break;
    }
}

static void food_counts(int *used, int *have) {
    *used = *have = 0;
    for (int i = 0; i < hudview.unit_count; ++i) {
        const mobj_t *unit = hudview.units ? hudview.units[i] : NULL;
        int pud;
        if (!unit || unit->remove || unit->hp <= 0 || unit->owner != consoleplayer) continue;
        pud = (int)unit->type_id - 1;
        if (pud < 0 || pud >= W2_TYPE_COUNT) continue;
        *have += mobjinfo[unit->type_id].w2.food.supply;
        *used += mobjinfo[unit->type_id].w2.food.demand;
    }
}

static void draw_minimap(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)item;
    if (!menu->app || level.width <= 0 || level.height <= 0 || rect.w <= 0 || rect.h <= 0) return;
    const tileset_t *tiles = hudview.tileset;
    uint8_t unseen = V_NearestIndex(0xff000000u);
    uint8_t own = V_NearestIndex(0xff00ff00u);
    uint8_t neutral = V_NearestIndex(0xffc0c0c0u);
    int scale_x = rect.w * 100 / level.width;
    int scale_y = rect.h * 100 / level.height;
    if (scale_x < 1) scale_x = 1;
    if (scale_y < 1) scale_y = 1;
    irect_t clip = V_GetClip();
    V_SetClip(rect);
    for (int py = 0; py < rect.h; ++py) {
        int gy = L_ScreenY(&level, py * level.height / rect.h);
        for (int px = 0; px < rect.w; ++px) {
            int gx = px * level.width / rect.w;
            uint8_t color = unseen;
            if (tiles && tiles->indices && L_Contains(&level, gx, gy) &&
                P_SightBrightness(&level, (ivec2_t){ gx, gy }) > 0) {
                int tile = level.tile_ids[L_Index(&level, gx, gy)];
                if (tiles->tile_lookup)
                    tile = tile < tiles->tile_lookup_count ? tiles->tile_lookup[tile] : -1;
                /* Wargus/Stratagus GetTileGraphicPixel: native tile samples,
                 * not hand-picked colors for grass, water and trees. */
                int x = 7 + ((px * 100) % scale_x) / 100 * 8;
                int y = 6 + ((py * 100) % scale_y) / 100 * 8;
                if (tile >= 0 && tile < tiles->count && x < tiles->tile_w && y < tiles->tile_h)
                    color = tiles->indices[((size_t)tile * tiles->tile_h + y) * tiles->tile_w + x];
            }
            V_DrawPoint((ivec2_t){ rect.x + px, rect.y + py }, color);
        }
    }
    for (int i = 0; i < hudview.unit_count; ++i) {
        const mobj_t *unit = hudview.units[i];
        int pud;
        if (!unit || unit->remove || unit->hp <= 0 || !P_VisibleToPlayer(unit)) continue;
        pud = (int)unit->type_id - 1;
        if (pud >= 0 && pud < W2_TYPE_COUNT && (mobjinfo[pud + 1].w2.flags & W2_CRITTER)) continue;
        if (pud < 0 || pud >= W2_TYPE_COUNT) continue;
        isize2_t footprint = mobjinfo[pud + 1].w2.footprint;
        fvec2_t pos = fvec2_sub(fixed3_xy_to_fvec2(unit->core.position),
                              (fvec2_t){footprint.w * 0.5f, footprint.h * 0.5f});
        int x = rect.x + (int)(pos.x * (float)rect.w / (float)level.width);
        int y = rect.y + (int)(L_ScreenYF(&level, pos.y) * (float)rect.h / (float)level.height);
        uint8_t color = unit->owner == consoleplayer ? own : unit->owner >= 8 ? neutral : 208 + unit->owner * 4;
        if (pud == 92) color = V_NearestIndex(0xffffff00u);
        V_FillRect((irect_t){ x + 1, y + 1, footprint.w * rect.w / level.width + 1,
                            footprint.h * rect.h / level.height + 1 }, color);
    }
    irect_t world = G_WorldViewport(menu->app);
    cell_t tl = R_ScreenToGrid(menu->app, world.x, world.y);
    cell_t br = R_ScreenToGrid(menu->app, world.x + world.w - 1, world.y + world.h - 1);
    int vx = rect.x + tl.x * rect.w / level.width;
    int vy = rect.y + L_ScreenY(&level, tl.y) * rect.h / level.height;
    int vw = (br.x - tl.x) * rect.w / level.width;
    int vh = (br.y - tl.y) * rect.h / level.height;
    if (vw < 2) vw = 2;
    if (vh < 2) vh = 2;
    V_DrawRectOutline((irect_t){ vx, vy, vw, vh }, V_NearestIndex(0xffffffffu));
    V_SetClip(clip);
}

static void draw_info(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    if (portrait_count != 1 || !portrait) return;
    int pud = (int)portrait->type_id - 1;
    if (pud < 0 || pud >= W2_TYPE_COUNT) return;
    char name[64], hp[32];
    const mobjinfo_t *type = &mobjinfo[portrait->type_id];
    if (type->label) snprintf(name, sizeof(name), "%s", type->label);
    else pretty_name(type->name, name, sizeof(name));
    char *second = NULL;
    if (V_TextWidth(&art.font, name) > 110) {
        second = strrchr(name, ' ');
        if (second) *second++ = '\0';
    }
    draw_ink(rect.x + 114 - V_TextWidth(&art.font, name) / 2, rect.y + 11, name);
    if (second)
        draw_ink(rect.x + 114 - V_TextWidth(&art.font, second) / 2, rect.y + 25, second);
    if (portrait->owner >= 8) {
        for (int i = 0; i < level.resource_vent_count; ++i) {
            if (level.resource_vents[i].source_id != portrait->id) continue;
            snprintf(hp, sizeof(hp), "Gold: %d", level.resource_vents[i].amount);
            draw_ink(rect.x + 88 - V_TextWidth(&art.font, hp) / 2, rect.y + 86, hp);
        }
        return;
    }
    if (portrait->owner != consoleplayer) return;
    snprintf(hp, sizeof(hp), "%d/%d", portrait->hp, portrait->max_hp);
    V_DrawText((ivec2_t){rect.x + 35 - V_TextWidth(&art.small_font, hp) / 2, rect.y + 61},
               &art.small_font, hp, V_RemapPalette(art.small_font.sprite.source_palette));
    int bar = 50;
    int filled = portrait->max_hp > 0 ? portrait->hp * bar / portrait->max_hp : 0;
    if (filled < 0) filled = 0;
    if (filled > bar) filled = bar;
    V_FillRect((irect_t){ rect.x + 8, rect.y + 51, bar, 7 }, V_NearestIndex(0xff000000u));
    if (filled > 0)
        V_FillRect((irect_t){ rect.x + 9, rect.y + 52, filled > 2 ? filled - 2 : 0, 5 },
                   V_NearestIndex(0xff00fc00u));
    bool site = W2_UnderConstruction(portrait);
    if (site) {
        /* Retail: the bar fills as the structure rises; no number. */
        V_FillRect((irect_t){rect.x + 12, rect.y + 153, 152 * W2_BuildProgress(portrait) / 100, 14},
                   V_NearestIndex(0xff306404u));
        const char *label = "% Complete";
        draw_ink(rect.x + 12 + (152 - V_TextWidth(&art.font, label)) / 2, rect.y + 154, label);
    } else if (type->w2.flags & W2_MOBILE) {
        snprintf(hp, sizeof(hp), "Level %d", type->w2.level);
        draw_ink(rect.x + 154 - V_TextWidth(&art.font, "Level "), rect.y + 41, hp);
        snprintf(hp, sizeof(hp), "%d", type->w2.armor);
        draw_stat((ivec2_t){rect.x + 100, rect.y + 71}, "Armor", hp);
        if (type->damage) {
            snprintf(hp, sizeof(hp), "%d-%d", type->w2.damage_min, type->damage);
            draw_stat((ivec2_t){rect.x + 100, rect.y + 86}, "Damage", hp);
        }
        snprintf(hp, sizeof(hp), "%d", type->w2.attack_range);
        draw_stat((ivec2_t){rect.x + 100, rect.y + 102}, "Range", hp);
        snprintf(hp, sizeof(hp), "%d", type->w2.sight);
        draw_stat((ivec2_t){rect.x + 100, rect.y + 118}, "Sight", hp);
        snprintf(hp, sizeof(hp), "%d", type->w2.speed);
        draw_stat((ivec2_t){rect.x + 100, rect.y + 133}, "Speed", hp);
    } else if (type->w2.flags & W2_HALL) {
        draw_ink(rect.x + 16, rect.y + 71, "Production");
        int gold = W2_ResourceIncome(portrait->owner, 0) - 100;
        snprintf(hp, sizeof(hp), "100%s", gold ? (gold == 20 ? "+20" : "+10") : "");
        draw_stat((ivec2_t){rect.x + 85, rect.y + 86}, "Gold", hp);
        draw_stat((ivec2_t){rect.x + 85, rect.y + 102}, "Lumber", W2_ResourceIncome(portrait->owner, 1) > 100 ? "100+25" : "100");
        draw_stat((ivec2_t){rect.x + 85, rect.y + 118}, "Oil", W2_ResourceIncome(portrait->owner, 2) > 100 ? "100+25" : "100");
    } else if (pud == 58 || pud == 59) {
        int used, have;
        food_counts(&used, &have);
        draw_ink(rect.x + 100 - V_TextWidth(&art.font, "Usage"), rect.y + 71, "Usage");
        snprintf(hp, sizeof(hp), "%d", have);
        draw_stat((ivec2_t){rect.x + 100, rect.y + 86}, "Supply", hp);
        snprintf(hp, sizeof(hp), "%d", used);
        draw_stat((ivec2_t){rect.x + 100, rect.y + 102}, "Demand", hp);
    } else if (pud == 76 || pud == 77) {
        draw_ink(rect.x + 16, rect.y + 86, "Production");
        draw_stat((ivec2_t){rect.x + 85, rect.y + 102}, "Lumber", "100+25");
    }
    if (portrait->production && portrait->production->queue_count) {
        const production_t *prod = portrait->production;
        int completed = prod->time_ms > 0 ? (prod->time_ms - prod->time_left_ms) * 100 / prod->time_ms : 0;
        if (completed < 0) completed = 0;
        if (completed > 100) completed = 100;
        V_FillRect((irect_t){rect.x + 12, rect.y + 153, 152 * completed / 100, 14},
                   V_NearestIndex(0xff306404u));
        /* The native bar shows progress only by fill; the label has no number. */
        const char *label = "% Complete";
        draw_ink(rect.x + 12 + (152 - V_TextWidth(&art.font, label)) / 2, rect.y + 154, label);
    }
}

static void draw_resources(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)item;
    int *res = stock();
    int used = 0, have = 0;
    char text[32];
    int width = menu->app ? menu->app->win.w / R_UIScale(menu->app) : 640;
    food_counts(&used, &have);
    snprintf(text, sizeof(text), "%d", res ? res[0] : 0);
    draw_ink(rect.x + 18, rect.y + 1, text);
    snprintf(text, sizeof(text), "%d", res ? res[1] : 0);
    draw_ink(rect.x + 93, rect.y + 1, text);
    snprintf(text, sizeof(text), "%d", res ? res[2] : 0);
    draw_ink(rect.x + 168, rect.y + 1, text);
    snprintf(text, sizeof(text), "%d/%d", used, have);
    draw_ink(width - 16 - 154 + 18, rect.y + 1, text);
}

static void draw_status(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)item;
    draw_ink(rect.x + 2, rect.y + 2, status_line);
    const menuitem_t *hover = M_MenuHover(menu);
    if (!hover || hover < &items[IT_CMD] || hover >= &items[IT_CMD + 9]) return;
    const cmd_t *cmd = &shown[hover - &items[IT_CMD]];
    const int costs[] = {cmd->gold, cmd->wood, cmd->oil};
    int x = rect.x + 2 + V_TextWidth(&art.font, status_line);
    for (int i = 0; i < 3; ++i) {
        if (!costs[i]) continue;
        irect_t icon = {x + art.font.glyph_width[' '], rect.y + 1, 14, 14};
        R_DrawSprite(&art.resource_icons, i, -1, NULL, &icon, 0, 16);
        char value[16];
        snprintf(value, sizeof(value), "%d", costs[i]);
        draw_ink(icon.x + 18, rect.y + 2, value);
        x = icon.x + 18 + V_TextWidth(&art.font, value);
    }
}

static void draw_slot(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu;
    const mobj_t *unit = slot_unit[item->id];
    if (portrait_count == 1 || !unit || unit->owner != consoleplayer || unit->max_hp <= 0) return;
    irect_t bar = {rect.x - 1, rect.y + 42, 50, 7};
    V_FillRect(bar, V_NearestIndex(0xff000000u));
    int fill = unit->hp * (bar.w - 2) / unit->max_hp;
    V_FillRect((irect_t){bar.x + 1, bar.y + 1, fill, bar.h - 2}, V_NearestIndex(0xff00fc00u));
}

menu_t *G_InitHUD(app_t *app, const char *data_root) {
    const w2_pud_t *pud = level.native_data;
    if (!laid_out) {
        layout();
        hud.refresh = refresh;
        laid_out = true;
    }
    snprintf(root_copy, sizeof(root_copy), "%s", data_root && data_root[0] ? data_root : "data/WAR2");
    w2_load_hud_art(root_copy, pud ? pud->era : 0, orc_side(), &art);
    use_sheet(&items[IT_MENUBACK], &art.menu_button);
    use_sheet(&items[IT_MENU], &art.menu_widgets);
    use_sheet(&items[IT_MAPFRAME], &art.minimap);
    use_sheet(&items[IT_INFO], &art.info);
    use_sheet(&items[IT_PANEL], &art.buttons);
    use_sheet(&items[IT_RESOURCE], &art.resource);
    use_sheet(&items[IT_STATUS], &art.status);
    use_sheet(&items[IT_FILLER], &art.filler);
    items[IT_MENU].font = art.font.sprite.numlumps ? &art.font : NULL;
    items[IT_MENU].ink = 0;
    for (int s = 0; s < MS_STATES; ++s) {
        items[IT_MENU].look[s].cell = s == MS_PUSHED ? 5 : 4;
        items[IT_MENU].look[s].palette = 1;
    }
    for (int i = IT_GOLD; i <= IT_OIL; ++i) items[i].sheet = &art.resource_icons;
    items[IT_MENU].fill = items[IT_MENU].sheet ? 0 : 0xff18242du;
    hud.app = app;
    return &hud;
}

void G_ShutdownHUD(void) {
    hudview = (hudview_t){0};
    w2_free_hud_art(&art);
}
