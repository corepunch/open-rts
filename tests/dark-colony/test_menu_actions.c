#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "dark-colony.h"
#include "info.h"
#include <assert.h>
#include <unistd.h>

static void click(app_t *app, const char *script, int id) {
    char path[1024], line[512], kind[16];
    M_PathJoin(path, sizeof(path), "data/DCOLONY/INTRFACE", script);
    FILE *file = fopen(path, "r");
    assert(file);
    irect_t rect = {0};
    while (fgets(line, sizeof(line), file)) {
        int control, description;
        irect_t value;
        if (sscanf(line, "%15s %d %d %d %d %d %d", kind, &control, &description,
                   &value.x, &value.y, &value.w, &value.h) == 7 && control == id &&
            (!strcmp(kind, "pushb") || !strcmp(kind, "list"))) { rect = value; break; }
    }
    fclose(file);
    assert(rect.w && rect.h);
    SDL_Event event = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT,
        .x = rect.x + rect.w / 2, .y = rect.y + rect.h / 2}};
    assert(M_Responder(app, &event, true));
    event.type = SDL_MOUSEBUTTONUP;
    M_Responder(app, &event, true);
}

static void text(app_t *app, const char *value) {
    SDL_Event event = {.text = {.type = SDL_TEXTINPUT}};
    snprintf(event.text.text, sizeof(event.text.text), "%s", value);
    assert(M_Responder(app, &event, true));
}

static void screenshot(app_t *app, const char *name) {
    const char *directory = getenv("OPEN_RTS_MENU_SCREENSHOT_DIR");
    if (!directory) return;
    V_AllocScreen(app->win.w, app->win.h);
    M_Drawer(app);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0,app->win.w,app->win.h,32,SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    V_ReadPixels(surface->pixels, surface->pitch);
    char path[1200]; M_PathJoin(path, sizeof(path), directory, name);
    assert(!SDL_SaveBMP(surface, path));
    SDL_FreeSurface(surface); V_FreeScreen();
}

static void tick(AiContext *ai, hudtext_t *hud) {
    P_Ticker();
    mobjlist_t objects = P_ListMobjs();
    P_AiTick(ai, &level, objects.items, objects.count, gameinfo, (int)(FIXED_DT * 1000));
    G_MissionTicker(&level, objects.items, &objects.count, hud, FIXED_DT);
    G_ProductionTicker(FIXED_DT);
    P_FreeMobjList(&objects);
}

int main(void) {
    char directory[] = "/private/tmp/open-rts-menu-XXXXXX";
    assert(mkdtemp(directory));
    SDL_setenv("OPEN_RTS_USER_DIR", directory, 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER));
    G_InitGame(); P_InitThinkers();
    const char *map = "data/DCOLONY/SCENARIO/HUMAN/HUMAN02.MAP";
    assert(G_DoLoadLevel(map, &level) && P_InitSight());
    P_LoadThings(map);
    AiContext ai;
    P_AiInit(&ai); P_AiAttachGame(&ai, G_AiInterface());
    hudtext_t hud = {0};
    app_t app = {.win = {640,480}, .cell = {32,32}, .running = true, .cam = {2,3}};
    assert(M_Init(&app, "data/DCOLONY"));
    D_SetGameSpeed(100);
    gamesettings = (gamesettings_t){10,10,2};
    DC_OpenOptions(&app);
    assert(menuactive);
    screenshot(&app, "dc-options-popup.bmp");
    click(&app, "LOPTE", 41); click(&app, "LOPTE", 42); click(&app, "LOPTE", 44);
    assert(game_speed == 100 && gamesettings.sound == 10);
    click(&app, "LOPTE", 55);
    assert(!menuactive && game_speed == 100 && gamesettings.sound == 10);
    DC_OpenOptions(&app);
    click(&app, "LOPTE", 41); click(&app, "LOPTE", 42); click(&app, "LOPTE", 44);
    click(&app, "LOPTE", 56);
    assert(!menuactive && game_speed == 110 && gamesettings.sound == 9 && gamesettings.detail == 1);
    gamesettings = (gamesettings_t){0}; D_SetGameSpeed(10);
    D_LoadSettings();
    assert(game_speed == 110 && gamesettings.sound == 9 && gamesettings.detail == 1);
    DC_OpenOptions(&app);
    for (int i = 0; i < 30; ++i) click(&app, "LOPTE", 40);
    click(&app, "LOPTE", 56);
    assert(game_speed == 10);
    DC_OpenOptions(&app);
    for (int i = 0; i < 30; ++i) click(&app, "LOPTE", 41);
    click(&app, "LOPTE", 56);
    assert(game_speed == 200);
    DC_OpenObjectives(&app);
    assert(menuactive && app.running);
    screenshot(&app, "dc-objectives-popup.bmp");
    click(&app, "LOBJE", 53); click(&app, "LOBJE", 56);
    assert(!menuactive);
    DC_OpenSave(&app);
    text(&app, "Mission checkpoint");
    screenshot(&app, "dc-save-popup.bmp");
    click(&app, "LSGE", 56);
    assert(!menuactive && dc_savefile[0]);
    for (int i = 0; i < 300; ++i) tick(&ai, &hud);
    mobj_t *actor = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){20,20},0), MT_TROOPER);
    mobj_t *target = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){22,20},0), MT_TROOPER);
    assert(actor && target);
    actor->target = target; actor->attack.target = target; actor->harvest.base = target;
    production_t *production = P_EnsureMobjProduction(actor);
    assert(production);
    *production = (production_t){.actor_id = MT_TROOPER, .queue_count = 2,
        .time_ms = 2000, .time_left_ms = 1200};
    uint32_t id = actor->id, target_id = target->id;
    P_UpdateSight();
    uint32_t before = G_Consistency();
    size_t mission_size;
    const void *mission = DC_MissionArchive(&mission_size);
    void *mission_copy = malloc(mission_size);
    assert(mission_copy); memcpy(mission_copy, mission, mission_size);
    assert(DC_SaveGame(dc_savefile, dc_savename, &app, &ai, &hud));
    dc_saveinfo_t info;
    assert(DC_SaveInfo(dc_savefile, &info) && !strcmp(info.name, dc_savename));
    for (int i = 0; i < 25; ++i) tick(&ai, &hud);
    uint32_t later = G_Consistency();
    assert(later != before);
    assert(DC_LoadGame(dc_savefile, &app, &ai, &hud));
    assert(G_Consistency() == before && app.cam.x == 2 && app.cam.y == 3);
    actor = P_MobjById(id); target = P_MobjById(target_id);
    assert(actor && target && actor->target == target && actor->attack.target == target &&
        actor->harvest.base == target && actor->production->queue_count == 2);
    mission = DC_MissionArchive(&mission_size);
    assert(!memcmp(mission_copy, mission, mission_size)); free(mission_copy);
    for (int i = 0; i < 25; ++i) tick(&ai, &hud);
    assert(G_Consistency() == later);
    M_StartControlPanel(&app); click(&app, "INTROE", 2);
    assert(menuactive);
    screenshot(&app, "dc-load-menu.bmp");
    /* First native load-list row, then native Load button. */
    SDL_Event row = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT, .x=130,.y=104}};
    assert(M_Responder(&app, &row, true));
    click(&app, "LOADGE", 5);
    assert(!menuactive && menumap && dc_loadfile[0]);
    menumap = NULL; dc_loadfile[0] = 0;
    FILE *file = fopen(dc_savefile, "r+b");
    assert(file && !fseek(file, -1, SEEK_END));
    int byte = fgetc(file);
    assert(!fseek(file, -1, SEEK_END) && fputc(byte ^ 1, file) != EOF && !fclose(file));
    before = G_Consistency();
    assert(!DC_SaveInfo(dc_savefile, &info) && !DC_LoadGame(dc_savefile, &app, &ai, &hud));
    assert(before == G_Consistency());
    unlink(dc_savefile);
    char settings[1200]; M_PathJoin(settings, sizeof(settings), directory, "settings.cfg");
    unlink(settings); rmdir(directory);
    M_Shutdown(); S_Shutdown(); P_FreeLevel(&level); SDL_Quit();
    puts("PASS: native Options limits/cancel/persistence, Objectives, Save/Load, mission/AI/production/reference round-trip and corruption rejection");
    return 0;
}
