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

/* Labels come from native resource banks, as the menus read them. */
static const char *R(int resource, int index) {
    static w2_text_t ring[8];
    static int next;
    w2_text_t *text = &ring[next++ & 7];
    return w2_resource_label(resource, index, text) ? text->text : "?";
}

static const char *S(int entry, int index) { return R(4000 + entry, index); }
static const char *N(int index) { return R(2047, index); }

static menuitem_t *find(const char *text) {
    for (int i = 0; currentmenu && i < currentmenu->numitems; ++i) {
        menuitem_t *item = &currentmenu->items[i];
        if (item->visible && !strcmp(item->text, text)) return item;
    }
    return NULL;
}

static menuitem_t *kind(menuitemkind_t type, int ordinal) {
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].visible && currentmenu->items[i].kind == type && !ordinal--)
            return &currentmenu->items[i];
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
    event.type = SDL_KEYUP;
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
    CHECK(currentmenu->numitems == 6 && M_MenuFind(currentmenu, 1)->rect.x == 208);
    draw("1-title");

    /* A button is pressed on the way down and acts on release; arrows move focus. */
    {
        menuitem_t *single = find(S(4, 1));
        irect_t r = M_MenuItemRect(currentmenu, single);
        SDL_Event event = {.type = SDL_MOUSEBUTTONDOWN};
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = r.x + r.w / 2;
        event.button.y = r.y + r.h / 2;
        M_Responder(&app, &event, false);
        CHECK(currentmenu->held == single && currentmenu->over && find(S(4, 1)) && !find(S(6, 1)));
        event.button.x = 1;
        event.button.y = 1;
        event.type = SDL_MOUSEBUTTONUP;
        M_Responder(&app, &event, false);
        CHECK(find(S(4, 1)) && !find(S(6, 1)));
        currentmenu->itemOn = -1;
        press(SDLK_DOWN);
        CHECK(currentmenu->itemOn >= 0);
    }

    /* Retail resource 6007 separates Single Player from race selection. */
    CHECK(click(S(4, 1)));
    CHECK(find(N(1)) && find(N(2)) && find(N(3)) && find(N(4)));
    CHECK(!find(S(6, 1)) && !find(S(6, 2)) && !kind(MI_LIST, 0));
    CHECK(currentmenu->numitems == 5 && M_MenuFind(currentmenu, -3)->rect.y == 348);
    draw("2-single");
    CHECK(click(N(3)));
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
    CHECK(list->id == 1 && list->rect.x == 166 && list->rect.y == 186 && list->rect.h == 112);
    CHECK(M_MenuFind(currentmenu, -2)->rect.x == 188 && M_MenuFind(currentmenu, -3)->rect.x == 332);
    draw("4-scenario");
    /* Every row's unlettered right edge must be the decoded native bar,
     * including the selected row: no generic highlight fill replaces it. */
    w2_menu_art_t native = {0};
    CHECK(w2_load_menu_art("data/WAR2", &native));
    CHECK(native.font.glyph_size.h == 17 && native.small_font.glyph_size.h == 14);
    const spritecell_t *capital = &native.font.sprite.cells[native.font.glyph_index['M']];
    const spritecell_t *descender = &native.font.sprite.cells[native.font.glyph_index['p']];
    CHECK(capital->rect.h == 17 && capital->bounds.y == 0 && capital->bounds.h == 12);
    CHECK(descender->rect.h == 17 && descender->bounds.y == 4 && descender->bounds.h == 12);
    irect_t caption = V_TextBounds(&native.font, "M M");
    CHECK(caption.y == 0 && caption.h == 12);
    caption = V_TextBounds(&native.font, "Mp");
    CHECK(caption.y == 0 && caption.h == 16);
    irect_t list_rect = M_MenuItemRect(currentmenu, list);
    const spritesheet_t *widgets = &native.widgets[1];
    CHECK(widgets->cells[46].rect.w == list_rect.w);
    for (int row = 0; row < 6; ++row)
        for (int y = 2; y < 17; ++y)
            for (int x = 250; x < 298; ++x) {
                uint8_t actual = screens[0].pixels[(list_rect.y + row * 18 + y) * 640 + list_rect.x + x];
                uint8_t expected = widgets->lumps[46].indices[y * 300 + x];
                CHECK(vpalette[actual] == widgets->source_palette[expected]);
            }
    /* The native capital has 12 occupied rows; its RLE skips the last two
     * rows of its 14-pixel record. A 28-pixel button leaves eight on each side. */
    menuitem_t centered = {.kind = MI_BUTTON, .visible = true, .rect = {0, 0, 106, 28},
                            .font = &native.font, .align = MALIGN_CENTER, .text = "M"};
    menu_t fixture = {.items = &centered, .numitems = 1, .itemOn = -1};
    V_BeginFrame(0xff0101ffu);
    uint8_t blank = screens[0].pixels[0];
    M_MenuDrawer(&fixture);
    int first = 28, last = -1;
    for (int y = 0; y < 28; ++y)
        for (int x = 0; x < 106; ++x)
            if (screens[0].pixels[y * 640 + x] != blank) {
                if (y < first) first = y;
                if (y > last) last = y;
            }
    CHECK(first == 8 && last == 19);
    w2_free_menu_art(&native);
    /* The same popup has matching draw/input geometry at UI scale two. */
    app.win = (isize2_t){1280, 960};
    V_AllocScreen(1280, 960);
    draw("4d-picker-2x");
    menuitem_t *scaled_choice = M_MenuFind(currentmenu, 3);
    irect_t scaled_rect = M_MenuItemRect(currentmenu, scaled_choice);
    click_at(scaled_rect.x + 10, scaled_rect.y + 10);
    CHECK(currentmenu->dropdown == scaled_choice);
    press(SDLK_END);
    press(SDLK_ESCAPE);
    CHECK(!currentmenu->dropdown && scaled_choice->value == 0);
    app.win = (isize2_t){640, 480};
    V_AllocScreen(640, 480);
    menuitem_t *type_choice = M_MenuFind(currentmenu, 2);
    menuitem_t *size_choice = M_MenuFind(currentmenu, 3);
    CHECK(type_choice && size_choice && M_MenuFind(currentmenu, 4));
    /* Filters are real popups; Escape leaves their committed value intact. */
    irect_t choice_rect = M_MenuItemRect(currentmenu, size_choice);
    click_at(choice_rect.x + 5, choice_rect.y + 5);
    CHECK(currentmenu->dropdown == size_choice);
    draw("4a-size-popup");
    press(SDLK_END);
    press(SDLK_RETURN);
    list = kind(MI_LIST, 0);
    /* Browse the native DATA directory: it contains no loose PUDs. */
    menuitem_t *directory = kind(MI_LIST, 0);
    int data_row = -1;
    for (int i = 0; i < directory->rows; ++i)
        if (!strcmp(directory->row(directory, i), "DATA/")) data_row = i;
    CHECK(data_row >= 0);
    currentmenu->itemOn = (int)(directory - currentmenu->items);
    press(SDLK_HOME);
    for (int i = 0; i < data_row; ++i) press(SDLK_DOWN);
    press(SDLK_RETURN);
    CHECK(kind(MI_LIST, 0)->rows == 1 && !strcmp(kind(MI_LIST, 0)->row(kind(MI_LIST, 0), 0), ".."));
    draw("4c-empty-folder");
    press(SDLK_HOME);
    press(SDLK_RETURN);
    list = kind(MI_LIST, 0);
    CHECK(list && M_MenuFind(currentmenu, 3)->value == 4);
    for (int i = 0; i < list->rows; ++i) {
        const char *name = list->row(list, i);
        if (!strchr(name, '/')) CHECK(!strcmp(name, "DRAGON") || !strcmp(name, "ICEBRDGE"));
    }
    size_choice = M_MenuFind(currentmenu, 3);
    choice_rect = M_MenuItemRect(currentmenu, size_choice);
    click_at(choice_rect.x + 5, choice_rect.y + 5);
    press(SDLK_HOME);
    press(SDLK_RETURN);
    type_choice = M_MenuFind(currentmenu, 2);
    choice_rect = M_MenuItemRect(currentmenu, type_choice);
    click_at(choice_rect.x + 5, choice_rect.y + 5);
    press(SDLK_HOME);
    press(SDLK_RETURN);
    list = kind(MI_LIST, 0);
    CHECK(list && list->rows == 28 && M_MenuFind(currentmenu, 4)->enabled);
    CHECK(M_MenuFind(currentmenu, 4)->rows == 9);
    draw("4b-built-in");
    /* Choose a native archive map, then reopen the custom picker. */
    press(SDLK_DOWN);
    CHECK(click(S(62, 1)));
    CHECK(find(S(9, 2)) && find(S(9, 2))->enabled);
    CHECK(click(S(62, 9)));
    type_choice = M_MenuFind(currentmenu, 2);
    choice_rect = M_MenuItemRect(currentmenu, type_choice);
    click_at(choice_rect.x + 5, choice_rect.y + 5);
    press(SDLK_END);
    press(SDLK_RETURN);
    list = kind(MI_LIST, 0);
    press(SDLK_DOWN);
    CHECK(list->value >= 0);
    CHECK(click(S(62, 1)));
    CHECK(find(S(9, 2)));

    /* Resources cycle through the presets. */
    CHECK(click(S(45, 10)));
    CHECK(find(S(45, 11)));
    draw("5-setup-low");
    CHECK(click(S(62, 2)));
    CHECK(find(N(1)));

    /* Race choice offers three native buttons, without a mission browser. */
    CHECK(click(N(1)));
    CHECK(find(S(6, 1)) && find(S(6, 2)) && find(S(6, 3)));
    CHECK(currentmenu->numitems == 4 && !kind(MI_LIST, 0) && !kind(MI_SCROLLBAR, 0));
    CHECK(M_MenuFind(currentmenu, 1)->rect.y == 240 && M_MenuFind(currentmenu, -3)->rect.y == 312);
    draw("6-campaign");
    press(SDLK_ESCAPE);
    CHECK(find(N(1)) && !menumap);
    CHECK(click(N(1)) && click(S(6, 3)));
    CHECK(find(N(1)) && !menumap);

    /* No saves yet: Load opens an empty list and Escape returns. */
    CHECK(click(N(2)));
    CHECK(find(S(27, 3)));
    draw("6b-load");
    press(SDLK_ESCAPE);
    CHECK(find(N(1)));
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
    CHECK(kind(MI_LIST, 0) && kind(MI_LIST, 0)->rows == 8);
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
    CHECK(click(N(3)));
    CHECK(click(S(9, 2)));
    CHECK(!menuactive && menumap && strstr(menumap, "PUD"));
    menumap = NULL;

    /* Either race begins at mission one, extracted through the ordinary PUD path. */
    for (int orc = 0; orc < 2; ++orc) {
        M_StartControlPanel(&app);
        CHECK(click(S(4, 1)) && click(N(1)));
        CHECK(click(S(6, orc ? 1 : 2)));
        CHECK(!menuactive && menumap && menumap[0] == '/');
        CHECK(strstr(menumap, orc ? "level01o.pud" : "level01h.pud"));
        w2_pud_info_t info;
        CHECK(w2_pud_info(menumap, &info) && info.width == 32 && info.height == 32);
        for (int i = 0; i < 8; ++i)
            if (info.owners[i] == 5) CHECK(info.sides[i] == orc);
        menumap = NULL;
    }

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
    CHECK(currentmenu->items[0].rect.x == 272 && currentmenu->items[0].rect.y == 96);
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
            CHECK(click(S(4, 1)) && click(N(1)) && click(S(6, 2)));
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
