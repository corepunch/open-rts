#define _DEFAULT_SOURCE
#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Rustfront's front end (games/rustfront/m_menu.c), driven by keys and clicks the way a player
 * would: the campaign starts its missions, the skirmish setup turns its seats into a start plan
 * and difficulties, and the pages draw. OPEN_RTS_RF_UI_SHOTS=DIR writes every page as a BMP. */

#undef CHECK
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL rustfront front end %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static app_t app = {.win = {640, 480}, .running = true};

static void key(SDL_Keycode code) {
    SDL_Event e = {.type = SDL_KEYDOWN};
    e.key.keysym.sym = code;
    CHECK(M_Responder(&app, &e, false));
    e.type = SDL_KEYUP;
    M_Responder(&app, &e, false);
}

static void render(const char *name) {
    V_AllocScreen(app.win.w, app.win.h);
    V_BeginFrame(0xff000000u);
    M_Drawer(&app);
    const char *directory = getenv("OPEN_RTS_RF_UI_SHOTS");
    if (!directory) return;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, app.win.w, app.win.h, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    V_ReadPixels(surface->pixels, surface->pitch);
    char path[1024];
    snprintf(path, sizeof(path), "%s/rustfront-%s.bmp", directory, name);
    CHECK(SDL_SaveBMP(surface, path) == 0);
    SDL_FreeSurface(surface);
}

static menuitem_t *find(int kind, const char *text) {
    for (int i = 0; i < currentmenu->numitems; ++i) {
        menuitem_t *item = &currentmenu->items[i];
        if ((int)item->kind == kind && (!text || !strcmp(item->text, text))) return item;
    }
    return NULL;
}

static void press(menuitem_t *item, menuaction_t action) {
    CHECK(item && item->enabled && item->routine);
    item->routine(currentmenu, item, action);
}

/* An owner's headquarters in the loaded level. */
static const mobj_t *hq_of(int owner) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function == P_MobjThinker && mo->owner == owner && RF_IsRole(mo->type_id, RF_ROLE_HQ)) return mo;
    }
    return NULL;
}

/* Owners with a headquarters in the loaded level. */
static int headquarters(int owner) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function == P_MobjThinker && mo->owner == owner && RF_IsRole(mo->type_id, RF_ROLE_HQ)) n++;
    }
    return n;
}

static int test_front(void) {
    char user[] = "/tmp/rf-front-XXXXXX";
    CHECK(mkdtemp(user));
    setenv("OPEN_RTS_USER_DIR", user, 1);
    CHECK(M_Init(&app, g_game_default_root));
    M_StartControlPanel(&app);
    CHECK(menuactive && currentmenu && currentmenu->background && find(MI_BUTTON, "Campaign"));
    CHECK(currentmenu->items[0].font && currentmenu->items[0].font->glyph_index['a'] >= 0);
    render("main");

    /* Campaign: the world map of the Rustfront (m_worldmap.c), as in Dune 2000. Every mission
     * of every campaign is fought in a territory of its own; the first of each is open, the
     * rest locked until won, and a win takes the mission's territory for its faction. */
    key(SDLK_c);
    CHECK(RF_HaveWorldMap() && currentmenu->background == RF_WorldMapSheet());
    menuitem_t *world = NULL;
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].kind == MI_BUTTON && currentmenu->items[i].ownerdraw &&
            currentmenu->items[i].rect.w == RF_WORLDMAP_W) world = &currentmenu->items[i];
    CHECK(world && find(MI_BUTTON, "Start")->enabled);
    for (int f = 0; f < RF_FACTION_COUNT; ++f)
        for (int i = 0; i < RF_CAMPAIGN_MISSIONS; ++i) {
            int t = RF_MissionTerritory(f, i);
            CHECK(t >= 0 && t < RF_TerritoryCount() && RF_TerritoryName(t)[0]);
            CHECK(RF_TerritoryAt(RF_TerritorySeat(t)) == t);       /* its marker stands in it */
            for (int j = 0; j < i; ++j) CHECK(RF_MissionTerritory(f, j) != t);
        }
    int foothold = RF_MissionTerritory(RF_DIRECTORATE, 0);
    CHECK(RF_TerritoryHolder(RF_DIRECTORATE, foothold) == 0);
    render("campaign");
    /* A click on the second marker picks the second mission, still locked; on the first, back. */
    ivec2_t second = RF_TerritorySeat(RF_MissionTerritory(RF_DIRECTORATE, 1));
    currentmenu->cursor = second;
    press(world, MA_ACTIVATE);
    CHECK(find(MI_BUTTON, "Start") && !find(MI_BUTTON, "Start")->enabled);
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].kind == MI_BUTTON && currentmenu->items[i].rect.w == RF_WORLDMAP_W) world = &currentmenu->items[i];
    currentmenu->cursor = RF_TerritorySeat(foothold);
    press(world, MA_ACTIVATE);
    CHECK(find(MI_BUTTON, "Start")->enabled);
    /* Winning Foothold takes Foundry Flats for the Directorate; the Clans' map is untouched. */
    CHECK(RF_CampaignRecordWin(RF_DIRECTORATE, 0));
    CHECK(RF_TerritoryHolder(RF_DIRECTORATE, foothold) == 1 + RF_DIRECTORATE);
    CHECK(RF_TerritoryHolder(RF_CLANS, foothold) == 0);
    render("campaign-won");
    CHECK(RF_CampaignResetProgress() && RF_TerritoryHolder(RF_DIRECTORATE, foothold) == 0);
    key(SDLK_2);                                     /* the Scrap Clans */
    render("campaign-clans");
    key(SDLK_RETURN);
    CHECK(!menuactive && menumap && !strcmp(menumap, "maps/campaign/clans-1.txt"));
    /* After the mission the front end comes back to the campaign. */
    menumap = NULL;
    M_StartControlPanel(&app);
    CHECK(menuactive && find(MI_BUTTON, "Reset"));
    key(SDLK_ESCAPE);
    CHECK(find(MI_BUTTON, "Skirmish"));

    /* Skirmish: maps with starts; you at the first, the computer at normal on the rest. */
    key(SDLK_s);
    menuitem_t *list = find(MI_LIST, NULL);
    CHECK(list && list->rows >= 5);
    int four = -1;
    for (int i = 0; i < list->rows; ++i)
        if (strstr(list->row(list, i), "Crossroads")) four = i;
    CHECK(four >= 0);
    list->value = four;
    press(list, MA_CHANGE);                          /* Crossroads: four starts */
    CHECK(find(MI_BUTTON, "You") && find(MI_BUTTON, "Computer: normal"));
    /* The second start goes to a hard computer, the third to nobody, the fourth to the
     * Concord on your team. */
    menuitem_t *who[4] = {0}, *faction[4] = {0}, *team[4] = {0};
    for (int i = 0; i < currentmenu->numitems; ++i) {
        menuitem_t *it = &currentmenu->items[i];
        if (it->kind != MI_BUTTON || it->value < 0 || it->value > 3) continue;
        if (!strcmp(it->text, "You") || !strncmp(it->text, "Computer", 8) || !strcmp(it->text, "Closed")) who[it->value] = it;
    }
    CHECK(who[1] && who[2]);
    press(who[1], MA_ACTIVATE);                      /* normal -> hard */
    for (int k = 0; k < 2; ++k) {                    /* normal -> hard -> closed */
        for (int i = 0; i < currentmenu->numitems; ++i) {
            menuitem_t *it = &currentmenu->items[i];
            if (it->kind == MI_BUTTON && it->value == 2 && (!strncmp(it->text, "Computer", 8))) { press(it, MA_ACTIVATE); break; }
        }
    }
    for (int i = 0; i < currentmenu->numitems; ++i) {
        menuitem_t *it = &currentmenu->items[i];
        if (it->kind != MI_BUTTON || it->value != 3) continue;
        if (!strcmp(it->text, "Directorate")) faction[3] = it;  /* start 3 defaults to Directorate */
        if (!strcmp(it->text, "-")) team[3] = it;
    }
    CHECK(faction[3] && team[3]);
    press(faction[3], MA_SECONDARY);                 /* Directorate -> Random -> Lumen Concord */
    for (int i = 0; i < currentmenu->numitems; ++i) {
        menuitem_t *it = &currentmenu->items[i];
        if (it->kind == MI_BUTTON && it->value == 3 && !strcmp(it->text, "Random")) { press(it, MA_SECONDARY); break; }
    }
    for (int i = 0; i < currentmenu->numitems; ++i) {
        menuitem_t *it = &currentmenu->items[i];
        if (it->kind == MI_BUTTON && it->value == 3 && !strcmp(it->text, "-")) { press(it, MA_ACTIVATE); break; }
    }
    for (int i = 0; i < currentmenu->numitems; ++i) {
        menuitem_t *it = &currentmenu->items[i];
        if (it->kind == MI_BUTTON && it->value == 0 && !strcmp(it->text, "-")) { press(it, MA_ACTIVATE); break; }
    }
    CHECK(find(MI_BUTTON, "Closed") && find(MI_BUTTON, "Computer: hard") && find(MI_BUTTON, "Lumen Concord"));
    render("skirmish");
    key(SDLK_RETURN);
    CHECK(!menuactive && menumap && strstr(menumap, "crossroads.txt"));
    CHECK(RF_AIDifficulty(1) == RF_AI_HARD && RF_AIDifficulty(2) == RF_AI_NORMAL);

    /* The plan the setup made: you, a hard computer and a Concord ally, three bases. */
    P_FreeLevel(&level);
    P_InitThinkers();
    char map[1200];
    M_PathJoin(map, sizeof(map), g_game_default_root, menumap); /* the driver's data root */
    CHECK(G_DoLoadLevel(map, &level) && P_InitSight());
    spritesheet_t sprite = {0};
    tileset_t tiles = {0};
    CHECK(W_LoadAssets(g_game_default_root, &level, NULL, &tiles, &sprite));
    CHECK(P_LoadThings(map) > 0);
    CHECK(headquarters(0) == 1 && headquarters(1) == 1 && headquarters(2) == 1 && headquarters(3) == 0);
    CHECK(RF_FactionOfType(hq_of(2)->type_id) == RF_CONCORD);
    CHECK(P_IsAlly(hq_of(0), hq_of(2)) && !P_IsAlly(hq_of(0), hq_of(1)));
    /* A save of it comes back as it was: the starts are not spawned a second time. */
    int before = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) before += th->function == P_MobjThinker;
    char save[1200];
    snprintf(save, sizeof(save), "%s/rustfront-1.sav", user);
    AiContext ai;
    P_AiInit(&ai);
    hudtext_t hud = {0};
    /* A mined-out cell and a difficulty survive the save. */
    int ore = -1;
    for (int i = 0; i < level.width * level.height && ore < 0; ++i)
        if (RF_TerrainHoldsOre(level.cell_terrain[i])) ore = i;
    CHECK(ore >= 0);
    RF_SetCellTerrain(&level, ore % level.width, ore / level.width, RF_TerrainOfLetter('.'));
    CHECK(G_SaveGame(save, "Crossroads test", &app, &ai, &hud));
    saveinfo_t info;
    CHECK(G_SaveInfo(save, &info) && !strcmp(info.name, "Crossroads test"));
    P_FreeLevel(&level);
    P_InitThinkers();
    CHECK(G_DoLoadLevel(map, &level) && P_InitSight());
    CHECK(P_LoadThings(map) > 0);
    RF_SetAIDifficulty(1, RF_AI_EASY);
    CHECK(RF_TerrainHoldsOre(level.cell_terrain[ore]));
    CHECK(G_LoadGame(save, &app, &ai, &hud));
    CHECK(level.cell_terrain[ore] == RF_TerrainOfLetter('.') && RF_AIDifficulty(1) == RF_AI_HARD);
    int after = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) after += th->function == P_MobjThinker;
    CHECK(after == before && headquarters(0) == 1 && headquarters(1) == 1 && headquarters(2) == 1);
    R_FreeTileset(&tiles);
    R_FreeSprite(&sprite);
    RF_SetStartPlan(NULL);
    P_FreeLevel(&level);

    /* Back from the skirmish, then the other pages. */
    menumap = NULL;
    M_StartControlPanel(&app);
    CHECK(menuactive && find(MI_BUTTON, "Start"));
    key(SDLK_ESCAPE);
    key(SDLK_o);
    CHECK(find(MI_SLIDER, NULL));
    render("options");
    key(SDLK_ESCAPE);
    key(SDLK_l);
    menuitem_t *slots = find(MI_LIST, NULL);
    CHECK(slots && slots->value == 0 && strstr(slots->row(slots, 0), "Crossroads test"));
    CHECK(find(MI_BUTTON, "Load")->enabled && strstr(slots->row(slots, 1), "Empty"));
    render("load");
    key(SDLK_RETURN);                                /* loads the save: its map, relative to the root */
    CHECK(!menuactive && menumap && !strcmp(menumap, "maps/crossroads.txt") && !strcmp(g_loadfile, save));
    g_loadfile[0] = '\0';
    menumap = NULL;
    M_StartControlPanel(&app);
    key(SDLK_m);
    CHECK(menuactive);
    render("multiplayer");
    key(SDLK_ESCAPE);

    /* In a level: the pause panel. */
    CHECK(G_DoLoadLevel(map, &level));
    M_ClearMenus();
    M_StartControlPanel(&app);
    CHECK(find(MI_BUTTON, "Resume") && find(MI_BUTTON, "Save Game"));
    render("pause");
    M_ClearMenus();
    P_FreeLevel(&level);
    puts("PASS: the front end starts missions and skirmishes from its seats, and draws every page");
    return 0;
}

static menuitem_t *find_text(const char *text) {
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (strstr(currentmenu->items[i].text, text)) return &currentmenu->items[i];
    return NULL;
}

/* The debrief (Rustfront's G_ShowOutcome): a won mission names the territory it takes, counts
 * the battle, scores it, ranks it; Continue leaves the level for the campaign's map. */
static int test_debrief(void) {
    const char *map = "maps/campaign/directorate-1.txt";
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", g_game_default_root, map);
    P_FreeLevel(&level);
    P_InitThinkers();
    CHECK(G_DoLoadLevel(path, &level) && P_LoadThings(path) > 0);
    CHECK(RF_InMission(NULL, NULL));
    leveltime = 754 * RTS_TICRATE;                         /* 12:34 into the mission */
    G_MissionTicker(&level, NULL, &(int){0}, NULL, RTS_TICK_MS);
    menuleave = false;
    G_ShowOutcome(OUTCOME_VICTORY);
    CHECK(menuactive && find_text("Mission Accomplished") && find_text("Foothold"));
    CHECK(find_text("Foundry Flats is taken") && find_text("12:34"));
    CHECK(find_text("Score") && find_text("Rank") && find(MI_BUTTON, "Continue"));
    CHECK(RF_Score(consoleplayer, true) > RF_Score(consoleplayer, false));
    CHECK(!strcmp(RF_ScoreRank(0), "Recruit") && !strcmp(RF_ScoreRank(5000), "General"));
    render("debrief");
    key(SDLK_RETURN);
    CHECK(!menuactive && menuleave);
    menuleave = false;
    G_ShowOutcome(OUTCOME_DEFEAT);
    CHECK(menuactive && find_text("Mission Failed") && find_text("Foundry Flats holds out"));
    render("debrief-lost");
    key(SDLK_ESCAPE);
    CHECK(!menuactive && menuleave);
    menuleave = false;
    P_FreeLevel(&level);
    leveltime = 0;
    puts("PASS: the debrief counts, scores and ranks the battle, and leaves the level");
    return 0;
}

int main(void) {
    RTS_RUN(test_front());
    RTS_RUN(test_debrief());
    return 0;
}
