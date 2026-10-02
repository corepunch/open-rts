/* Every war map must deliver a purchased Exploiter / Brozaar: retail shows the
 * badge for the release time and then the harvester leaves the base. Runs the
 * ticcmd purchase path for each start of every multiplayer map, both races,
 * against a human (netplay) and an AI (single-player war) opponent. */
#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "t_local.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICKS_PER_SECOND 30
enum { UI_EXPLOITER = 87, UI_BROZAAR = 46 };

static int count_harvesters(int owner) {
    int n = 0;
    mobjlist_t l = P_ListMobjs();
    for (int i = 0; i < l.count; ++i)
        n += l.items[i]->owner == owner && !l.items[i]->remove && l.items[i]->hp > 0 &&
             (l.items[i]->traits & MF_HARVESTER);
    P_FreeMobjList(&l);
    return n;
}

/* The executable's tic body (driver/d_main.c): its custom UI production
 * update, not the model's G_ProductionTicker. */
static void driver_tic(menu_t *ui, AiContext *ai) {
    P_Ticker();
    mobjlist_t objects = P_ListMobjs();
    int count = objects.count;
    P_AiTick(ai, &level, objects.items, count, gameinfo, (int)(FIXED_DT * 1000));
    G_UpdateProduction(&level, objects.items, &count, FIXED_DT);
    P_FreeMobjList(&objects);
}

static int compare(const void *a, const void *b) { return strcmp(a, b); }

/* Returns 0 if the harvester appears, -1 for a start without a base (the
 * Atlantis map), else prints why and returns 1. */
static int buy(RtsGameModel *model, void *hud, const char *map, int race, int player, bool vs_ai) {
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = map};
    dc_skirmish_t setup = {.quantity = 4, .flow = 4, .erupting = 1};
    for (int i = 0; i < 8; ++i)
        setup.players[i] = (dc_skirmish_player_t){.type = DC_PLAYER_NONE, .race = race, .color = i, .team = i};
    setup.players[player].type = DC_PLAYER_HUMAN;
    setup.players[!player].type = vs_ai ? DC_PLAYER_AI : DC_PLAYER_HUMAN;
    DC_RequestSkirmish(map, &setup);
    if (!rts_game_model_load(model, &config)) {
        fprintf(stderr, "%s: load failed\n", map);
        return 1;
    }
    int ui = race ? UI_BROZAAR : UI_EXPLOITER;
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui);
    if (!G_FindProducer(player, product)) return -1;
    int before = count_harvesters(player), row = product->row_id;
    netactive = !vs_ai; consoleplayer = player;
    G_RunTiccmd(player, &(ticcmd_t){.order = TC_PURCHASE, .product = ui});
    G_RunTiccmd(player, &(ticcmd_t){.order = TC_SUBMIT});
    AiContext ai;
    P_AiInit(&ai);
    P_AiAttachGame(&ai, G_AiInterface());
    for (int tic = 0; tic < 3 * TICKS_PER_SECOND && count_harvesters(player) == before; ++tic)
        driver_tic(hud, &ai);
    netactive = false; consoleplayer = 0;
    if (count_harvesters(player) == before + 1 && level.purchases[player][row].queued == 0) return 0;
    mobj_t *base = G_FindProducer(player, product);
    fprintf(stderr, "%s race %d slot %d vs %s: no %s after 3 s (queued %d, base %s",
            map, race, player, vs_ai ? "AI" : "human", product->label,
            level.purchases[player][row].queued, base ? "found" : "missing");
    if (base && base->production)
        fprintf(stderr, ", production queue %d left %d ms blocked %d",
                base->production->queue_count, base->production->time_left_ms,
                base->production->blocked);
    fprintf(stderr, ")\n");
    return 1;
}

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return rts_fail("war_harvester", "SDL video init");
    static char maps[128][64];
    int count = 0;
    DIR *dir = opendir("data/DCOLONY/SCENARIO/MPLAYER");
    if (!dir) return rts_fail("war_harvester", "multiplayer maps are present");
    for (struct dirent *e; count < 128 && (e = readdir(dir));) {
        size_t n = strlen(e->d_name);
        if (n > 4 && !strcmp(e->d_name + n - 4, ".MAP"))
            snprintf(maps[count++], sizeof(maps[0]), "SCENARIO/MPLAYER/%s", e->d_name);
    }
    closedir(dir);
    qsort(maps, count, sizeof(maps[0]), compare);
    int failures = 0, runs = 0, baseless = 0;
    RtsGameModel *model = rts_game_model_create();
    app_t app = {.win = {640, 480}, .cell = {32, 32}, .running = true};
    menu_t *ui = G_InitHUD(&app, "data/DCOLONY");
    if (!ui) return rts_fail("war_harvester", "HUD loads");
    for (int m = 0; m < count; ++m)
        for (int race = 0; race < 2; ++race)
            for (int player = 0; player < 2; ++player)
                for (int ai = 0; ai < 2; ++ai) {
                    int result = buy(model, ui, maps[m], race, player, ai);
                    if (result < 0) { ++baseless; continue; }
                    failures += result;
                    ++runs;
                }
    rts_game_model_destroy(model);
    if (failures) {
        fprintf(stderr, "%d of %d war starts never delivered the harvester\n", failures, runs);
        return rts_fail("war_harvester", "every war start delivers a purchased harvester");
    }
    printf("PASS: %d war starts on %d maps deliver a purchased Exploiter/Brozaar "
           "(%d starts have no base)\n", runs, count, baseless);
    return 0;
}
