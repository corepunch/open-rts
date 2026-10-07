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
static const char *D(int index) { return R(2000, index); } /* MUDDAT 6001's captions */

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

static bool click_id(int id) {
    menuitem_t *item = currentmenu ? M_MenuFind(currentmenu, id) : NULL;
    if (!item || !item->visible || !item->enabled) return false;
    irect_t r = M_MenuItemRect(currentmenu, item);
    return click_at(r.x + r.w / 2, r.y + r.h / 2);
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
    /* Custom Scenario is MUDDAT 6001; its first scenario is built-in 220. */
    CHECK(click(N(3)));
    CHECK(find(D(1)) && find(D(2)) && find(D(3)));
    CHECK(M_MenuFind(currentmenu, 2)->rect.x == 400 && M_MenuFind(currentmenu, 3)->rect.y == 368);
    char line[256];
    snprintf(line, sizeof(line), "%s\n%s", S(45, 0), S(63, 22));
    CHECK(!strcmp(M_MenuFind(currentmenu, 11)->text, line));
    CHECK(M_MenuFind(currentmenu, 4)->enabled && M_MenuFind(currentmenu, 4)->rows == 4);
    CHECK(!M_MenuFind(currentmenu, 10)->enabled && M_MenuFind(currentmenu, 10)->value == 2);
    CHECK(!M_MenuFind(currentmenu, 5)->visible);
    draw("3-setup");

    /* The picker: built-in names from STRDAT 63, no Players filter in single player. */
    CHECK(click(D(2)));
    CHECK(find(S(62, 1)) && find(S(62, 2)));
    menuitem_t *list = kind(MI_LIST, 0);
    CHECK(list && list->id == 1 && list->rows == 28 && list->value == 0);
    CHECK(!strcmp(list->row(list, 0), S(63, 22)) && !strcmp(list->row(list, 27), S(63, 49)));
    CHECK(list->rect.x == 168 && list->rect.y == 188 && list->rect.w == 296 && list->rect.h == 108);
    CHECK(!M_MenuFind(currentmenu, 4)->visible && M_MenuFind(currentmenu, 2)->rows == 2);
    CHECK(!strcmp(M_MenuFind(currentmenu, 5)->text, S(63, 22)));
    CHECK(M_MenuFind(currentmenu, -2)->rect.x == 188 && M_MenuFind(currentmenu, -3)->rect.x == 332);
    draw("4-scenario");
    /* A row's bar is native frame 46 cropped to the row, not stretched. */
    w2_menu_art_t native = {0};
    CHECK(w2_load_menu_art("data/WAR2", &native));
    CHECK(native.font.glyph_size.h == 17 && native.small_font.glyph_size.h == 14);
    const spritesheet_t *widgets = &native.widgets[1];
    for (int row = 0; row < 6; ++row)
        for (int y = 0; y < 18; ++y)
            for (int x = 270; x < 296; ++x) {
                uint8_t actual = screens[0].pixels[(list->rect.y + row * 18 + y) * 640 + list->rect.x + x];
                uint8_t expected = widgets->lumps[46].indices[y * 300 + x];
                CHECK(vpalette[actual] == widgets->source_palette[expected]);
            }
    /* WAR2.EXE 0x5e83c: a 28-pixel button centres MAINDAT 282's 14-pixel
     * cell at 1 + 29/2, so its 12-row capital leaves eight rows each side. */
    menuitem_t centered = {.kind = MI_BUTTON, .visible = true, .enabled = true, .rect = {0, 0, 106, 28},
                            .flags = W2_ITEM_FLAGS(2, 0x0218), .text = "M"};
    menu_t fixture = {.items = &centered, .numitems = 1, .itemOn = -1, .drawitem = w2_draw_item};
    V_BeginFrame(0xff0101ffu);
    uint8_t blank = screens[0].pixels[0];
    M_MenuDrawer(&fixture);
    int first = 28, last = -1, left = 106, right = -1;
    for (int y = 0; y < 28; ++y)
        for (int x = 0; x < 106; ++x)
            if (screens[0].pixels[y * 640 + x] != blank) {
                if (y < first) first = y;
                if (y > last) last = y;
                if (x < left) left = x;
                if (x > right) right = x;
            }
    CHECK(first == 8 && last == 19);
    CHECK(left == 49 && right == 58); /* 1 + 107/2 - (10 + 1 + 1)/2, plus the glyph's offset 1 */
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
    /* 128 x 128 keeps only the large built-in maps; the info lines follow the pick. */
    menuitem_t *size_choice = M_MenuFind(currentmenu, 3);
    irect_t choice_rect = M_MenuItemRect(currentmenu, size_choice);
    click_at(choice_rect.x + 5, choice_rect.y + 5);
    CHECK(currentmenu->dropdown == size_choice);
    draw("4a-size-popup");
    press(SDLK_END);
    press(SDLK_RETURN);
    list = kind(MI_LIST, 0);
    CHECK(list->rows > 0 && list->rows < 28 && M_MenuFind(currentmenu, 3)->value == 4);
    CHECK(list->value == -1 && !M_MenuFind(currentmenu, 6)->text[0] && !M_MenuFind(currentmenu, -2)->enabled);
    currentmenu->itemOn = (int)(list - currentmenu->items);
    press(SDLK_DOWN);
    CHECK(list->value >= 0 && !strcmp(M_MenuFind(currentmenu, 6)->text, S(63, 9)));
    /* Custom scenarios are the loose PUD files only: lowercase names, no folders. */
    menuitem_t *type_choice = M_MenuFind(currentmenu, 2);
    choice_rect = M_MenuItemRect(currentmenu, type_choice);
    click_at(choice_rect.x + 5, choice_rect.y + 5);
    press(SDLK_END);
    press(SDLK_RETURN);
    list = kind(MI_LIST, 0);
    CHECK(list && list->rows == 2);
    CHECK(!strcmp(list->row(list, 0), "dragon.pud") && !strcmp(list->row(list, 1), "icebrdge.pud"));
    size_choice = M_MenuFind(currentmenu, 3);
    choice_rect = M_MenuItemRect(currentmenu, size_choice);
    click_at(choice_rect.x + 5, choice_rect.y + 5);
    press(SDLK_HOME);
    press(SDLK_RETURN);
    list = kind(MI_LIST, 0);
    CHECK(list && list->rows == 8 && list->value == -1 && !M_MenuFind(currentmenu, -2)->enabled);
    for (int i = 0; i < list->rows; ++i) CHECK(!strchr(list->row(list, i), '/'));
    draw("4c-custom");
    currentmenu->itemOn = (int)(list - currentmenu->items);
    press(SDLK_DOWN);
    CHECK(list->value >= 0 && !strcmp(list->row(list, 0), "alamo.pud"));
    press(SDLK_HOME);
    CHECK(list->value == 0);
    CHECK(M_MenuFind(currentmenu, 6)->text[0] && M_MenuFind(currentmenu, 7)->text[0]);
    CHECK(click(S(62, 1)));
    snprintf(line, sizeof(line), "%s\n%s", S(45, 1), "alamo.pud");
    CHECK(!strcmp(M_MenuFind(currentmenu, 11)->text, line));
    /* Back to the built-in list: choose the second map. */
    CHECK(click(D(2)));
    type_choice = M_MenuFind(currentmenu, 2);
    choice_rect = M_MenuItemRect(currentmenu, type_choice);
    click_at(choice_rect.x + 5, choice_rect.y + 5);
    press(SDLK_HOME);
    press(SDLK_RETURN);
    list = kind(MI_LIST, 0);
    CHECK(list && list->rows == 28);
    draw("4b-built-in");
    currentmenu->itemOn = (int)(list - currentmenu->items);
    press(SDLK_HOME);
    press(SDLK_DOWN);
    CHECK(click(S(62, 1)));
    snprintf(line, sizeof(line), "%s\n%s", S(45, 0), S(63, 23));
    CHECK(!strcmp(M_MenuFind(currentmenu, 11)->text, line));

    /* Resources are a native dropdown. */
    menuitem_t *resources = M_MenuFind(currentmenu, 4);
    choice_rect = M_MenuItemRect(currentmenu, resources);
    click_at(choice_rect.x + 5, choice_rect.y + 5);
    CHECK(currentmenu->dropdown == resources);
    press(SDLK_DOWN);
    press(SDLK_RETURN);
    CHECK(M_MenuFind(currentmenu, 4)->value == 1);
    draw("5-setup-low");
    CHECK(click(D(3)));
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

    /* Connection methods, then the game list. Create and Join are the engine pages. */
    CHECK(click(S(4, 2)));
    {
        menuitem_t *list = M_MenuFind(currentmenu, 1);
        CHECK(list && list->rows == 3);
        irect_t r = M_MenuItemRect(currentmenu, list);
        click_at(r.x + 8, r.y + list->row_height / 2);
        CHECK(list->value == 0);
        draw("8a-connect");
        CHECK(click(S(5, 1)));
        draw("8b-modem");
        CHECK(click_id(7));
        draw("8c-modem-config");
        press(SDLK_ESCAPE);
        press(SDLK_ESCAPE);
        CHECK(find(S(5, 1)));
        list = M_MenuFind(currentmenu, 1);
        CHECK(list);
        r = M_MenuItemRect(currentmenu, list);
        click_at(r.x + 8, r.y + list->row_height + list->row_height / 2);
        CHECK(list->value == 1 && click(S(5, 1)));
        draw("8d-direct");
        press(SDLK_ESCAPE);
        CHECK(find(S(5, 1)));
        list = M_MenuFind(currentmenu, 1);
        CHECK(list);
        r = M_MenuItemRect(currentmenu, list);
        click_at(r.x + 8, r.y + 2 * list->row_height + list->row_height / 2);
        CHECK(list->value == 2);
    }
    CHECK(click(S(5, 1)));
    CHECK(find(S(38, 3)) && find(S(38, 1)));
    draw("8-multiplayer");
    CHECK(click(S(38, 3)));
    CHECK(find(S(62, 9)));
    CHECK(kind(MI_LIST, 0) && kind(MI_LIST, 0)->rows == 8);
    draw("9-host");
    press(SDLK_ESCAPE);
    CHECK(find(S(38, 3)) && find(S(38, 1)));
    CHECK(click(S(38, 1)));
    CHECK(find(S(38, 5)) && !find(S(38, 3)));
    draw("10-browse");
    press(SDLK_ESCAPE);
    CHECK(find(S(38, 1)));
    press(SDLK_ESCAPE);
    press(SDLK_ESCAPE);
    CHECK(find(S(4, 1)));

    /* Starting a scenario hands its path to the driver and closes the menu. */
    CHECK(click(S(4, 1)));
    CHECK(click(N(3)));
    CHECK(click(D(1)));
    CHECK(!menuactive && menumap && strstr(menumap, "scenario-221.pud"));
    menumap = NULL;

    /* Either race begins at mission one, extracted through the ordinary PUD path. */
    for (int orc = 0; orc < 2; ++orc) {
        M_StartControlPanel(&app);
        CHECK(click(S(4, 1)) && click(N(1)));
        CHECK(click(S(6, orc ? 1 : 2)));
        CHECK(menuactive);
        draw(orc ? "6d-brief-orc" : "6c-brief-human");
        CHECK(click(S(orc ? 55 : 54, 1)));
        CHECK(!menuactive && menumap && menumap[0] == '/');
        CHECK(strstr(menumap, orc ? "level01o.pud" : "level01h.pud"));
        w2_pud_info_t info;
        CHECK(w2_pud_info(menumap, &info) && info.width == 32 && info.height == 32);
        for (int i = 0; i < 8; ++i)
            if (info.owners[i] == 5) CHECK(info.sides[i] == orc);
        P_InitThinkers();
        CHECK(G_DoLoadLevel(menumap, &level));
        CHECK(((w2_mission_t *)level.mission)->campaign.number == 1);
        CHECK(((w2_mission_t *)level.mission)->campaign.orc == (orc != 0));
        CHECK(P_LoadThings(NULL) > 0);
        mobjlist_t initial = P_ListMobjs();
        menumap = NULL;
        for (int tic = 0; tic < 40; ++tic) W2_CheckVictory(initial.items, initial.count);
        CHECK(!menuactive); /* no enemy base is not the construction objective */
        P_FreeMobjList(&initial);
        P_FreeLevel(&level);
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
    CHECK(find(S(7, 1)));
    draw("11-game-menu");
    CHECK(click(S(7, 4)));
    CHECK(find(S(10, 5)));
    draw("11a-options");
    CHECK(click_id(3));
    draw("11b-screen");
    press(SDLK_ESCAPE);
    CHECK(find(S(10, 5)) && click_id(1));
    draw("11c-sound");
    {
        menuitem_t *slider = M_MenuFind(currentmenu, 2);
        CHECK(slider && slider->kind == MI_SLIDER);
        irect_t r = M_MenuItemRect(currentmenu, slider);
        click_at(r.x + 4, r.y + r.h / 2);
    }
    CHECK(gamesettings.sound < 10 && snd_volume == gamesettings.sound * 10);
    press(SDLK_ESCAPE);
    CHECK(find(S(10, 5)));
    int speed = game_speed;
    CHECK(click_id(2));
    draw("11d-speed");
    {
        menuitem_t *slider = M_MenuFind(currentmenu, 1);
        CHECK(slider && slider->kind == MI_SLIDER);
        irect_t r = M_MenuItemRect(currentmenu, slider);
        click_at(r.x + 4, r.y + r.h / 2);
    }
    CHECK(game_speed != speed);
    press(SDLK_ESCAPE);
    press(SDLK_ESCAPE);
    CHECK(find(S(7, 1)));
    CHECK(click(S(7, 5)) && find(S(8, 1)));
    draw("11e-help");
    CHECK(click_id(1));
    draw("11f-keys");
    press(SDLK_ESCAPE);
    CHECK(find(S(8, 1)) && click_id(2));
    draw("11g-tips");
    press(SDLK_ESCAPE);
    CHECK(find(S(8, 1)));
    press(SDLK_ESCAPE);
    CHECK(find(S(7, 1)));
    CHECK(click(S(7, 6)) && find(S(52, 1)));
    draw("11h-objectives");
    press(SDLK_ESCAPE);
    CHECK(find(S(7, 1)) && click_id(6));
    draw("11i-end");
    CHECK(click_id(2));
    draw("11j-confirm");
    press(SDLK_ESCAPE);
    press(SDLK_ESCAPE);
    CHECK(find(S(7, 1)));
    CHECK(click_id(2));
    draw("11k-load");
    press(SDLK_ESCAPE);
    CHECK(find(S(7, 1)));

    /* Save names a file for the driver; Load offers it again and sets the load. */
    CHECK(click(S(7, 2)) && find(S(26, 4)));
    draw("11l-save");
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

    /* Campaign construction victory and skirmish results pass through the
     * native acknowledgement, then the result screen, before loading a map. */
    for (int round = 0; round < 4; ++round) {
        bool campaign = round < 2, orc = round == 1, defeat = round == 3;
        M_ClearMenus();
        P_InitThinkers();
        W2_SetCampaign(0, false);
        if (campaign) { /* enter the campaign through the menus, as a player would */
            M_StartControlPanel(&app);
            CHECK(click(S(4, 1)) && click(N(1)) && click(S(6, orc ? 1 : 2)));
            CHECK(menuactive && click(S(orc ? 55 : 54, 1)));
            CHECK(!menuactive && menumap);
        }
        CHECK(G_DoLoadLevel(campaign ? menumap : "data/WAR2/ALAMO.PUD", &level));
        CHECK(P_LoadThings(NULL) > 0);
        mobjlist_t all = P_ListMobjs();
        const w2_pud_t *map = level.native_data;
        for (int i = 0; !campaign && i < all.count; ++i) {
            mobj_t *unit = all.items[i];
            bool foe = unit->owner < 8 && unit->owner != consoleplayer &&
                       (map->owners[unit->owner] == 4 || map->owners[unit->owner] == 5);
            if (defeat ? unit->owner == consoleplayer : foe) unit->remove = true;
        }
        menumap = NULL;
        if (campaign) {
            for (int tic = 0; tic < 40; ++tic) W2_CheckVictory(all.items, all.count);
            CHECK(!menuactive);
            int farms = 0;
            int farm_type = orc ? MT_PIG_FARM : MT_FARM;
            for (int i = 0; i < all.count; ++i) farms += all.items[i]->owner == consoleplayer && all.items[i]->type_id == farm_type;
            for (; farms < 4; ++farms) {
                mobj_t *farm = P_SpawnMobj(fixed3_zero(), farm_type);
                CHECK(farm); farm->owner = consoleplayer;
            }
            mobj_t *barracks = P_SpawnMobj(fixed3_zero(), orc ? MT_ORC_BARRACKS : MT_HUMAN_BARRACKS);
            CHECK(barracks); barracks->owner = consoleplayer;
            barracks->w2.build_left_ms = 1;
            P_FreeMobjList(&all);
            all = P_ListMobjs();
            for (int tic = 0; tic < 40; ++tic) W2_CheckVictory(all.items, all.count);
            CHECK(!menuactive); /* unfinished buildings cannot complete the objective */
            barracks->w2.build_left_ms = 0;
        }
        for (int tic = 0; tic < 40; ++tic) W2_CheckVictory(all.items, all.count);
        CHECK(menuactive);
        if (defeat) {
            CHECK(find(S(21, 2)) && click(S(21, 1)) && !menumap && menuactive);
            CHECK(currentmenu->numitems == 31 && currentmenu->background);
            CHECK(click(S(22, 1)) && menumap && !menuactive);
            CHECK(!strcmp(menumap, "ALAMO.PUD")); /* restart the active map, not the previous campaign */
        } else if (campaign) {
            CHECK(find(S(20, 3)) && click(S(20, 1)) && !menumap && menuactive);
            CHECK(currentmenu->numitems == 31 && currentmenu->background);
            draw(orc ? "12-orc-victory" : "12-victory");
            CHECK(click(S(22, 1)) && menuactive && !menumap);
            draw(orc ? "13-brief-orc" : "13-brief-human");
            CHECK(click(S(orc ? 55 : 54, 1)) && menumap && menumap[0] == '/' &&
                  strstr(menumap, orc ? "level02o" : "level02h"));
            P_FreeMobjList(&all);
            CHECK(G_DoLoadLevel(menumap, &level) && ((w2_mission_t *)level.mission)->campaign.number == 2);
            CHECK(((w2_mission_t *)level.mission)->campaign.orc == orc);
            CHECK(P_LoadThings(NULL) > 0);
            all = P_ListMobjs();
        } else {
            CHECK(find(S(20, 3)) && click(S(20, 1)) && menuactive && !menumap);
            CHECK(click(S(22, 1)) && menuleave && !menuactive);
            menuleave = false;
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
