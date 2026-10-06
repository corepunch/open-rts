#include "t_local.h"
#include "warcraft-2.h"
#include "w2_local.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d\n", __LINE__); return rts_fail("Warcraft II menus", #c); } } while (0)

static app_t app = {.win = {640, 480}, .running = true};

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

static void draw(const char *shot) {
    V_BeginFrame(0xff000000u);
    M_Drawer(&app);
    const char *dir = getenv("W2_MENU_SHOTS");
    if (dir && shot) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.bmp", dir, shot);
        save_bmp(path);
    }
}

static menuitem_t *find(const char *text) {
    for (int i = 0; currentmenu && i < currentmenu->numitems; ++i) {
        menuitem_t *item = &currentmenu->items[i];
        if (item->visible && !strcmp(item->text, text)) return item;
    }
    return NULL;
}

static bool click_at(int x, int y) {
    SDL_Event event = {.type = SDL_MOUSEBUTTONDOWN};
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    M_Responder(&app, &event, false);
    event.type = SDL_MOUSEBUTTONUP;
    M_Responder(&app, &event, false);
    return true;
}

static bool click(const char *text) {
    menuitem_t *item = find(text);
    if (!item || !item->enabled) return false;
    irect_t r = M_MenuItemRect(currentmenu, item);
    return click_at(r.x + r.w / 2, r.y + r.h / 2);
}

static void press(SDL_Keycode key) {
    SDL_Event event = {.type = SDL_KEYDOWN};
    event.key.keysym.sym = key;
    M_Responder(&app, &event, false);
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    G_InitGame();
    CHECK(M_Init(&app, "data/WAR2"));
    V_AllocScreen(640, 480);
    M_StartControlPanel(&app);
    CHECK(menuactive);
    draw("1-title");

    /* Title -> Single Player -> Standard Game: the setup shows the map's sides. */
    CHECK(click("Single Player Game"));
    CHECK(find("Standard Game") && find("Campaign Game") && find("Load Game"));
    draw("2-single");
    CHECK(click("Standard Game"));
    CHECK(find("Start Game") && find("Select Scenario") && find("Cancel Game"));
    CHECK(find("Person") && find("Computer"));
    draw("3-setup");

    /* The scenario picker lists the retail PUDs and returns the pick. */
    CHECK(click("Select Scenario"));
    CHECK(find("OK") && find("Cancel"));
    menuitem_t *list = NULL;
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].kind == MI_LIST) list = &currentmenu->items[i];
    CHECK(list && list->rows >= 8);
    draw("4-scenario");
    press(SDLK_DOWN);
    CHECK(list->value >= 0);
    CHECK(click("OK"));
    CHECK(find("Start Game"));

    /* Resources cycle through the Wargus presets and reach the level. */
    CHECK(click("Resources: Map Default") || click("Map Default"));
    CHECK(find("Low"));
    draw("5-setup-low");
    CHECK(click("Cancel Game"));
    CHECK(find("Standard Game"));

    /* Campaign: both races list levels that load from MAINDAT. */
    CHECK(click("Campaign Game"));
    CHECK(click("Orc Campaign"));
    list = NULL;
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].kind == MI_LIST) list = &currentmenu->items[i];
    CHECK(list && list->rows == W2_CAMPAIGN_LEVELS);
    draw("6-campaign");
    press(SDLK_ESCAPE);
    CHECK(find("Human Campaign"));
    CHECK(click("Human Campaign"));
    press(SDLK_ESCAPE);
    press(SDLK_ESCAPE);
    CHECK(find("Standard Game"));

    CHECK(click("Load Game"));
    press(SDLK_ESCAPE);
    CHECK(find("Single Player Game"));

    CHECK(click("Show Credits"));
    draw("7-credits");
    press(SDLK_ESCAPE);
    CHECK(find("Single Player Game"));

    /* Starting a scenario hands its path to the driver and closes the menu. */
    CHECK(click("Single Player Game"));
    CHECK(click("Standard Game"));
    CHECK(click("Start Game"));
    CHECK(!menuactive && menumap && strstr(menumap, "PUD"));
    menumap = NULL;

    /* A campaign level is extracted from MAINDAT and launched by absolute path. */
    M_StartControlPanel(&app);
    CHECK(click("Single Player Game"));
    CHECK(click("Campaign Game"));
    CHECK(click("Orc Campaign"));
    CHECK(click("OK"));
    CHECK(!menuactive && menumap && menumap[0] == '/');
    w2_pud_info_t info;
    CHECK(w2_pud_info(menumap, &info) && info.width > 0);
    menumap = NULL;

    /* A chosen stock replaces the map's for one load of the playing sides. */
    P_InitThinkers();
    W2_SetStartResources(2);
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level));
    const w2_pud_t *pud = level.native_data;
    for (int i = 0; i < 8; ++i)
        if (pud->owners[i] == 5) CHECK(level.player_resources[i][0] == 5000 && level.player_resources[i][2] == 2000);
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level)); /* the next load is the map's own */
    pud = level.native_data;
    for (int i = 0; i < 8; ++i)
        if (pud->owners[i] == 5) CHECK(level.player_resources[i][0] != 5000);
    P_FreeLevel(&level);

    /* In a level the game menu's Options opens the sound and speed box and returns to it. */
    setenv("OPEN_RTS_USER_DIR", "build", 1);
    P_InitThinkers();
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level));
    gamesettings.sound = 10;
    M_StartControlPanel(&app);
    CHECK(find("Return to Game") && click("Options"));
    CHECK(find("Game Options") && find("Lower") && find("Raise"));
    CHECK(click("Lower"));
    CHECK(gamesettings.sound == 9 && snd_volume == 90);
    int speed = game_speed;
    CHECK(click("Game Speed: Normal") || click("Game Speed: Fast") || click("Game Speed: Slow"));
    CHECK(game_speed != speed);
    press(SDLK_ESCAPE);
    CHECK(find("Return to Game"));
    P_FreeLevel(&level);
    M_Shutdown();
    puts("PASS: Warcraft II title, single player, setup, scenario and campaign pickers, credits");
    return 0;
}
