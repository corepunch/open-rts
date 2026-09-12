#include "game.h"
#include "g_game.h"
#include "engine.h"
#include "info.h"
#include "p_local.h"
#include "w_spr.h"
#include "../rts_model_test.h"
#include <assert.h>

static mobj_t *find(int type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        if (!actor->remove && actor->owner == 0 && actor->type_id == type) return actor;
    }
    return NULL;
}

static void screenshot(app_t *app, SDL_Surface *surface, tileset_t *tiles,
                        spritecache_t *cache, const char *path) {
    mobjlist_t objects = P_ListMobjs();
    R_DrawLevel(app, &level, tiles);
    R_RenderPlayerView(app, &level, tiles, objects.items, objects.count, NULL, cache, gameinfo, 0);
    SDL_RenderPresent(r_renderer);
    assert(!SDL_SaveBMP(surface, path));
    P_FreeMobjList(&objects);
}

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN02.MAP"};
    assert(model && rts_game_model_load(model, &config));
    mobj_t *barracks = find(MT_BRRKPOD), *exploiter = find(MT_EXPLOITER);
    for (int tic = 0; !exploiter && tic < 900; ++tic) {
        assert(rts_tick(model, NULL));
        exploiter = find(MT_EXPLOITER);
    }
    assert(barracks && exploiter && states[barracks->core.state_id].group == 1);
    /* Keep the native delivered Exploiter, city and economy, but isolate this
     * production test from mission attacks that can destroy its Barracks. */
    if (level.destroy_mission) level.destroy_mission(level.mission);
    level.mission = NULL;
    level.destroy_mission = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        if (actor->owner != 0) P_RemoveMobj(actor);
    }
    const StaticProductDefinition *product = G_ModelProductByUIId(model, 89);
    assert(product && product->cost == 350 && G_ModelProductAvailable(model, 0, product));
    RtsGameCommand build = {.kind = RTS_GAME_COMMAND_BUILD_PRODUCT,
                            .data.build_product = {.producer_id = barracks->id, .ui_id = 89}};
    int money = level.player_resources[0][0];
    assert(money < product->cost && !rts_game_model_command(model, &build));
    assert(!barracks->production && level.player_resources[0][0] == money);
    static RtsRenderSnapshot snapshot;
    assert(rts_game_model_snapshot(model, &snapshot));
    int index = rts_find_unit_by_id(&snapshot, exploiter->id);
    RtsGameCommand select = {.kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
                             .data.select_unit_index = {index, false}};
    RtsGameCommand harvest = {.kind = RTS_GAME_COMMAND_HARVEST_SELECTED,
                              .data.harvest_selected = {.target = {69.5f, 48.5f}}};
    assert(index >= 0 && rts_game_model_command(model, &select));
    assert(rts_game_model_command(model, &harvest));
    bool deployed = false;
    for (int tic = 0; tic < 6000 && level.player_resources[0][0] < 2 * product->cost; ++tic) {
        assert(rts_tick(model, NULL));
        deployed |= exploiter->harvest.phase == HARVEST_PHASE_MINING;
    }
    assert(deployed && level.player_resources[0][0] >= 2 * product->cost);
    money = level.player_resources[0][0];
    assert(rts_game_model_command(model, &build) && rts_game_model_command(model, &build));
    assert(level.player_resources[0][0] == money - 2 * product->cost);
    assert(barracks->production && barracks->production->queue_count == 2);

    /* Native exit/standing commands independently determine the handoff point. */
    dc_fin_t hubu, trsc;
    assert(DC_LoadFIN("data/DCOLONY/ANIMATE/HUBU.FIN", &hubu));
    assert(DC_LoadFIN("data/DCOLONY/ANIMATE/TRSC.FIN", &trsc));
    const dc_fin_label_t *release = DC_FINLabel(&hubu, "TRSCBUILD0");
    const dc_fin_label_t *stand = DC_FINLabel(&trsc, "TRSCSTAND8");
    assert(release && stand);
    spritedirection_t final = {0}, idle = {0};
    assert(DC_FINFrame(&hubu, SDL_SwapLE16(release->end), &final));
    assert(DC_FINFrame(&trsc, SDL_SwapLE16(stand->start), &idle));
    ivec2_t delta = ivec2_add(barracks->core.render_offset,
                              ivec2_sub(final.layers[0].offset, idle.layers[0].offset));
    fvec2_t exit = fvec2_add(fixed3_xy_to_fvec2(barracks->core.position),
                            (fvec2_t){delta.x / 32.0f, -delta.y / 32.0f});
    free(final.layers);
    free(idle.layers);
    DC_FreeFIN(&hubu);
    DC_FreeFIN(&trsc);

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(r_renderer);
    app_t app = {.renderer = r_renderer, .win = {640, 480}, .cell = {32, 32}};
    fvec2_t screen;
    R_MapPositionToScreen(&app, &level, barracks->core.position, &screen.x, &screen.y);
    app.cam = fvec2_sub((fvec2_t){320, 300}, screen);
    tileset_t tiles = {0};
    spritesheet_t fallback = {0};
    assert(W_LoadAssets(r_renderer, "data/DCOLONY", &level, "SPRITES/TROOPER1.SPR", &tiles, &fallback));
    spritecache_t *cache = calloc(1, sizeof(*cache));
    assert(cache && R_InitSprites(r_renderer, "data/DCOLONY", &level, NULL, 0, cache));
    screenshot(&app, surface, &tiles, cache, "/private/tmp/barracks-closed.bmp");
    int releases = 0, frames = 0, built = 0;
    uint32_t built_ids[2] = {0};
    for (int tic = 0; tic < 900 && built < 2; ++tic) {
        assert(rts_tick(model, NULL));
        mobj_t *release = find(MT_PRODUCTION_RELEASE);
        if (release && release->core.state_id >= S_BRRKPOD_BUILD_TRSC1 &&
            release->core.state_id <= S_BRRKPOD_BUILD_TRSC22) {
            if (release->core.state_id == S_BRRKPOD_BUILD_TRSC1 &&
                release->core.tics == states[S_BRRKPOD_BUILD_TRSC1].tics) releases++;
            assert(release->core.sprite_id == SPR_HUBU && !barracks->production->release_ready);
            assert(states[barracks->core.state_id].group == 1);
            assert(barracks->core.frame == states[S_BRRKPOD_STND].frame ||
                   barracks->core.frame == states[S_BRRKPOD_STND_2].frame);
            assert(!memcmp(&release->core.position, &barracks->core.position, sizeof(fixed3_t)));
            assert(release->traits == (MF_RENDERABLE | MF_NOBLOCKMAP));
            frames++;
            if (releases == 1 && release->core.state_id == S_BRRKPOD_BUILD_TRSC4)
                screenshot(&app, surface, &tiles, cache, "/private/tmp/barracks-open.bmp");
            if (releases == 1 && release->core.state_id == S_BRRKPOD_BUILD_TRSC18)
                screenshot(&app, surface, &tiles, cache, "/private/tmp/barracks-exit.bmp");
        }
        RtsGameEvent event;
        while (rts_game_model_poll_event(model, &event)) {
            if (event.type != RTS_GAME_EVENT_UNIT_BUILT || event.target_id != barracks->id) continue;
            assert(built < 2 && frames == 44 * (built + 1));
            assert(states[barracks->core.state_id].group == 1);
            mobjlist_t objects = P_ListMobjs();
            mobj_t *trooper = NULL;
            for (int i = 0; i < objects.count; ++i)
                if (objects.items[i]->id == event.subject_id) trooper = objects.items[i];
            assert(trooper && trooper->type_id == MT_TROOPER && trooper->thinker.function == P_MobjThinker);
            assert(trooper->hp == trooper->max_hp && trooper->team == barracks->team);
            assert(fvec2_near(fixed3_xy_to_fvec2(trooper->core.position), exit, 0.0001f));
            assert(trooper->core.sprite_id == SPR_TRSC && trooper->core.angle == ANG90);
            built_ids[built++] = trooper->id;
            P_FreeMobjList(&objects);
            screenshot(&app, surface, &tiles, cache, "/private/tmp/barracks-released.bmp");
        }
    }
    assert(built == 2 && releases == 2 && built_ids[0] != built_ids[1] && !barracks->production);
    /* Exercise the actual sidebar and driver production path as well. */
    for (int tic = 0; tic < 3000 && level.player_resources[0][0] < product->cost; ++tic)
        assert(rts_tick(model, NULL));
    assert(level.player_resources[0][0] >= product->cost);
    mobjlist_t objects = P_ListMobjs();
    for (int i = 0; i < objects.count; ++i) P_MobjSetSelected(objects.items[i], false);
    void *ui = G_InitCustomUI(&app, "data/DCOLONY");
    assert(ui);
    SDL_Event click = {.button = {.type = SDL_MOUSEBUTTONDOWN,
                                  .button = SDL_BUTTON_LEFT, .x = 530, .y = 165}};
    money = level.player_resources[0][0];
    assert(G_CustomUIResponder(ui, &app, &level, objects.items, objects.count, &click));
    assert(barracks->production && barracks->production->queue_count == 1);
    assert(level.player_resources[0][0] == money - product->cost);
    P_FreeMobjList(&objects);
    int blocked_cell = (int)exit.y * level.width + (int)exit.x;
    uint8_t saved_block = level.blocked[blocked_cell];
    int blocked_ticks = 0;
    bool finished = false, blocked = false;
    for (int tic = 0; tic < 600 && !finished; ++tic) {
        mobj_t *release = find(MT_PRODUCTION_RELEASE);
        if (release && release->core.state_id == S_BRRKPOD_BUILD_TRSC22 && release->core.tics == 1) {
            level.blocked[blocked_cell] = 1;
            blocked = true;
        }
        P_Ticker();
        objects = P_ListMobjs();
        finished = G_UpdateProduction(ui, &level, objects.items, &objects.count, FIXED_DT);
        P_FreeMobjList(&objects);
        if (blocked && blocked_ticks < 3 && barracks->production && barracks->production->release_ready) {
            assert(!finished); /* A blocked native exit must never fall back to a random cell. */
            if (++blocked_ticks == 3) level.blocked[blocked_cell] = saved_block;
        }
    }
    assert(finished && blocked_ticks == 3 && !barracks->production);
    mobj_t *trooper = (mobj_t *)thinkercap.prev;
    assert(trooper->type_id == MT_TROOPER && trooper->core.state_id == S_TRSC_STND);
    assert(fvec2_near(fixed3_xy_to_fvec2(trooper->core.position), exit, 0.0001f));
    /* No selection: native fixed slots remain empty until prerequisites exist. */
    objects = P_ListMobjs();
    for (int i = 0; i < objects.count; ++i) P_MobjSetSelected(objects.items[i], false);
    level.player_resources[0][0] = 10000;
    G_CustomUIDrawer(ui, &app, &level, objects.items, objects.count, cache, NULL);
    SDL_RenderPresent(r_renderer);
    assert(!SDL_SaveBMP(surface, "/private/tmp/dc-build-menu.bmp"));
    mobj_t *center = find(MT_EXCOPOD);
    assert(center && !center->production);
    click.button.x = 530;
    click.button.y = 125; /* Exploiter keeps the first unit slot. */
    assert(G_CustomUIResponder(ui, &app, &level, objects.items, objects.count, &click));
    assert(center->production && center->production->actor_id == MT_EXPLOITER);
    assert(level.player_resources[0][0] == 8500);
    level.player_resources[0][0] = 10000;
    click.button.x = 590;
    click.button.y = 290; /* MAINE control 81: Sci-Pod at (577,276). */
    assert(!find(MT_SCNCPOD));
    assert(G_CustomUIResponder(ui, &app, &level, objects.items, objects.count, &click));
    mobj_t *science = find(MT_SCNCPOD), *exco = find(MT_EXCOPOD);
    assert(science && exco && level.player_resources[0][0] == 8000);
    assert(science->core.state_id == S_SCNCPOD_BUILD1);
    /* Native slot 3 is (64,10): cancel it without adding a terrain row. */
    assert(ivec2_equal(science->core.render_offset, (ivec2_t){-64, 10}));
    assert(fvec2_near(fixed3_xy_to_fvec2(science->core.position),
                      fvec2_add(fixed3_xy_to_fvec2(exco->core.position),
                                (fvec2_t){4.0f, -5.0f / 32.0f}), 0.0001f));
    P_FreeMobjList(&objects);
    objects = P_ListMobjs();
    assert(G_CustomUIResponder(ui, &app, &level, objects.items, objects.count, &click));
    assert(level.player_resources[0][0] == 8000); /* No duplicate module. */
    P_FreeMobjList(&objects);
    for (int tic = 0; tic < 300 && states[science->core.state_id].group == 6; ++tic)
        assert(rts_tick(model, NULL));
    assert(states[science->core.state_id].group == 1);
    level.player_resources[0][0] = 20000;
    static const struct { int ui_y, type, state, cost; const char *image; } modules[] = {
        {330, MT_ROBOPOD, S_ROBOPOD_BUILD1, 2000, "/private/tmp/dc-factory-construction.bmp"},
        {290, MT_SCNCPOD2, S_SCNCPOD2_BUILD1, 2000, NULL},
        {330, MT_ROBOPOD2, S_ROBOPOD2_BUILD1, 2000, NULL},
        {370, MT_RSCHPOD, S_RSCHPOD_BUILD1, 3000, "/private/tmp/dc-research-construction.bmp"},
    };
    for (size_t i = 0; i < sizeof(modules) / sizeof(modules[0]); ++i) {
        click.button.y = modules[i].ui_y;
        objects = P_ListMobjs();
        money = level.player_resources[0][0];
        assert(G_CustomUIResponder(ui, &app, &level, objects.items, objects.count, &click));
        P_FreeMobjList(&objects);
        mobj_t *module = find(modules[i].type);
        assert(module && module->core.state_id == modules[i].state);
        assert(level.player_resources[0][0] == money - modules[i].cost);
        objects = P_ListMobjs();
        assert(G_CustomUIResponder(ui, &app, &level, objects.items, objects.count, &click));
        P_FreeMobjList(&objects);
        assert(level.player_resources[0][0] == money - modules[i].cost);
        for (int tic = 0; tic < 300 && states[module->core.state_id].group == 6; ++tic) {
            if (tic == 50 && modules[i].image)
                screenshot(&app, surface, &tiles, cache, modules[i].image);
            assert(rts_tick(model, NULL));
        }
        assert(states[module->core.state_id].group == 1);
    }
    G_ShutdownCustomUI(ui);
    R_FreeSpriteCache(cache);
    free(cache);
    R_FreeTileset(&tiles);
    R_FreeSprite(&fallback);
    R_FreeSpriteBuffer();
    SDL_DestroyRenderer(r_renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    rts_game_model_destroy(model);
    puts("PASS: native Human02 mining pays for two queued Troopers; complete door/exit FIN, exact mobj handoff, sidebar build and blocked-exit retry");
    return 0;
}
