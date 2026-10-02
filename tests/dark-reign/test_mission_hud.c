#include "t_local.h"
#include "engine.h"
#include "info.h"
#include "dark-reign.h"

#define CHECK(c) RTS_CHECK(c, "dark-reign mission HUD", #c)

static int compare_buildings(spritecache_t *cache) {
    const char *bodies[] = {"nfhqt1l0.spr", "nclnc1l0.spr", "ncpow1l0.spr"};
    const char *tops[] = {"tfhqt1l0.spr", "tclnc1l0.spr", "tcpow1l0.spr"};
    mapdecoration_t decorations[3] = {0};
    for (int i = 0; i < 3; ++i) {
        snprintf(decorations[i].sprite_name, 32, "tileset|%s", bodies[i]);
        snprintf(decorations[i].sprite2_name, 32, "base|%s", bodies[i]);
        snprintf(decorations[i].sprite3_name, 32, "base|%s", tops[i]);
    }
    level_t fixture = level;
    fixture.decorations = decorations;
    fixture.decoration_count = 3;
    CHECK(R_InitSprites(g_game_default_root, &fixture, NULL, 0, cache));
    int pixels = 0;
    for (int i = 0; i < 3; ++i) {
        const spritesheet_t *result = R_CacheLookup(cache, bodies[i]);
        const spritesheet_t *layers[] = {
            R_CacheLookup(cache, decorations[i].sprite_name),
            R_CacheLookup(cache, decorations[i].sprite2_name),
            R_CacheLookup(cache, decorations[i].sprite3_name),
        };
        CHECK(result && layers[0] && layers[1] && layers[2]);
        for (int y = 0; y < result->frame_size.h; ++y)
            for (int x = 0; x < result->frame_size.w; ++x) {
                uint32_t expected = 0;
                for (int layer = 2; layer >= 0; --layer) {
                    const spritesheet_t *s = layers[layer];
                    if (x >= s->frame_size.w || y >= s->frame_size.h) continue;
                    int frame = s->numlumps > 1 ? 1 : 0;
                    int index = s->lumps[frame].indices[y*s->frame_size.w+x];
                    if (index) { expected = s->palette[index]; break; }
                }
                CHECK(result->palette[result->lumps[0].indices[y*result->frame_size.w+x]] == expected);
                ++pixels;
            }
    }
    printf("PASS: %d completed-building pixels match the native three-layer presentation\n", pixels);
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {0};
    CHECK(model && rts_game_model_load(model, &config));
    CHECK(!strcmp(g_game_default_map, "scenario/FIXED/M01F/M01F.SCN"));
    CHECK(level.width == 60 && level.height == 60);
    CHECK(level.player_resources[0][0] == 4000);
    CHECK(level.has_camera && level.camera.x == 225.0f/24 && level.camera.y == 1160.0f/24);
    CHECK(G_ModelHasActorType(model, 0, MT_FG_HQ1));
    CHECK(G_ModelHasActorType(model, 0, MT_FG_LIFE_PLANT));
    CHECK(G_ModelHasActorType(model, 0, MT_FG_POWER_PLANT));
    CHECK(!G_ModelHasActorType(model, 0, MT_FG_CONSTRUCTION_CREW));
    CHECK(DR_ProductInTech(11) && DR_ProductInTech(13) && DR_ProductInTech(9) && DR_ProductInTech(1));
    CHECK(!DR_ProductInTech(10) && !DR_ProductInTech(14));
    CHECK(gameui->command_rows == 5 && gameui->icon_size.w == 64 && gameui->icon_size.h == 50);
    StaticProductDefinition products[128];
    int product_count = G_ModelGetProducts(model, 0, products, 128);
    CHECK(product_count > 64 && product_count <= 128);
    for (int i = 0; i < product_count; ++i) {
        int matches = 0;
        for (int j = 0; j < gameui->product_count; ++j)
            matches += gameui->products[j].id == products[i].ui_id;
        CHECK(matches == 1);
    }
    irect_t radar = DR_MinimapRect(&level);
    CHECK(radar.x == 488 && radar.y == 381 && radar.w == 60 && radar.h == 60);

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640,480,32,SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    app_t app = {.win = {640,480}, .cell = {24,24}};
    V_AllocScreen(app.win.w, app.win.h);
    CHECK(screens[0].pixels);
    /* The HUD draws over a level, whose tileset palette is the screen palette. */
    tileset_t tiles = {0};
    spritesheet_t fallback = {0};
    CHECK(W_LoadAssets(g_game_default_root, &level, g_game_default_sprite, &tiles, &fallback));
    I_SetPalette(tiles.palette);
    V_BeginFrame(0xff000000u);
    sb_state_t *bar = G_InitCustomUI(&app, g_game_default_root);
    CHECK(bar && bar->ready);
    mobjlist_t objects = P_ListMobjs();
    spritecache_t *cache = calloc(1, sizeof(*cache));
    CHECK(cache && R_InitSprites(g_game_default_root, &level, objects.items, objects.count, cache));
    const spritesheet_t *hq = R_CacheLookup(cache, "nfhqt1l0.spr");
    CHECK(hq && hq->numlumps == 1 && hq->cells[0].ground_point.x == 0 && hq->cells[0].ground_point.y == 0);
    CHECK(compare_buildings(cache) == 0);
    for (int i = 0; i < objects.count; ++i) P_MobjSetSelected(objects.items[i], false);
    G_CustomUIDrawer(bar, &app, &level, objects.items, objects.count, cache, NULL);
    V_ReadPixels(surface->pixels, surface->pitch);
    CHECK(SDL_SaveBMP(surface, "/private/tmp/open-rts-mission01-hud.bmp") == 0);
    SDL_Event click = {.type = SDL_MOUSEBUTTONDOWN};
    click.button.button = SDL_BUTTON_LEFT;
    click.button.x = 458; click.button.y = 74;
    CHECK(G_CustomUIResponder(bar, &app, &level, objects.items, objects.count, &click));
    CHECK(level.player_resources[0][0] == 3700); /* First native slot builds a rig at the HQ. */
    click.button.x = 522; /* Freighter is unavailable without an assembly plant. */
    CHECK(G_CustomUIResponder(bar, &app, &level, objects.items, objects.count, &click));
    CHECK(level.player_resources[0][0] == 3700);
    click.button.x = 590; click.button.y = 10;
    /* MENU hands over to the native options screen (none before M_Init). */
    CHECK(G_CustomUIResponder(bar, &app, &level, objects.items, objects.count, &click));
    CHECK(!bar->options_visible && !menuactive);
    /* The selected Imperium rig must expose structures, just like the FG rig. */
    dr_mission_t *mission = level.mission;
    mission->product_count = 1;
    mission->products[0].type = 11001;
    mission->products[0].tech_level = 0;
    mobj_t *rig = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){10, 10}, 0), MT_IMP_CONSTRUCTION_CREW);
    CHECK(rig);
    rig->owner = rig->team = 0;
    rig->allegiance = ALLEGIANCE_PLAYER;
    P_MobjSetSelected(rig, true);
    click.button.x = 458; click.button.y = 74;
    CHECK(G_CustomUIResponder(bar, &app, &level, objects.items, objects.count, &click));
    CHECK(level.player_resources[0][0] == 2950);
    SDL_Event release = click;
    release.type = SDL_MOUSEBUTTONUP;
    CHECK(G_CustomUIResponder(bar, &app, &level, objects.items, objects.count, &release));
    SDL_Event motion = {.motion = {.type = SDL_MOUSEMOTION}};
    CHECK(!G_CustomUIResponder(bar, &app, &level, objects.items, objects.count, &motion));
    G_CustomUIDrawer(bar, &app, &level, objects.items, objects.count, cache, NULL);
    V_ReadPixels(surface->pixels, surface->pitch);
    CHECK(SDL_SaveBMP(surface, "/private/tmp/open-rts-imperium-hud.bmp") == 0);
    click.button.x = 530; click.button.y = 45;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    CHECK(bar->page == DR_PAGE_PATHS);
    click.button.x = 515; click.button.y = 108;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    CHECK(bar->order == UI_WAYPOINT);
    click.button.x = 100; click.button.y = 150;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    click.button.x = 150;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    CHECK(bar->path.count == 2 && !rig->waypoints.count);
    click.button.x = 570; click.button.y = 78; /* Native Advanced path page. */
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    CHECK(bar->path_advanced);
    click.button.x = 560; click.button.y = 212;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    CHECK(bar->path.mode == WP_LOOP);
    click.button.x = 480; click.button.y = 312;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    CHECK(bar->saved_path_count == 1 && bar->saved_paths[0].count == 2);
    click.button.x = 480; click.button.y = 132;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    CHECK(!bar->path.count && bar->saved_paths[0].count == 2);
    click.button.x = 563; click.button.y = 261;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    CHECK(bar->path.count == 2 && bar->saved_path_selection == 0);
    click.button.x = 520; click.button.y = 160;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    CHECK(rig->waypoints.count == 2 && rig->waypoints.mode == WP_LOOP);
    G_CustomUIDrawer(bar,&app,&level,objects.items,objects.count,cache,NULL);
    V_ReadPixels(surface->pixels, surface->pitch);
    CHECK(SDL_SaveBMP(surface,"/private/tmp/open-rts-paths-hud.bmp") == 0);
    SDL_Event key = {.key = {.type = SDL_KEYDOWN,.keysym.sym = SDLK_s}};
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&key));
    CHECK(!rig->waypoints.count && !P_HasMoveOrder(rig));
    /* The saved-path widget owns wheel scrolling even with the native font. */
    bar->saved_path_count = 12;
    for (int i = 1; i < bar->saved_path_count; ++i) bar->saved_paths[i] = bar->saved_paths[0];
    motion.motion.x = 563; motion.motion.y = 261;
    G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&motion);
    SDL_Event wheel = {.wheel = {.type = SDL_MOUSEWHEEL, .y = -2}};
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&wheel));
    click.button.x = 563; click.button.y = 261;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    CHECK(bar->saved_path_selection == 2);
    /* Controls use scaled rectangles. */
    app.win = (isize2_t){1280,960};
    key.key.keysym.sym = SDLK_b;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&key));
    CHECK(bar->page == DR_PAGE_BUILD);
    key.key.keysym.sym = SDLK_x;
    CHECK(!G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&key));
    /* Use the driver's routing: Escape cancels the HUD before opening a menu. */
    CHECK(M_Init(&app, g_game_default_root));
    key.key.keysym.sym = SDLK_m;
    CHECK(!D_MenuResponder(&app,&key,bar,objects.items,objects.count));
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&key));
    CHECK(bar->order == UI_MOVE);
    key.key.keysym.sym = SDLK_ESCAPE;
    CHECK(D_MenuResponder(&app,&key,bar,objects.items,objects.count));
    CHECK(bar->order == UI_UNAVAILABLE && !menuactive);
    click.button.x = 1180; click.button.y = 20;
    CHECK(G_CustomUIResponder(bar,&app,&level,objects.items,objects.count,&click));
    CHECK(!bar->options_visible && menuactive); /* the shell's options screen */
    CHECK(D_MenuResponder(&app,&key,bar,objects.items,objects.count));
    CHECK(!menuactive);
    /* With no HUD cancellation, Escape opens the options screen. While that
     * menu is open it takes Escape, leaving a pending HUD order alone. */
    CHECK(D_MenuResponder(&app,&key,bar,objects.items,objects.count));
    CHECK(menuactive);
    bar->order = UI_ATTACK;
    CHECK(D_MenuResponder(&app,&key,bar,objects.items,objects.count));
    CHECK(!menuactive && bar->order == UI_ATTACK);
    CHECK(D_MenuResponder(&app,&key,bar,objects.items,objects.count));
    CHECK(!menuactive && bar->order == UI_UNAVAILABLE);
    M_Shutdown();
    G_ShutdownCustomUI(bar);
    R_FreeSpriteCache(cache); free(cache);
    P_FreeMobjList(&objects);
    R_FreeSprite(&fallback);
    R_FreeTileset(&tiles);
    V_FreeScreen();
    SDL_FreeSurface(surface);
    rts_game_model_destroy(model);
    SDL_Quit();
    puts("PASS: Mission 01 start, native HUD assets, technology, production slots and MENU");
    return 0;
}
