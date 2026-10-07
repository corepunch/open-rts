#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II HUD", #c)

static int test_minimap(menu_t *menu, const tileset_t *tiles, mobj_t *worker) {
    const menuitem_t *map = NULL;
    for (int i = 0; i < menu->numitems; ++i)
        if (menu->items[i].kind == MI_MINIMAP) map = &menu->items[i];
    CHECK(map && map->ownerdraw);
    irect_t rect = M_MenuItemRect(menu, map);
    hudview_t saved = hudview;
    fvec2_t camera = menu->app->cam;
    menu->app->cam = (fvec2_t){100000, 100000}; /* Keep the viewport outside this terrain check. */
    hudview.unit_count = 0;
    map->ownerdraw(menu, map, rect);
    uint8_t terrain[128 * 128];
    CHECK(rect.w == 128 && rect.h == 128);
    int visible = 0;
    for (int y = 0; y < rect.h; ++y)
        for (int x = 0; x < rect.w; ++x) {
            ivec2_t cell = {x * level.width / rect.w, y * level.height / rect.h};
            uint8_t pixel = screens[0].pixels[(rect.y + y) * screens[0].w + rect.x + x];
            terrain[y * rect.w + x] = pixel;
            if (P_SightBrightness(&level, cell) == 0) {
                CHECK(pixel == V_NearestIndex(0xff000000u));
                continue;
            }
            int tile = tiles->tile_lookup[level.tile_ids[L_Index(&level, cell.x, cell.y)]];
            int sx = 7 + ((x * 100) % (rect.w * 100 / level.width)) / 100 * 8;
            int sy = 6 + ((y * 100) % (rect.h * 100 / level.height)) / 100 * 8;
            CHECK(pixel == tiles->indices[((size_t)tile * tiles->tile_h + sy) * tiles->tile_w + sx]);
            ++visible;
        }
    CHECK(visible > 100);
    hudview.units = &worker;
    hudview.unit_count = 1;
    map->ownerdraw(menu, map, rect);
    fvec2_t position = fvec2_sub(fixed3_xy_to_fvec2(worker->core.position), (fvec2_t){0.5f, 0.5f});
    ivec2_t corner = {(int)(position.x * rect.w / level.width) + 1,
                     (int)(position.y * rect.h / level.height) + 1};
    isize2_t footprint = {rect.w / level.width + 1, rect.h / level.height + 1};
    for (int y = 0; y < rect.h; ++y)
        for (int x = 0; x < rect.w; ++x) {
            uint8_t expected = irect_contains((irect_t){corner.x, corner.y, footprint.w, footprint.h},
                                             (ivec2_t){x, y}) ? V_NearestIndex(0xff00ff00u) : terrain[y * rect.w + x];
            CHECK(screens[0].pixels[(rect.y + y) * screens[0].w + rect.x + x] == expected);
        }
    hudview = saved;
    menu->app->cam = camera;
    M_MenuDrawer(menu);
    return 0;
}

static bool save_bmp(const char *path) {
    SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0, screens[0].w, screens[0].h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!image) return false;
    for (int y = 0; y < screens[0].h; ++y) {
        uint32_t *row = (uint32_t *)((uint8_t *)image->pixels + y * image->pitch);
        for (int x = 0; x < screens[0].w; ++x) row[x] = vpalette[screens[0].pixels[y * screens[0].w + x]];
    }
    SDL_Surface *rgb = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_RGB24, 0);
    bool ok = rgb && SDL_SaveBMP(rgb, path) == 0;
    SDL_FreeSurface(rgb);
    SDL_FreeSurface(image);
    return ok;
}

static int test_button_frames(menu_t *menu) {
    int icons = 0;
    for (int i = 0; i < menu->numitems; ++i) {
        const menuitem_t *item = &menu->items[i];
        if (!item->visible || !item->frame.outer) continue;
        irect_t r = M_MenuItemRect(menu, item);
        CHECK(r.w == 46 && r.h == 38);
        CHECK(screens[0].pixels[(r.y - 2) * 640 + r.x - 2] == V_NearestIndex(0xff000000u));
        CHECK(screens[0].pixels[(r.y - 1) * 640 + r.x - 1] == V_NearestIndex(0xfffcfcfcu));
        CHECK(screens[0].pixels[(r.y + r.h) * 640 + r.x + r.w] == V_NearestIndex(0xfffcfcfcu));
        ++icons;
    }
    CHECK(icons > 0);
    return 0;
}

static int test_info_layouts(menu_t *menu, mobj_t *worker) {
    mobj_t *other = NULL, *hall = NULL;
    for (int i = 0; i < hudview.unit_count; ++i) {
        mobj_t *unit = hudview.units[i];
        if (unit != worker && unit->owner == consoleplayer) other = unit;
        if (unit->owner == consoleplayer && unit->type_id == MT_TOWN_HALL) hall = unit;
    }
    CHECK(other && hall);
    P_MobjSetSelected(other, true);
    M_MenuDrawer(menu);
    RTS_RUN(test_button_frames(menu));
    int portraits = 0;
    for (int i = 0; i < menu->numitems; ++i) {
        const menuitem_t *item = &menu->items[i];
        if (item->visible && item->kind == MI_BUTTON && item->frame.outer && item->rect.y < 336)
            ++portraits;
        if (item->rect.x == 0 && item->rect.y == 160) CHECK(item->look[MS_NORMAL].cell == 0);
    }
    CHECK(portraits == 2);
    P_MobjSetSelected(other, false);
    P_MobjSetSelected(worker, false);
    P_MobjSetSelected(hall, true);
    CHECK(G_BuildOrder(hall, 3));
    M_MenuDrawer(menu);
    RTS_RUN(test_button_frames(menu));
    bool training = false;
    for (int i = 0; i < menu->numitems; ++i) {
        const menuitem_t *item = &menu->items[i];
        if (item->rect.x == 0 && item->rect.y == 160) CHECK(item->look[MS_NORMAL].cell == 3);
        if (item->rect.x == 110 && item->rect.y == 241) training = item->visible && item->frame.outer;
    }
    CHECK(training);
    P_MobjSetSelected(hall, false);
    P_MobjSetSelected(worker, true);
    M_MenuDrawer(menu);
    return 0;
}

static const menuitem_t *command_slot(const menu_t *menu, int slot) {
    static const int col_x[3] = { 9, 65, 121 }, cmd_y[3] = { 340, 387, 434 };
    for (int i = 0; i < menu->numitems; ++i) {
        const menuitem_t *it = &menu->items[i];
        if (it->kind == MI_BUTTON && it->rect.x == col_x[slot % 3] && it->rect.y == cmd_y[slot / 3]) return it;
    }
    return NULL;
}

static const menuitem_t *training_slot(const menu_t *menu) {
    for (int i = 0; i < menu->numitems; ++i)
        if (menu->items[i].rect.x == 110 && menu->items[i].rect.y == 241) return &menu->items[i];
    return NULL;
}

static void relist(mobjlist_t *units) {
    P_FreeMobjList(units);
    *units = P_ListMobjs();
    hudview.units = units->items;
    hudview.unit_count = units->count;
}

/* A blacksmith offers the next weapon and shield tiers, hides them while it
 * researches, and the hall offers its upgrade once the barracks stands. */
static int test_research_buttons(menu_t *menu, mobjlist_t *units, mobj_t *worker, bool orc) {
    for (int i = 0; i < units->count; ++i) P_MobjSetSelected(units->items[i], false);
    fvec2_t at = fvec2_add(fixed3_xy_to_fvec2(worker->core.position), (fvec2_t){3, 3});
    mobj_t *smith = P_SpawnMobj(fixed3_from_fvec2(at, 0), orc ? MT_ORC_BLACKSMITH : MT_HUMAN_BLACKSMITH);
    CHECK(smith);
    smith->owner = (uint8_t)consoleplayer;
    smith->team = (uint8_t)consoleplayer;
    smith->allegiance = ALLEGIANCE_PLAYER;
    relist(units);
    P_MobjSetSelected(smith, true);
    M_MenuDrawer(menu);
    const menuitem_t *weapon = command_slot(menu, 0), *shield = command_slot(menu, 1), *third = command_slot(menu, 2);
    CHECK(weapon && shield && third && weapon->visible && shield->visible && third->visible);
    CHECK(third->look[MS_NORMAL].cell == (orc ? 138 : 140));
    CHECK(weapon->look[MS_NORMAL].cell == (orc ? 120 : 117) && shield->look[MS_NORMAL].cell == (orc ? 168 : 165));
    CHECK(weapon->tooltip && !strcmp(weapon->tooltip, orc ? "Upgrade battle axe" : "Upgrade sword"));
    CHECK(shield->tooltip && !strcmp(shield->tooltip, "Upgrade shield"));
    RTS_RUN(test_button_frames(menu));
    int *stock = level.player_resources[consoleplayer];
    int gold = stock[0], lumber = stock[1];
    stock[0] = 1000;
    stock[1] = 500;
    CHECK(G_BuildOrder(smith, orc ? W2_UI_AXE1 : W2_UI_SWORD1));
    CHECK(smith->production && smith->production->product_class == RTS_PRODUCT_UPGRADE);
    CHECK(stock[0] == (orc ? 500 : 200) && stock[1] == (orc ? 400 : 500));
    M_MenuDrawer(menu);
    CHECK(!command_slot(menu, 0)->visible && !command_slot(menu, 1)->visible);
    const menuitem_t *training = training_slot(menu);
    CHECK(training && training->visible && training->look[MS_NORMAL].cell == (orc ? 120 : 117));
    W2_ApplyUpgrade(consoleplayer, orc ? W2_UPGRADE_AXE1 : W2_UPGRADE_SWORD1);
    P_FreeMobjProduction(smith);
    M_MenuDrawer(menu);
    CHECK(!training_slot(menu)->visible);
    CHECK(command_slot(menu, 0)->visible && command_slot(menu, 0)->look[MS_NORMAL].cell == (orc ? 121 : 118));
    CHECK(command_slot(menu, 1)->visible && command_slot(menu, 1)->look[MS_NORMAL].cell == (orc ? 168 : 165));
    level.upgrades[orc ? MT_GRUNT : MT_FOOTMAN][consoleplayer].weapon = 0;
    P_MobjSetSelected(smith, false);
    /* The map's town hall: its keep button needs a human barracks. */
    mobj_t *hall = NULL;
    for (int i = 0; i < units->count; ++i)
        if (units->items[i]->owner == consoleplayer && units->items[i]->type_id == MT_TOWN_HALL) hall = units->items[i];
    CHECK(hall);
    const StaticProductDefinition *keep = G_ModelProductByUIId(NULL, W2_UI_KEEP);
    bool has_barracks = G_ModelHasActorType(NULL, consoleplayer, MT_HUMAN_BARRACKS);
    CHECK(G_ModelProductAvailable(NULL, consoleplayer, keep) == has_barracks);
    P_FreeMobjProduction(hall); /* A hall still training offers no upgrade. */
    P_MobjSetSelected(hall, true);
    M_MenuDrawer(menu);
    CHECK(command_slot(menu, 0)->visible && command_slot(menu, 1)->visible == has_barracks);
    mobj_t *barracks = NULL;
    if (!has_barracks) {
        barracks = P_SpawnMobj(fixed3_from_fvec2(fvec2_add(at, (fvec2_t){4, 0}), 0), MT_HUMAN_BARRACKS);
        CHECK(barracks);
        barracks->owner = (uint8_t)consoleplayer;
        barracks->team = (uint8_t)consoleplayer;
        barracks->allegiance = ALLEGIANCE_PLAYER;
        relist(units);
        M_MenuDrawer(menu);
    }
    const menuitem_t *upgrade = command_slot(menu, 1);
    CHECK(upgrade->visible && upgrade->look[MS_NORMAL].cell == 66 && !strcmp(upgrade->tooltip, "Upgrade to keep"));
    P_MobjSetSelected(hall, false);
    stock[0] = gold;
    stock[1] = lumber;
    P_RemoveMobj(smith);
    if (barracks) P_RemoveMobj(barracks);
    P_RunThinkers();
    relist(units);
    P_MobjSetSelected(worker, true);
    M_MenuDrawer(menu);
    return 0;
}

/* The build page sends the worker off with a construct command; the site it
 * raises offers only Cancel and shows the progress panel. */
static int test_construction_buttons(menu_t *menu, app_t *app, mobjlist_t *units, mobj_t *worker, bool orc) {
    for (int i = 0; i < units->count; ++i) P_MobjSetSelected(units->items[i], false);
    P_MobjSetSelected(worker, true);
    W2_InterruptHarvest(worker);
    worker->harvest.phase = HARVEST_PHASE_NONE;
    worker->harvest.target = -1;
    M_MenuDrawer(menu);
    const menuitem_t *page = command_slot(menu, 6);
    CHECK(page && page->visible && page->hotkey == SDLK_b);
    uint16_t farm = orc ? MT_PIG_FARM : MT_FARM;
    /* A clear 2x2 spot near the worker, reached through the camera. */
    ivec2_t at = fvec2_cell(fixed3_xy_to_fvec2(worker->core.position)), cell = {-1, -1};
    for (int dy = -4; dy <= 4 && cell.x < 0; ++dy)
        for (int dx = -4; dx <= 4 && cell.x < 0; ++dx)
            if ((dx || dy) && W2_CanPlace(farm, (ivec2_t){at.x + dx, at.y + dy}, worker)) cell = (ivec2_t){at.x + dx, at.y + dy};
    CHECK(cell.x >= 0);
    SDL_Event key = {.type = SDL_KEYDOWN};
    key.key.keysym.sym = SDLK_b;
    netactive = true;
    CHECK(t_hud_event(menu, app, units->items, units->count, &key));
    M_MenuDrawer(menu);
    const menuitem_t *farm_button = command_slot(menu, 0);
    CHECK(farm_button->visible && farm_button->hotkey == SDLK_f && farm_button->look[MS_NORMAL].cell == (orc ? 39 : 38));
    key.key.keysym.sym = SDLK_f;
    CHECK(t_hud_event(menu, app, units->items, units->count, &key));
    CHECK(menu->target == farm_button);
    fvec2_t screen;
    R_MapToScreen(app, &level, cell.x + 0.5f, cell.y + 0.5f, &screen.x, &screen.y);
    SDL_Event click = {.type = SDL_MOUSEBUTTONDOWN};
    click.button.button = SDL_BUTTON_LEFT;
    click.button.x = (int)screen.x;
    click.button.y = (int)screen.y;
    CHECK(t_hud_event(menu, app, units->items, units->count, &click));
    CHECK(!menu->target);
    ticcmd_t command;
    G_BuildTiccmd(&command);
    netactive = false;
    CHECK(command.order == TC_CONSTRUCT && command.product == farm && command.count == 1 && command.units[0] == worker->id);
    CHECK((command.position.x >> FIXED_FRAC_BITS) == cell.x && (command.position.y >> FIXED_FRAC_BITS) == cell.y);
    int *stock = level.player_resources[consoleplayer];
    int gold = stock[0], lumber = stock[1];
    G_RunTiccmd(consoleplayer, &command);
    CHECK(worker->w2.build_phase == W2_BUILD_TO_SITE);
    for (int i = 0; i < 900 && worker->w2.build_phase == W2_BUILD_TO_SITE; ++i) P_Ticker();
    mobj_t *site = P_MobjById(worker->w2.site);
    CHECK(site && W2_UnderConstruction(site) && stock[0] == gold - 500 && stock[1] == lumber - 250);
    relist(units);
    P_MobjSetSelected(site, true);
    M_MenuDrawer(menu);
    const menuitem_t *cancel = command_slot(menu, 8);
    CHECK(cancel && cancel->visible && cancel->look[MS_NORMAL].cell == 91 && cancel->hotkey == SDLK_ESCAPE);
    CHECK(!strcmp(cancel->tooltip, "Cancel construction") && !command_slot(menu, 0)->visible);
    CHECK(!P_MobjIsSelected(worker)); /* The builder inside left the selection. */
    CHECK(!training_slot(menu)->visible);
    for (int i = 0; i < menu->numitems; ++i)
        if (menu->items[i].rect.x == 0 && menu->items[i].rect.y == 160) CHECK(menu->items[i].look[MS_NORMAL].cell == 3);
    key.key.keysym.sym = SDLK_ESCAPE;
    netactive = true;
    CHECK(t_hud_event(menu, app, units->items, units->count, &key));
    G_BuildTiccmd(&command);
    netactive = false;
    CHECK(command.order == TC_CONSTRUCT && command.product == 0 && command.units[0] == site->id);
    uint32_t site_id = site->id;
    G_RunTiccmd(consoleplayer, &command);
    CHECK(!P_MobjById(site_id) && stock[0] == gold && stock[1] == lumber);
    CHECK(worker->w2.build_phase == W2_BUILD_NONE && (worker->traits & MF_SELECTABLE));
    P_RunThinkers();
    relist(units);
    P_MobjSetSelected(worker, true);
    M_MenuDrawer(menu);
    return 0;
}

static int test_hud(const char *capture, bool orc) {
    G_InitGame();
    P_InitThinkers();
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level));
    CHECK(P_LoadThings(NULL) == 64);
    ((w2_pud_t *)level.native_data)->sides[consoleplayer] = orc ? 1 : 0;
    CHECK(P_InitSight());
    P_UpdateSight();
    mobjlist_t units = P_ListMobjs();
    mobj_t *worker_unit = NULL;
    for (int i = 0; i < units.count; ++i)
        if (units.items[i]->owner == consoleplayer && units.items[i]->type_id == 3) {
            worker_unit = units.items[i];
            P_MobjSetSelected(worker_unit, true);
            break;
        }
    CHECK(worker_unit);
    if (orc) {
        worker_unit->type_id = 4;
        worker_unit->info = &actor_types[3];
    }
    app_t app = {.win = {640, 480}, .cell = {32, 32}};
    tileset_t tiles = {0};
    spritesheet_t unit = {0};
    CHECK(W_LoadAssets("data/WAR2", &level, "footman", &tiles, &unit));
    spritecache_t sprites = {0};
    CHECK(R_InitSprites("data/WAR2", &level, units.items, units.count, &sprites));
    fvec2_t position = fixed3_xy_to_fvec2(worker_unit->core.position);
    app.cam = fvec2_sub((fvec2_t){400, 240}, fvec2_scale(position, 32));
    V_AllocScreen(app.win.w, app.win.h);
    I_SetPalette(tiles.palette);
    hudview = (hudview_t){.units = units.items, .unit_count = units.count, .sprites = &sprites, .tileset = &tiles};
    R_DrawLevel(&app, &level, &tiles);
    R_RenderPlayerView(&app, &level, &tiles, units.items, units.count, &unit, &sprites, gameinfo, 0);
    R_DrawFog(&app, &level);
    menu_t *menu = G_InitHUD(&app, "data/WAR2");
    CHECK(menu);
    M_MenuDrawer(menu);
    w2_hud_art_t native = {0};
    CHECK(w2_load_hud_art("data/WAR2", 0, orc, &native));
    /* Every untouched command-panel pixel must be the native retail pixel. */
    int compared = 0;
    for (int y = 336; y < 480; ++y)
        for (int x = 0; x < 176; ++x) {
            bool button = false;
            for (int i = 0; i < menu->numitems; ++i) {
                const menuitem_t *it = &menu->items[i];
                irect_t occupied = it->rect;
                if (it->frame.outer) occupied = (irect_t){occupied.x - 2, occupied.y - 2, occupied.w + 4, occupied.h + 4};
                if (it->visible && it->kind == MI_BUTTON && it->rect.y >= 340 && irect_contains(occupied, (ivec2_t){x,y}))
                    button = true;
            }
            if (button) continue;
            CHECK(screens[0].pixels[y * 640 + x] == native.buttons.lumps[0].indices[(y - 336) * 176 + x]);
            ++compared;
        }
    CHECK(compared > 176 * 144 / 4);
    CHECK(command_slot(menu, 8)->visible && !strcmp(command_slot(menu, 8)->tooltip, "Board transport"));
    for (int frame = 0; frame < 3; ++frame) {
        const spritecell_t *cell = &native.resource_icons.cells[frame];
        for (int y = 0; y < cell->rect.h; ++y)
            for (int x = 0; x < cell->rect.w; ++x) {
                uint8_t index = native.resource_icons.lumps[frame].indices[y * cell->rect.w + x];
                if (index) CHECK(screens[0].pixels[y * 640 + 176 + frame * 75 + x] == index);
            }
    }
    CHECK(native.small_font.glyph_size.h < native.font.glyph_size.h);
    CHECK(native.font.sprite.source_palette[2] == tiles.palette[246]);
    CHECK(native.font.sprite.source_palette[13] == tiles.palette[192]);
    CHECK(native.info.numlumps == 4 && native.menu_widgets.numlumps == 50);
    CHECK(native.spell_icons.numlumps == W2_BUFF_COUNT);
    for (int i = 0; i < W2_BUFF_COUNT; ++i)
        CHECK(native.spell_icons.cells[i].rect.w == 16 && native.spell_icons.cells[i].rect.h == 16);
    CHECK(memcmp(native.info.lumps[1].indices, native.info.lumps[2].indices, 176 * 176) == 0);
    CHECK(memcmp(native.info.lumps[0].indices, native.info.lumps[1].indices, 176 * 176) != 0);
    CHECK(memcmp(native.info.lumps[1].indices, native.info.lumps[3].indices, 176 * 176) != 0);
    RTS_RUN(test_button_frames(menu));
    for (int i = 0; i < menu->numitems; ++i) {
        const menuitem_t *item = &menu->items[i];
        if (item->rect.x == 24 && item->rect.y == 2) {
            CHECK(item->sheet && item->look[MS_NORMAL].cell == 4 && item->look[MS_PUSHED].cell == 5);
            CHECK(item->sheet->cells[4].rect.w == 128 && item->sheet->cells[4].rect.h == 20);
        }
        if (item->rect.x == 0 && item->rect.y == 160) CHECK(item->look[MS_NORMAL].cell == 1);
    }
    RTS_RUN(test_info_layouts(menu, worker_unit));
    RTS_RUN(test_research_buttons(menu, &units, worker_unit, orc));
    RTS_RUN(test_minimap(menu, &tiles, worker_unit));
    /* Last: the simulated minute of walking changes the ground. */
    RTS_RUN(test_construction_buttons(menu, &app, &units, worker_unit, orc));
    if (capture) CHECK(save_bmp(capture));
    /* A neutral mine must produce a context harvest command, not an attack. */
    int mine_hit = -1;
    ivec2_t click = {0};
    for (int y = 16; y < 464 && mine_hit < 0; y += 4)
        for (int x = 176; x < 624 && mine_hit < 0; x += 4) {
            int hit = R_PickUnit(&app, &level, units.items, units.count, &unit, &sprites, gameinfo, x, y, -1);
            if (hit >= 0 && units.items[hit]->type_id == 93) {
                mine_hit = hit;
                click = (ivec2_t){x, y};
            }
        }
    CHECK(mine_hit >= 0);
    SDL_Event event = {.type = SDL_MOUSEBUTTONDOWN};
    event.button.button = SDL_BUTTON_RIGHT;
    event.button.x = click.x;
    event.button.y = click.y;
    netactive = true;
    G_Responder(&app, &level, units.items, units.count, &unit, &sprites, gameinfo, &event);
    ticcmd_t command;
    G_BuildTiccmd(&command);
    CHECK(command.order == TC_ORDER && command.count == 1 && command.units[0] == worker_unit->id);
    netactive = false;
    G_RunTiccmd(consoleplayer, &command);
    CHECK(worker_unit->harvest.phase == HARVEST_PHASE_TO_MINE);
    app.win = (isize2_t){800, 600};
    V_AllocScreen(app.win.w, app.win.h);
    M_MenuDrawer(menu);
    bool bottom = false, right = false, wide = false;
    for (int i = 0; i < menu->numitems; ++i) {
        const menuitem_t *it = &menu->items[i];
        irect_t r = M_MenuItemRect(menu, it);
        if (it->anchor & MANCHOR_BOTTOM) bottom |= r.y == 584;
        if (it->anchor & MANCHOR_RIGHT) right |= r.x == 784 && r.h == 600;
        if (it->anchor & MANCHOR_WIDE) wide |= r.w == 608;
    }
    CHECK(bottom && right && wide);
    w2_free_hud_art(&native);
    G_ShutdownHUD();
    V_FreeScreen();
    R_FreeTileset(&tiles);
    R_FreeSprite(&unit);
    R_FreeSpriteCache(&sprites);
    P_FreeMobjList(&units);
    P_FreeLevel(&level);
    return 0;
}

int main(int argc, char **argv) {
    RTS_RUN(test_hud(argc > 1 ? argv[1] : NULL, false));
    RTS_RUN(test_hud(argc > 2 ? argv[2] : NULL, true));
    puts("PASS: native human/orc HUD pixels, minimap terrain/footprints, resource icons, fonts, anchors, research, upgrade and construction buttons");
    return 0;
}
