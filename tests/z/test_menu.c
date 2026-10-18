#define _DEFAULT_SOURCE
#include "t_local.h"
#include "../../games/z/z_local.h"

#include <sys/wait.h>
#include <fcntl.h>

#undef CHECK
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL z menu %d: %s (%s)\n", __LINE__, #c, SDL_GetError()); exit(1); } } while (0)

static app_t app = {.win = {640,480}, .running = true};

static void key(SDL_Keycode code) {
    SDL_Event e = {.type = SDL_KEYDOWN}; e.key.keysym.sym = code;
    CHECK(M_Responder(&app, &e, false));
    e.type = SDL_KEYUP; M_Responder(&app, &e, false);
}

static void click(int x, int y) {
    SDL_Event e = {.type = SDL_MOUSEBUTTONDOWN}; e.button.button = SDL_BUTTON_LEFT;
    e.button.x = x; e.button.y = y;
    CHECK(M_Responder(&app, &e, false));
    e.type = SDL_MOUSEBUTTONUP; M_Responder(&app, &e, false);
}

static void render(const char *name) {
    V_AllocScreen(app.win.w, app.win.h); V_BeginFrame(0xff000000u); M_Drawer(&app);
    const char *directory = getenv("OPEN_RTS_Z_UI_SHOTS");
    if (!directory) return;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, app.win.w, app.win.h, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface); V_ReadPixels(surface->pixels, surface->pitch);
    SDL_Surface *rgb = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB24, 0);
    char path[1024]; snprintf(path, sizeof(path), "%s/z-menu-%s.bmp", directory, name);
    CHECK(rgb && SDL_SaveBMP(rgb, path) == 0); SDL_FreeSurface(rgb); SDL_FreeSurface(surface);
}

static void native_pixels(void) {
    blob_t file; spritesheet_t bg = {0};
    CHECK(Z_ReadPac("data/Z/WARDATA.PAC", "MANE.LBM", &file));
    CHECK(W_DecodeLBM(&file, &bg));
    render("main");
    /* Header above every overlay: original palette and exact 2x pixels. */
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 320; ++x)
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
            CHECK(vpalette[screens[0].pixels[(y * 2 + dy + 40) * 640 + x * 2 + dx]] ==
                  bg.palette[bg.lumps[0].indices[y * 320 + x]]);
    CHECK(screens[0].pixels[0] == V_NearestIndex(0xff000000u));
    R_FreeSprite(&bg); W_FreeFile(&file);
}

static void front_end(void) {
    CHECK(M_Init(&app, "data/Z")); M_StartControlPanel(&app);
    CHECK(menuactive && currentmenu->fit && currentmenu->numitems > 6);
    const bitmapfont_t *font = currentmenu->items[0].font;
    int space = font->glyph_index[' '];
    CHECK(space >= 0 && space < font->sprite.numlumps && font->glyph_width[' '] == 8);
    CHECK(!font->sprite.lumps[space].indices);
    native_pixels();
    /* Retail New Game hitbox (35,58)..(100,69), in the centred 2x screen. */
    click(90, 168);
    CHECK(!menuactive && menumap && !strcmp(menumap, g_game_default_map));
    menumap = NULL; M_StartControlPanel(&app);
    key(SDLK_r); CHECK(menuactive); render("replay");
    menuitem_t *list = NULL;
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].kind == MI_LIST) list = &currentmenu->items[i];
    CHECK(list && list->rows == 20 && !strcmp(list->row(list, 0), "Virgin Soldiers"));
    list->value = 19; key(SDLK_RETURN);
    CHECK(!menuactive && !strcmp(menumap, "LEVEL20/LEVEL20.MAP"));
    menumap = NULL; M_StartControlPanel(&app);
    key(SDLK_l); render("load"); key(SDLK_ESCAPE); CHECK(menuactive);
    key(SDLK_h); render("scores"); key(SDLK_ESCAPE);
    key(SDLK_m); render("multiplayer");
    CHECK(menuactive && currentmenu->background->source_palette[10] != 0);
    key(SDLK_p); key(SDLK_p); render("four-players"); key(SDLK_p);
    key(SDLK_ESCAPE); CHECK(menuactive);
    app.win = (isize2_t){1280,960}; render("large");
    click(180,336); CHECK(!menuactive && menumap);
    menumap = NULL; app.win = (isize2_t){640,480};
    level.width = 1; M_StartControlPanel(&app);
    CHECK(!strcmp(currentmenu->items[currentmenu->itemOn].text, "RESUME GAME"));
    key(SDLK_RETURN); CHECK(!menuactive && !menumap);
    level.width = 0; M_StartControlPanel(&app);
}

static void host_game(int count, const char *expected) {
    key(SDLK_m);
    for (int attempts = 0; attempts < 3; ++attempts) {
        currentmenu->refresh(currentmenu);
        bool matched = false;
        for (int i = 0; i < currentmenu->numitems; ++i)
            if (currentmenu->items[i].kind == MI_STATIC &&
                atoi(currentmenu->items[i].text) == count) matched = true;
        if (matched) break;
        key(SDLK_p);
    }
    key(SDLK_c);
    CHECK(M_NetHosting() && M_NetInLobby());
    pid_t children[3];
    for (int p = 1; p < count; ++p) {
        children[p - 1] = fork(); CHECK(children[p - 1] >= 0);
        if (!children[p - 1]) {
            /* Drop the inherited host descriptors without publishing a cancel. */
            I_ShutdownNetwork();
            CHECK(I_JoinNetGame("z", "127.0.0.1"));
            char path[512]; int result = 0;
            uint64_t deadline = SDL_GetTicks64() + 4000;
            while (!result && SDL_GetTicks64() < deadline) { result = I_PollNetGame(path, sizeof(path)); SDL_Delay(1); }
            CHECK(result == 1 && !strcmp(path, expected));
            I_ShutdownNetwork(); _exit(0);
        }
    }
    uint64_t deadline = SDL_GetTicks64() + 4000;
    while (I_NetPlayerCount() < count && SDL_GetTicks64() < deadline) { M_Ticker(); SDL_Delay(1); }
    CHECK(I_NetPlayerCount() == count); render("host-lobby");
    key(SDLK_c);
    while (menuactive && SDL_GetTicks64() < deadline) { M_Ticker(); SDL_Delay(1); }
    CHECK(!menuactive && menumap && !strcmp(menumap, expected));
    for (int p = 1; p < count; ++p) {
        int status; pid_t result;
        do {
            char path[512]; CHECK(I_PollNetGame(path, sizeof(path)) >= 0);
            result = waitpid(children[p - 1], &status, WNOHANG); SDL_Delay(1);
        } while (!result && SDL_GetTicks64() < deadline);
        CHECK(result == children[p - 1]);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    menumap = NULL; M_NetStop(); D_QuitNetGame(); M_StartControlPanel(&app);
}

static void browser(void) {
    key(SDLK_m); key(SDLK_j);
    bool dropdown = false;
    for (int i = 0; i < currentmenu->numitems; ++i) dropdown |= currentmenu->items[i].kind == MI_DROPDOWN;
    CHECK(menuactive && dropdown); render("browser");
    key(SDLK_ESCAPE); CHECK(menuactive && !I_NetMenuSession());
    key(SDLK_ESCAPE); CHECK(menuactive);
}

static void saved_game(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z"};
    CHECK(model && rts_game_model_load(model, &config));
    AiContext ai = {0}; hudtext_t hud = {0};
    char path[1200]; snprintf(path, sizeof(path), "%s/menu.sav", D_UserDirectory());
    CHECK(G_SaveGame(path, "Menu save", &app, &ai, &hud));
    rts_game_model_destroy(model);
    key(SDLK_l); render("saved-game");
    menuitem_t *list = NULL;
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].kind == MI_LIST) list = &currentmenu->items[i];
    CHECK(list && list->rows == 1 && !strcmp(list->row(list, 0), "Menu save"));
    key(SDLK_RETURN);
    CHECK(!menuactive && !strcmp(g_loadfile, path) && menumap && menumap[0] == '/');
    CHECK(strstr(menumap, "/data/Z/LEVEL01/LEVEL01.MAP"));
    g_loadfile[0] = '\0'; menumap = NULL; unlink(path); M_StartControlPanel(&app);
}

static void join_game(void) {
    int ready[2], done[2]; CHECK(pipe(ready) == 0 && pipe(done) == 0);
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        close(ready[0]); close(done[1]);
        CHECK(fcntl(done[0], F_SETFL, O_NONBLOCK) == 0);
        I_ShutdownNetwork();
        CHECK(I_HostNetGame("z", "Menu join test", "LEVEL27/LEVEL27.MAP", 2));
        CHECK(write(ready[1], "R", 1) == 1); close(ready[1]);
        uint64_t deadline = SDL_GetTicks64() + 4000;
        while (SDL_GetTicks64() < deadline) {
            char path[512], byte;
            CHECK(I_PollNetGame(path, sizeof(path)) >= 0);
            if (I_NetPlayerCount() == 2) CHECK(I_LaunchNetGame());
            if (read(done[0], &byte, 1) == 1) { I_ShutdownNetwork(); _exit(0); }
            SDL_Delay(1);
        }
        _exit(1);
    }
    close(ready[1]); close(done[0]); char byte;
    CHECK(read(ready[0], &byte, 1) == 1); close(ready[0]);
    key(SDLK_m); key(SDLK_j); I_QueryNetGames("127.0.0.1");
    int count = 0; uint64_t deadline = SDL_GetTicks64() + 4000;
    while (!count && SDL_GetTicks64() < deadline) { I_NetGames(&count); M_Ticker(); SDL_Delay(1); }
    CHECK(count == 1); render("join-browser"); key(SDLK_c);
    CHECK(menuactive);
    while (menuactive && SDL_GetTicks64() < deadline) { M_Ticker(); SDL_Delay(1); }
    CHECK(!menuactive && menumap && !strcmp(menumap, "LEVEL27/LEVEL27.MAP"));
    CHECK(doomcom->consoleplayer == 1);
    CHECK(write(done[1], "D", 1) == 1); close(done[1]);
    int status; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    menumap = NULL; M_NetStop(); D_QuitNetGame(); M_StartControlPanel(&app);
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
    CHECK(rts_test_net_port());
    char directory[] = "/private/tmp/z-menu-test-XXXXXX";
    CHECK(mkdtemp(directory) && setenv("OPEN_RTS_USER_DIR", directory, 1) == 0);
    front_end();
    saved_game();
    host_game(2, "LEVEL26/LEVEL26.MAP");
    host_game(3, "LEVEL40/LEVEL40.MAP");
    host_game(4, "LEVEL33/LEVEL33.MAP");
    browser();
    join_game();
    key(SDLK_q); CHECK(!app.running && !menuactive);
    M_Shutdown(); V_FreeScreen(); I_ShutdownNetwork(); SDL_Quit();
    rmdir(directory);
    puts("PASS: Z retail menus, native pixels, replay, load, scaling, LAN browse and 2/3/4-player launch");
    return 0;
}
