#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "t_local.h"
#include <assert.h>

/* The second player's Gene-Sac finishes its only Sy-Demon. Spawning the last
 * unit frees the producer's queue, which the production loop must not read
 * again (an ASan build caught the use after free). The HUD then refreshes
 * and a right click deselects the new unit. */
int main(void) {
    netgame = true;
    doomcom->numplayers = 2;
    consoleplayer = 1;
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY",
        .map_path = "SCENARIO/MPLAYER/D2PLAY01.MAP"};
    assert(model && rts_game_model_load(model, &config));
    V_AllocScreen(640, 480);
    app_t app = {.win = {640,480}, .cell = {32,32}, .running = true};
    spritecache_t *cache = calloc(1, sizeof(*cache));
    menu_t *ui = G_InitHUD(&app, "data/DCOLONY");
    assert(cache && ui);
    mobjlist_t objects = P_ListMobjs();
    mobj_t *hive = NULL;
    for (int i = 0; i < objects.count; ++i)
        if (objects.items[i]->type_id == MT_ALIEN_MINDHIVE && objects.items[i]->owner == 1)
            hive = objects.items[i];
    assert(hive);
    const int buildings[] = {MT_ALIEN_WARHIVE, MT_ALIEN_BRDRHIVE, MT_ALIEN_MINDHIVE2};
    mobj_t *sac = NULL;
    for (int k = 0; k < 3; ++k) {
        mobj_t *building = P_SpawnMobj(hive->core.position, buildings[k]);
        assert(building);
        building->owner = 1;
        if (buildings[k] == MT_ALIEN_BRDRHIVE) sac = building;
    }
    level.player_resources[1][0] = 100000;
    G_RunTiccmd(1, &(ticcmd_t){.order = TC_PURCHASE, .product = 50}); /* Sy-Demon */
    G_RunTiccmd(1, &(ticcmd_t){.order = TC_SUBMIT});
    assert(sac->production && sac->production->queue_count == 1);
    mobj_t *demon = NULL;
    for (int tic = 0; tic < 3000 && !demon; ++tic) {
        P_Ticker();
        P_FreeMobjList(&objects);
        objects = P_ListMobjs();
        int count = objects.count;
        G_UpdateProduction(&level, objects.items, &count, RTS_TICK_MS);
        P_FreeMobjList(&objects);
        objects = P_ListMobjs();
        for (int i = 0; i < objects.count; ++i)
            if (objects.items[i]->type_id == MT_SY_DEMON) demon = objects.items[i];
    }
    assert(demon && demon->owner == 1 && !sac->production);

    assert(R_InitSprites("data/DCOLONY", &level, objects.items, objects.count, cache));
    hudtext_t log = {0};
    P_MobjSetSelected(demon, true);
    t_hud_draw(ui, &app, objects.items, objects.count, cache, &log);
    SDL_Event e = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_RIGHT,
                              .x = 200, .y = 200}};
    if (!t_hud_event(ui, &app, objects.items, objects.count, &e))
        G_Responder(&app, &level, objects.items, objects.count, NULL, cache, gameinfo, &e);
    assert(!P_MobjIsSelected(demon));
    t_hud_draw(ui, &app, objects.items, objects.count, cache, &log);

    P_FreeMobjList(&objects);
    G_ShutdownHUD();
    R_FreeSpriteCache(cache); free(cache);
    rts_game_model_destroy(model);
    V_FreeScreen();
    puts("PASS: second player's last Sy-Demon frees its queue once; right click deselects it");
    return 0;
}
