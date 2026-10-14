#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "t_local.h"
#include <stdio.h>
#include <string.h>

/* A mission's end: its script, or elimination on a skirmish map, ends the
 * level through the engine; the native WINGAMEE debrief or MULTIWNE result
 * opens over it, and its buttons continue the campaign or leave. */

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static RtsGameModel *model;
static SDL_Surface *surface;
static app_t app = {.win = {640, 480}, .running = true};

static void screenshot(const char *name) {
    V_BeginFrame(0xff000000u);
    M_Drawer(&app);
    V_ReadPixels(surface->pixels, surface->pitch);
    SDL_SaveBMP(surface, name);
}

/* Let the screen's entrance gadgets play out and uncover the buttons. */
static void settle(void) {
    for (int i = 0; i < 70; ++i) { SDL_Delay(17); M_Ticker(); }
}

static void press(int id) {
    menuitem_t *item = &currentmenu->items[id];
    item->routine(currentmenu, item, MA_ACTIVATE);
}

static bool load(const char *map) {
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = map};
    menuleave = false;
    menumap = NULL;
    M_ClearMenus();
    return rts_game_model_load(model, &config);
}

/* Remove every unit of a team (or owner), then let the model run. */
static void wipe(int team, bool by_owner) {
    mobjlist_t all = P_ListMobjs();
    for (int i = 0; i < all.count; ++i) {
        mobj_t *unit = all.items[i];
        if ((by_owner ? unit->owner : unit->team) == team && unit->type_id != MT_VENT)
            P_RemoveMobj(unit);
    }
    P_FreeMobjList(&all);
    for (int tic = 0; tic < 10 && !menuactive; ++tic) rts_game_model_tick(model, RTS_TICK_MS);
}

static bool starts_with(const char *text, const char *prefix) {
    return !strncmp(text, prefix, strlen(prefix));
}

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
    surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    V_AllocScreen(640, 480);
    model = rts_game_model_create();
    CHECK(model);
    CHECK(M_Init(&app, "data/DCOLONY"));

    /* HUMAN03 block 1: team 2's buildings gone, bail 0 1. */
    CHECK(load("SCENARIO/HUMAN/HUMAN03.MAP"));
    for (int tic = 0; tic < 30; ++tic) rts_game_model_tick(model, RTS_TICK_MS);
    CHECK(!menuactive && G_LevelOutcome() == OUTCOME_NONE);
    wipe(2, false);
    CHECK(G_LevelOutcome() == OUTCOME_VICTORY && menuactive && menuinlevel);
    CHECK(!strcmp(currentmenu->items[8].text, "Victory"));
    /* Campaign sides are named after their GAMESTAT faction, as is the
     * destroyed one; owner 1 stood for team 2 and owner 2 for team 7. */
    CHECK(starts_with(currentmenu->items[0].text, "Pan Luma Money:"));
    CHECK(starts_with(currentmenu->items[1].text, "Taar Money:"));
    CHECK(starts_with(currentmenu->items[2].text, "Aerogen Money:"));
    CHECK(strstr(currentmenu->items[0].text, "Kills:") && strstr(currentmenu->items[0].text, "Losses:"));
    CHECK(!strcmp(currentmenu->items[10].text, "NEXT MISSION") && !strcmp(currentmenu->items[9].text, "MENU"));
    CHECK(!currentmenu->items[12].visible && !currentmenu->items[36].visible);
    CHECK(!currentmenu->items[33].visible && currentmenu->items[34].visible); /* human commander */
    settle();
    screenshot("/private/tmp/dc-debrief-victory.bmp");
    rts_game_model_tick(model, RTS_TICK_MS);
    CHECK(G_LevelOutcome() == OUTCOME_VICTORY); /* shown once */
    press(10); /* NEXT MISSION: the next entry's briefing */
    CHECK(menuactive && !strcmp(currentmenu->items[6].text, "ISLAND ASSAULT"));
    press(2); /* To Battle */
    CHECK(!menuactive && menumap && !strcmp(menumap, "SCENARIO/HUMAN/HUMAN04.MAP"));

    /* HUMAN03 block 2: the player's buildings gone, bail 1 2. A defeat
     * replays the same mission; MENU leaves the level. */
    CHECK(load("SCENARIO/HUMAN/HUMAN03.MAP"));
    wipe(0, false);
    CHECK(G_LevelOutcome() == OUTCOME_DEFEAT && menuactive);
    CHECK(!strcmp(currentmenu->items[8].text, "Defeat"));
    screenshot("/private/tmp/dc-debrief-defeat.bmp");
    press(10);
    CHECK(menuactive && !strcmp(currentmenu->items[6].text, "ALLIED INTERVENTION"));
    press(2);
    CHECK(menumap && !strcmp(menumap, "SCENARIO/HUMAN/HUMAN03.MAP"));
    CHECK(load("SCENARIO/HUMAN/HUMAN03.MAP"));
    wipe(0, false);
    press(9);
    CHECK(menuleave && !menuactive && !menumap);

    /* DC.EXE has no screen after the last mission: the campaign ends at the
     * main menu. */
    CHECK(load("SCENARIO/HUMAN/HUMAN15.MAP"));
    G_EndLevel(OUTCOME_VICTORY);
    CHECK(menuactive && !strcmp(currentmenu->items[8].text, "Victory"));
    press(10);
    CHECK(menuleave && !menuactive && !menumap);

    /* A skirmish has no bail actions: elimination decides it and MULTIWNE
     * lists each side. */
    dc_skirmish_t setup = {.quantity = 4, .flow = 4};
    for (int i = 0; i < 8; ++i)
        setup.players[i] = (dc_skirmish_player_t){.type = DC_PLAYER_NONE, .team = i};
    setup.players[0] = (dc_skirmish_player_t){.type = DC_PLAYER_HUMAN, .name = "Igor", .team = 0};
    setup.players[1] = (dc_skirmish_player_t){.type = DC_PLAYER_AI, .race = 1, .color = 1, .team = 1};
    for (int round = 0; round < 2; ++round) {
        DC_RequestSkirmish("SCENARIO/MPLAYER/D2PLAY01.MAP", &setup);
        CHECK(load("SCENARIO/MPLAYER/D2PLAY01.MAP") && DC_LevelSkirmish(&level));
        for (int tic = 0; tic < 30; ++tic) rts_game_model_tick(model, RTS_TICK_MS);
        CHECK(!menuactive);
        wipe(round ? 0 : 1, true);
        CHECK(G_LevelOutcome() == (round ? OUTCOME_DEFEAT : OUTCOME_VICTORY) && menuactive);
        CHECK(!strcmp(currentmenu->items[8].text, round ? "Defeat" : "Victory"));
        CHECK(starts_with(currentmenu->items[0].text, "Igor Money:"));
        CHECK(starts_with(currentmenu->items[1].text, "Player1 Money:"));
        CHECK(!strcmp(currentmenu->items[9].text, "MENU"));
        screenshot(round ? "/private/tmp/dc-result-defeat.bmp" : "/private/tmp/dc-result-victory.bmp");
        press(9);
        CHECK(menuleave && !menuactive);
    }

    rts_game_model_destroy(model);
    M_Shutdown();
    V_FreeScreen();
    SDL_FreeSurface(surface);
    SDL_Quit();
    puts("PASS: campaign debrief victory/defeat, next mission, campaign end, skirmish results");
    return 0;
}
