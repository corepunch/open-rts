#include "t_local.h"
#include "warcraft-2.h"
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
    CHECK(compared > 10000);
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
    RTS_RUN(test_minimap(menu, &tiles, worker_unit));
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
    puts("PASS: native human/orc HUD pixels, minimap terrain/footprints, resource icons, fonts and anchors");
    return 0;
}
