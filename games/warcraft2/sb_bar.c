#include "engine.h"
#include "info.h"
#include "w2_local.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Left column from the retail 640x480 layout: menu, minimap, info, commands,
 * then the resource and status bars over the map. Coordinates match the
 * measured MAINDAT pieces. */

enum {
    IT_MENU, IT_MAPFRAME, IT_MAP, IT_INFO, IT_PANEL,
    IT_RESOURCE, IT_STATUS, IT_FILLER,
    IT_CMD, IT_SLOT = IT_CMD + 9, IT_COUNT = IT_SLOT + 9
};

enum {
    CK_NONE, CK_MOVE, CK_STOP, CK_ATTACK, CK_PATROL, CK_STAND,
    CK_REPAIR, CK_HARVEST, CK_RETURN, CK_PAGE, CK_CANCEL, CK_TRAIN, CK_PLACE
};

typedef struct {
    int kind, icon, arg, gold, wood, oil;
    SDL_Keycode key;
    const char *tip;
} cmd_t;

typedef struct {
    int pud, icon, gold, wood, oil;
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

/* Costs are the Wargus units.lua numbers. Pos 6 of the basic page is empty.
 * Orc buildings are the next PUD type and the next icon. */
static const bld_t basic_page[9] = {
    { 58, 38, 500, 250, 0, SDLK_f, "Build farm", "Build pig farm" },
    { 60, 42, 700, 450, 0, SDLK_b, "Build barracks", "Build barracks" },
    { 74, 40, 1200, 800, 0, SDLK_h, "Build town hall", "Build great hall" },
    { 76, 44, 600, 450, 0, SDLK_l, "Build lumber mill", "Build lumber mill" },
    { 82, 46, 800, 450, 100, SDLK_s, "Build blacksmith", "Build blacksmith" },
    { -1, -1, 0, 0, 0, 0, NULL, NULL },
    { 64, 60, 550, 200, 0, SDLK_t, "Build tower", "Build tower" },
    { 103, 92, 0, 0, 0, SDLK_w, "Build wall", "Build wall" },
    { -1, 91, 0, 0, 0, SDLK_ESCAPE, "Cancel", NULL },
};
static const bld_t advanced_page[9] = {
    { 72, 48, 800, 450, 0, SDLK_s, "Build shipyard", "Build shipyard" },
    { 78, 52, 700, 400, 400, SDLK_f, "Build foundry", "Build foundry" },
    { 84, 50, 800, 350, 200, SDLK_r, "Build refinery", "Build refinery" },
    { 68, 58, 1000, 400, 0, SDLK_i, "Build inventor", "Build alchemist" },
    { 66, 56, 1000, 300, 0, SDLK_a, "Build stables", "Build ogre mound" },
    { 80, 64, 1000, 200, 0, SDLK_m, "Build mage tower", "Build temple" },
    { 62, 62, 900, 500, 0, SDLK_c, "Build church", "Build altar of storms" },
    { 70, 72, 1000, 400, 0, SDLK_g, "Build gryphon aviary", "Build dragon roost" },
    { -1, 91, 0, 0, 0, SDLK_ESCAPE, "Cancel", NULL },
};

static menuitem_t items[IT_COUNT];
static menu_t hud = { .items = items, .numitems = IT_COUNT, .itemOn = -1 };
static w2_hud_art_t art;
static cmd_t shown[9];
static char tipbuf[9][80];
static char note[96];
static char status_line[96];
static char root_copy[1024];
static mobj_t *slot_unit[9];
static const mobj_t *portrait;
static int portrait_count;
static int page;
static uint32_t command_id;
static bool laid_out;

static void on_menu(menu_t *menu, menuitem_t *item, menuaction_t action);
static void on_command(menu_t *menu, menuitem_t *item, menuaction_t action);
static void on_slot(menu_t *menu, menuitem_t *item, menuaction_t action);
static void draw_minimap(const menu_t *menu, const menuitem_t *item, irect_t rect);
static void draw_info(const menu_t *menu, const menuitem_t *item, irect_t rect);
static void draw_resources(const menu_t *menu, const menuitem_t *item, irect_t rect);
static void draw_status(const menu_t *menu, const menuitem_t *item, irect_t rect);

irect_t G_WorldViewport(const app_t *app) {
    int w = app && app->win.w > 192 ? app->win.w - 192 : 1;
    int h = app && app->win.h > 32 ? app->win.h - 32 : 1;
    return (irect_t){ 176, 16, w, h };
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
        uint8_t tint[256];
        V_ModulateRemap(tint, art.font.sprite.source_palette, 0xffffe84au);
        V_DrawText((ivec2_t){ x, y }, &art.font, text, tint);
        return;
    }
    V_DrawSmallText((irect_t){ x, y, 180, 8 }, text, 0xffffe84au,
                    (isize2_t){ screens[0].w, screens[0].h });
}

static void draw_icon(int frame, int x, int y) {
    if (frame < 0 || frame >= art.icons.numlumps || !art.icons.cells) return;
    irect_t dst = { x, y, art.icons.cells[frame].rect.w, art.icons.cells[frame].rect.h };
    R_DrawSprite(&art.icons, frame, -1, NULL, &dst, 0, 16);
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
    items[IT_MENU].enabled = true;
    items[IT_MENU].routine = on_menu;
    snprintf(items[IT_MENU].text, sizeof(items[IT_MENU].text), "Menu (F10)");
    items[IT_MENU].inset = (ivec2_t){ 24, 2 };
    items[IT_MENU].ink = 0xffffe84au;
    items[IT_MAP].kind = MI_MINIMAP;
    items[IT_MAP].enabled = true;
    items[IT_MAP].ownerdraw = draw_minimap;
    items[IT_INFO].ownerdraw = draw_info;
    items[IT_RESOURCE].ownerdraw = draw_resources;
    items[IT_STATUS].ownerdraw = draw_status;
    static const int col_x[3] = { 9, 65, 121 };
    static const int cmd_y[3] = { 340, 387, 434 };
    static const int slot_y[3] = { 169, 223, 277 };
    for (int i = 0; i < 9; ++i) {
        menuitem_t *cmd = &items[IT_CMD + i];
        menuitem_t *slot = &items[IT_SLOT + i];
        *cmd = (menuitem_t){
            .kind = MI_BUTTON, .routine = on_command,
            .rect = { col_x[i % 3], cmd_y[i / 3], 46, 38 },
        };
        *slot = (menuitem_t){
            .kind = MI_BUTTON, .id = i, .routine = on_slot,
            .rect = { col_x[i % 3], slot_y[i / 3], 46, 38 },
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
    char *buf = tipbuf[slot];
    buf[0] = '\0';
    if (tip) {
        int n = snprintf(buf, sizeof(tipbuf[slot]), "%s", tip);
        if (gold && n > 0 && n < (int)sizeof(tipbuf[slot]))
            n += snprintf(buf + n, sizeof(tipbuf[slot]) - (size_t)n, "  %dg", gold);
        if (wood && n > 0 && n < (int)sizeof(tipbuf[slot]))
            n += snprintf(buf + n, sizeof(tipbuf[slot]) - (size_t)n, " %dl", wood);
        if (oil && n > 0 && n < (int)sizeof(tipbuf[slot]))
            snprintf(buf + n, sizeof(tipbuf[slot]) - (size_t)n, " %do", oil);
    }
    shown[slot] = (cmd_t){ kind, icon, arg, gold, wood, oil, key, buf[0] ? buf : NULL };
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
        put_cmd(i, CK_PLACE, icon, pud, bld->gold, bld->wood, bld->oil, bld->key, tip);
    }
}

static void fill_train(const mobj_t *unit) {
    int type = unit->type_id;
    if (type == 75 || type == 89 || type == 91 || type == 76 || type == 90 || type == 92) {
        bool orc = type == 76 || type == 90 || type == 92;
        int pud = orc ? 3 : 2;
        put_cmd(0, CK_TRAIN, icon_of(pud), orc ? 4 : 3, 400, 0, 0, SDLK_p,
                orc ? "Train peon" : "Train peasant");
        return;
    }
    if (type != 61 && type != 62) return;
    bool orc = type == 62;
    static const int human_pud[] = { 0, 8, 4, 6 };
    static const int human_ui[] = { 1, 5, 7, 9 };
    static const int wood[] = { 0, 50, 300, 100 };
    static const SDL_Keycode keys[] = { SDLK_f, SDLK_a, SDLK_b, SDLK_k };
    static const char *human_tip[] = { "Train footman", "Train archer", "Train ballista", "Train knight" };
    static const char *orc_tip[] = { "Train grunt", "Train axethrower", "Train catapult", "Train ogre" };
    for (int i = 0; i < 4; ++i) {
        int pud = human_pud[i] + (orc ? 1 : 0);
        int ui = human_ui[i] + (orc ? 1 : 0);
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui);
        put_cmd(i, CK_TRAIN, icon_of(pud), ui, product ? product->cost : 0, wood[i], 0,
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
    for (int i = 0; i < 9; ++i) {
        menuitem_t *slot = &items[IT_SLOT + i];
        bool show = count > 1 && slot_unit[i];
        slot->visible = slot->enabled = show;
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

static bool foot_clear(int x, int y, isize2_t foot) {
    for (int yy = 0; yy < foot.h; ++yy)
        for (int xx = 0; xx < foot.w; ++xx) {
            int cx = x + xx, cy = y + yy;
            if (!L_Contains(&level, cx, cy)) return false;
            int index = L_Index(&level, cx, cy);
            if (!level.cell_terrain || level.cell_terrain[index] != 0) return false;
            if (level.cell_solid && level.cell_solid[index]) return false;
        }
    return true;
}

static fvec2_t cursor_goal(const menu_t *menu) {
    cell_t cell = R_ScreenToMapGrid(menu->app, &level, menu->cursor.x, menu->cursor.y);
    return (fvec2_t){ cell.x + 0.5f, cell.y + 0.5f };
}

static mobj_t *nearest_hall(fvec2_t from) {
    mobj_t *best = NULL;
    float best_d = 0.0f;
    for (int i = 0; i < hudview.unit_count; ++i) {
        mobj_t *unit = hudview.units[i];
        int pud;
        if (!unit || unit->remove || unit->hp <= 0 || unit->owner != consoleplayer) continue;
        pud = (int)unit->type_id - 1;
        if (pud < 0 || pud >= W2_TYPE_COUNT || !(w2_units[pud].flags & W2_HALL)) continue;
        fvec2_t at = fixed3_xy_to_fvec2(unit->core.position);
        float dx = at.x - from.x, dy = at.y - from.y, d = dx * dx + dy * dy;
        if (!best || d < best_d) { best = unit; best_d = d; }
    }
    return best;
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

static void place_building(menu_t *menu, menuitem_t *item, const cmd_t *cmd) {
    int pud = cmd->arg;
    if (pud < 0 || pud >= W2_TYPE_COUNT || !w2_units[pud].name ||
        (w2_units[pud].flags & W2_SKIP)) {
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
    cell_t cell = R_ScreenToMapGrid(menu->app, &level, menu->cursor.x, menu->cursor.y);
    isize2_t foot = { w2_units[pud].tw > 0 ? w2_units[pud].tw : 1,
                      w2_units[pud].th > 0 ? w2_units[pud].th : 1 };
    if (!foot_clear(cell.x, cell.y, foot)) {
        set_note("Cannot build there.");
        M_MenuTarget(menu, item);
        return;
    }
    fvec2_t at = { cell.x + foot.w * 0.5f, cell.y + foot.h * 0.5f };
    mobj_t *built = P_SpawnMobj(fixed3_from_fvec2(at, 0), (uint16_t)(pud + 1));
    if (!built) {
        set_note("Cannot build there.");
        M_MenuTarget(menu, item);
        return;
    }
    built->owner = consoleplayer;
    built->team = consoleplayer < 8 ? consoleplayer : 8;
    built->allegiance = ALLEGIANCE_PLAYER;
    built->core.angle = ANG270;
    w2_mark_footprint(cell.x, cell.y, foot);
    res[0] -= cmd->gold;
    res[1] -= cmd->wood;
    res[2] -= cmd->oil;
    if (hudview.sprites)
        w2_cache_unit_sprite(root_copy, (spritecache_t *)hudview.sprites, pud);
    set_note("");
    if (res[0] >= cmd->gold && res[1] >= cmd->wood && res[2] >= cmd->oil)
        M_MenuTarget(menu, item);
}

static void train_product(mobj_t *producer, const cmd_t *cmd) {
    int *res = stock();
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, cmd->arg);
    if (!producer || !res || !product || res[0] < product->cost ||
        res[1] < cmd->wood || res[2] < cmd->oil) {
        set_note("Not enough resources.");
        return;
    }
    if (!G_QueueProduct(producer, product)) {
        set_note("Cannot train.");
        return;
    }
    res[1] -= cmd->wood;
    res[2] -= cmd->oil;
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
        mobj_t *from = NULL;
        for (int i = 0; i < hudview.unit_count; ++i)
            if (hudview.units[i] && P_MobjIsSelected(hudview.units[i]) &&
                hudview.units[i]->owner == consoleplayer) { from = hudview.units[i]; break; }
        mobj_t *hall = from ? nearest_hall(fixed3_xy_to_fvec2(from->core.position)) : NULL;
        if (!hall) { set_note("No hall to receive goods."); break; }
        G_SelectedTiccmd(TC_MOVE, hudview.units, hudview.unit_count,
                         fixed3_xy_to_fvec2(hall->core.position), 0);
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
        if (w2_units[pud].flags & W2_CRITTER) continue;
        if (pud == 58 || pud == 59) *have += 4;
        else if (w2_units[pud].flags & W2_HALL) *have += 1;
        if ((w2_units[pud].flags & W2_MOBILE) && !(w2_units[pud].flags & W2_STRUCTURE))
            *used += 1;
    }
}

static void draw_minimap(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)item;
    if (!menu->app || level.width <= 0 || level.height <= 0 || rect.w <= 0 || rect.h <= 0) return;
    uint8_t land = V_NearestIndex(0xff6a8a28u);
    uint8_t water = V_NearestIndex(0xff184878u);
    uint8_t forest = V_NearestIndex(0xff143014u);
    uint8_t unseen = V_NearestIndex(0xff000000u);
    uint8_t own = V_NearestIndex(0xffe0d060u);
    uint8_t enemy = V_NearestIndex(0xffc03030u);
    uint8_t neutral = V_NearestIndex(0xffc0c0c0u);
    for (int py = 0; py < rect.h; ++py) {
        int gy = L_ScreenY(&level, py * level.height / rect.h);
        for (int px = 0; px < rect.w; ++px) {
            int gx = px * level.width / rect.w;
            uint8_t color = unseen;
            if (L_Contains(&level, gx, gy) && P_SightBrightness(&level, (ivec2_t){ gx, gy }) > 0) {
                uint8_t terrain = level.cell_terrain ? level.cell_terrain[L_Index(&level, gx, gy)] : 0;
                color = terrain == 1 ? water : terrain == 2 ? forest : land;
            }
            V_DrawPoint((ivec2_t){ rect.x + px, rect.y + py }, color);
        }
    }
    for (int i = 0; i < hudview.unit_count; ++i) {
        const mobj_t *unit = hudview.units[i];
        int pud;
        if (!unit || unit->remove || unit->hp <= 0 || !P_VisibleToPlayer(unit)) continue;
        pud = (int)unit->type_id - 1;
        if (pud >= 0 && pud < W2_TYPE_COUNT && (w2_units[pud].flags & W2_CRITTER)) continue;
        fvec2_t pos = fixed3_xy_to_fvec2(unit->core.position);
        int x = rect.x + (int)(pos.x * (float)rect.w / (float)level.width);
        int y = rect.y + (int)(L_ScreenYF(&level, pos.y) * (float)rect.h / (float)level.height);
        uint8_t color = unit->owner == consoleplayer ? own : unit->owner >= 8 ? neutral : enemy;
        V_FillRect((irect_t){ x, y, 2, 2 }, color);
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
    V_DrawRectOutline((irect_t){ vx, vy, vw, vh }, own);
}

static void draw_info(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    if (portrait_count != 1 || !portrait) return;
    int pud = (int)portrait->type_id - 1;
    draw_icon(icon_of(pud), rect.x + 9, rect.y + 9);
    char name[64], hp[32];
    pretty_name(pud >= 0 && pud < W2_TYPE_COUNT ? w2_units[pud].name : NULL, name, sizeof(name));
    draw_ink(rect.x + 58, rect.y + 12, name);
    snprintf(hp, sizeof(hp), "%d / %d", portrait->hp, portrait->max_hp);
    draw_ink(rect.x + 58, rect.y + 28, hp);
    int bar = 158;
    int filled = portrait->max_hp > 0 ? portrait->hp * bar / portrait->max_hp : 0;
    if (filled < 0) filled = 0;
    if (filled > bar) filled = bar;
    V_FillRect((irect_t){ rect.x + 9, rect.y + 50, bar, 5 }, V_NearestIndex(0xff101010u));
    if (filled > 0)
        V_FillRect((irect_t){ rect.x + 9, rect.y + 50, filled, 5 }, V_NearestIndex(0xff20c020u));
}

static void draw_resources(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)item;
    int *res = stock();
    int used = 0, have = 0;
    char text[32];
    int width = menu->app ? menu->app->win.w : 640;
    food_counts(&used, &have);
    snprintf(text, sizeof(text), "%d", res ? res[0] : 0);
    draw_ink(rect.x + 18, rect.y + 1, text);
    snprintf(text, sizeof(text), "%d", res ? res[1] : 0);
    draw_ink(rect.x + 93, rect.y + 1, text);
    snprintf(text, sizeof(text), "%d", res ? res[2] : 0);
    draw_ink(rect.x + 168, rect.y + 1, text);
    snprintf(text, sizeof(text), "%d/%d", used, have);
    draw_ink(width - 16 - 154 + 18, rect.y + 1, text);
    draw_ink(width - 16 - 84 + 18, rect.y + 1, "0");
}

static void draw_status(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    draw_ink(rect.x + 2, rect.y + 2, status_line);
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
    use_sheet(&items[IT_MENU], &art.menu_button);
    use_sheet(&items[IT_MAPFRAME], &art.minimap);
    use_sheet(&items[IT_INFO], &art.info);
    use_sheet(&items[IT_PANEL], &art.buttons);
    use_sheet(&items[IT_RESOURCE], &art.resource);
    use_sheet(&items[IT_STATUS], &art.status);
    use_sheet(&items[IT_FILLER], &art.filler);
    items[IT_MENU].font = art.font.sprite.numlumps ? &art.font : NULL;
    items[IT_MENU].ink = 0xffffe84au;
    items[IT_MENU].fill = items[IT_MENU].sheet ? 0 : 0xff18242du;
    hud.app = app;
    return &hud;
}

void G_ShutdownHUD(void) {
    w2_free_hud_art(&art);
}
