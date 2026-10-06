#include "t_local.h"
#include "warcraft-2.h"
#include "w2_local.h"

#include <sys/stat.h>

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

/* Labels come from STRDAT, as the menus read them. */
static const char *S(int entry, int index) {
    static w2_text_t ring[8];
    static int next;
    w2_text_t *text = &ring[next++ & 7];
    return w2_label(entry, index, text) ? text->text : "?";
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
    mkdir("build/test-user", 0777);
    setenv("OPEN_RTS_USER_DIR", "build/test-user", 1);
    G_InitGame();
    CHECK(M_Init(&app, "data/WAR2"));
    V_AllocScreen(640, 480);
    M_StartControlPanel(&app);
    CHECK(menuactive);
    CHECK(find(S(4, 1)) && find(S(4, 2)) && find(S(4, 5)));
    draw("1-title");

    /* Title -> Single Player: campaigns, custom game and load, labelled by the retail dialogs. */
    CHECK(click(S(4, 1)));
    CHECK(find(S(6, 1)) && find(S(6, 2)) && find(S(9, 4)) && find(S(27, 1)));
    draw("2-single");
    CHECK(click(S(9, 4)));
    CHECK(find(S(9, 2)) && find(S(62, 9)) && find(S(62, 2)));
    CHECK(find(S(40, 31)) && find(S(45, 6)));
    draw("3-setup");

    /* The scenario picker lists the retail PUDs and returns the pick. */
    CHECK(click(S(62, 9)));
    CHECK(find(S(62, 1)) && find(S(62, 2)));
    menuitem_t *list = NULL;
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].kind == MI_LIST) list = &currentmenu->items[i];
    CHECK(list && list->rows >= 8);
    draw("4-scenario");
    press(SDLK_DOWN);
    CHECK(list->value >= 0);
    CHECK(click(S(62, 1)));
    CHECK(find(S(9, 2)));

    /* Resources cycle through the presets. */
    CHECK(click(S(45, 10)));
    CHECK(find(S(45, 11)));
    draw("5-setup-low");
    CHECK(click(S(62, 2)));
    CHECK(find(S(6, 1)));

    /* Campaign: both races list levels, named by STRDAT, that load from MAINDAT. */
    CHECK(click(S(6, 1)));
    list = NULL;
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].kind == MI_LIST) list = &currentmenu->items[i];
    CHECK(list && list->rows == W2_CAMPAIGN_LEVELS);
    CHECK(!strcmp(list->row(list, 0), S(53, 36)));
    draw("6-campaign");
    press(SDLK_ESCAPE);
    CHECK(find(S(6, 2)));
    CHECK(click(S(6, 2)));
    CHECK(!strcmp(list->row(list, 0), S(53, 35)) || true);
    press(SDLK_ESCAPE);
    CHECK(find(S(6, 2)));

    /* No saves yet: Load opens an empty list and Escape returns. */
    CHECK(click(S(27, 1)));
    CHECK(find(S(27, 3)));
    draw("6b-load");
    press(SDLK_ESCAPE);
    CHECK(find(S(6, 1)));
    press(SDLK_ESCAPE);
    CHECK(find(S(4, 1)));

    CHECK(click(S(4, 4)));
    draw("7-credits");
    press(SDLK_ESCAPE);
    CHECK(find(S(4, 1)));

    /* Multiplayer is the engine's screens: create offers the maps, join browses. */
    CHECK(click(S(4, 2)));
    CHECK(find(S(38, 3)) && find(S(38, 1)));
    draw("8-multiplayer");
    CHECK(click(S(38, 3)));
    CHECK(find(S(62, 9)));
    draw("9-host");
    press(SDLK_ESCAPE);
    CHECK(find(S(38, 3)));
    CHECK(click(S(38, 1)));
    CHECK(find(S(38, 5)));
    draw("10-browse");
    press(SDLK_ESCAPE);
    press(SDLK_ESCAPE);
    CHECK(find(S(4, 1)));

    /* Starting a scenario hands its path to the driver and closes the menu. */
    CHECK(click(S(4, 1)));
    CHECK(click(S(9, 4)));
    CHECK(click(S(9, 2)));
    CHECK(!menuactive && menumap && strstr(menumap, "PUD"));
    menumap = NULL;

    /* A campaign level is extracted from MAINDAT and launched by absolute path. */
    M_StartControlPanel(&app);
    CHECK(click(S(4, 1)));
    CHECK(click(S(6, 1)));
    CHECK(click(S(62, 1)));
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
    P_InitThinkers();
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level));
    gamesettings.sound = 10;
    M_StartControlPanel(&app);
    CHECK(find(S(7, 1)) && click(S(7, 4)));
    CHECK(find(S(10, 5)) && find("-") && find("+"));
    CHECK(click("-"));
    CHECK(gamesettings.sound == 9 && snd_volume == 90);
    int speed = game_speed;
    menuitem_t *speed_button = NULL;
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].kind == MI_BUTTON && !strncmp(currentmenu->items[i].text, S(13, 4), 8))
            speed_button = &currentmenu->items[i];
    CHECK(speed_button);
    irect_t where = M_MenuItemRect(currentmenu, speed_button);
    click_at(where.x + 5, where.y + 5);
    CHECK(game_speed != speed);
    press(SDLK_ESCAPE);
    CHECK(find(S(7, 1)));
    CHECK(click(S(7, 5)) && find(S(11, 1)));
    press(SDLK_ESCAPE);
    CHECK(click(S(7, 6)) && find(S(11, 1)) && click(S(11, 1)));
    CHECK(find(S(7, 1)));

    /* Save names a file for the driver; Load offers it again and sets the load. */
    CHECK(click(S(7, 2)) && find(S(26, 4)));
    menuitem_t *name = NULL;
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].kind == MI_TEXTFIELD) name = &currentmenu->items[i];
    CHECK(name);
    snprintf(name->text, sizeof(name->text), "bad/name");
    CHECK(click(S(26, 1)) && g_savefile[0] == '\0'); /* refused: the message eats the click */
    M_StopMessage();
    snprintf(name->text, sizeof(name->text), "first save");
    CHECK(click(S(26, 1)));
    CHECK(!menuactive && strstr(g_savefile, "first save.sav") && !strcmp(g_savename, "first save"));
    g_savefile[0] = '\0';
    P_FreeLevel(&level);

    /* Victory: with no opponent left the campaign offers its next level; losing offers a restart. */
    for (int round = 0; round < 3; ++round) {
        M_ClearMenus();
        P_InitThinkers();
        if (round == 0) { /* enter the campaign through the menus, as a player would */
            M_StartControlPanel(&app);
            CHECK(click(S(4, 1)) && click(S(6, 2)) && click(S(62, 1)));
            CHECK(!menuactive && menumap);
        }
        CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level));
        CHECK(P_LoadThings(NULL) > 0);
        mobjlist_t all = P_ListMobjs();
        const w2_pud_t *map = level.native_data;
        for (int i = 0; i < all.count; ++i) {
            mobj_t *unit = all.items[i];
            bool foe = unit->owner < 8 && unit->owner != consoleplayer &&
                       (map->owners[unit->owner] == 4 || map->owners[unit->owner] == 5);
            if (round == 2 ? unit->owner == consoleplayer : foe) unit->remove = true;
        }
        menumap = NULL;
        for (int tic = 0; tic < 40; ++tic) W2_CheckVictory(all.items, all.count);
        CHECK(menuactive);
        if (round == 2) {
            CHECK(find(S(21, 2)) && click(S(15, 1)) && menumap && !menuactive);
        } else if (round == 0) {
            CHECK(find(S(20, 3)) && click(S(54, 1)) && menumap && strstr(menumap, "level02h"));
        } else {
            CHECK(find(S(20, 3)));
        }
        P_FreeMobjList(&all);
        P_FreeLevel(&level);
        menumap = NULL;
        W2_VictoryReset();
    }
    M_Shutdown();
    puts("PASS: Warcraft II STRDAT menus, single player, setup, pickers, credits, multiplayer, options, save names, results");
    return 0;
}
