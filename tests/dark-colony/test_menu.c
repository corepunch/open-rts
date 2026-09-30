#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include "game.h"
#include "m_menu.h"
#include "w_spr.h"
#include "dc_skirmish.h"
#include "dc_types.h"
#include "d_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void key(app_t *app, SDL_Keycode code, bool inlevel) {
    SDL_Event event = {.type = SDL_KEYDOWN};
    event.key.keysym.sym = code;
    CHECK(M_Responder(app, &event, inlevel));
    CHECK(app->running);
}

static void click(app_t *app, int x, int y) {
    SDL_Event event = {.type = SDL_MOUSEBUTTONDOWN};
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    CHECK(M_Responder(app, &event, false));
    event.type = SDL_MOUSEBUTTONUP;
    CHECK(M_Responder(app, &event, false) == menuactive);
    CHECK(app->running);
}

static uint64_t region_sum(const SDL_Surface *surface, irect_t rect) {
    uint64_t sum = 0;
    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)surface->pixels + y * surface->pitch);
        for (int x = rect.x; x < rect.x + rect.w; ++x) sum += (row[x] & 0x00ffffffu) * (uint64_t)(x + y * 640 + 1);
    }
    return sum;
}

static void screenshot(app_t *app, SDL_Surface *surface, const char *name) {
    V_AllocScreen(640, 480);
    V_BeginFrame(0xff000000u);
    M_Drawer(app);
    V_ReadPixels(surface->pixels, surface->pitch);
    CHECK(SDL_SaveBMP(surface, name) == 0);
}

static void join_lan_menu(app_t *app, SDL_Surface *surface) {
    int ready[2], done[2];
    CHECK(pipe(ready) == 0 && pipe(done) == 0);
    fflush(NULL);
    pid_t host = fork();
    CHECK(host >= 0);
    if (!host) {
        close(ready[0]); close(done[1]);
        CHECK(fcntl(done[0], F_SETFL, O_NONBLOCK) == 0);
        CHECK(I_HostNetGame("dark-colony", "Menu LAN test", "SCENARIO/MPLAYER/D2PLAY01.MAP", 2));
        CHECK(write(ready[1], "R", 1) == 1);
        char map[512], byte;
        uint64_t deadline = SDL_GetTicks64() + 10000;
        bool finished = false;
        while (SDL_GetTicks64() < deadline) {
            CHECK(I_PollNetGame(map, sizeof(map)) >= 0);
            if (read(done[0], &byte, 1) == 1) { finished = true; break; }
            SDL_Delay(1);
        }
        CHECK(finished && !strcmp(map, "SCENARIO/MPLAYER/D2PLAY01.MAP"));
        I_ShutdownNetwork();
        _exit(0);
    }
    close(ready[1]); close(done[0]);
    char byte;
    CHECK(read(ready[0], &byte, 1) == 1);
    click(app, 400, 325);
    click(app, 530, 420);
    I_QueryNetGames("127.0.0.1");
    int count;
    uint64_t deadline = SDL_GetTicks64() + 3000;
    do { M_Ticker(); I_NetGames(&count); SDL_Delay(1); } while (!count && SDL_GetTicks64() < deadline);
    CHECK(count > 0);
    click(app, 150, 110);
    screenshot(app, surface, "/private/tmp/dc-menu-lan-found.bmp");
    click(app, 440, 460);
    CHECK(menuactive && I_NetJoining());
    deadline = SDL_GetTicks64() + 3000;
    do { M_Ticker(); SDL_Delay(1); } while (menuactive && SDL_GetTicks64() < deadline);
    CHECK(!menuactive && menumap && !strcmp(menumap, "SCENARIO/MPLAYER/D2PLAY01.MAP"));
    CHECK(doomcom->consoleplayer == 1 && doomcom->numplayers == 2 && I_NetMenuSession());
    char map[512] = "";
    CHECK(I_StartNetGame("dark-colony", map, sizeof(map)) && !I_NetMenuSession());
    CHECK(write(done[1], "Q", 1) == 1);
    int status;
    CHECK(waitpid(host, &status, 0) == host && WIFEXITED(status) && !WEXITSTATUS(status));
    close(ready[0]); close(done[1]);
    D_QuitNetGame();
    menumap = NULL;
    M_StartControlPanel(app);
}

int main(void) {
    CHECK(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    app_t app = {.win = {640, 480}, .running = true};
    V_AllocScreen(640, 480);
    G_InitGame();
    CHECK(M_Init(&app, "data/DCOLONY"));
    CHECK(!menuactive && !menumap && !level.mission);
    M_StartControlPanel(&app);
    CHECK(menuactive);
    /* INTROE's banim plays LARGEBUTTON over each button in turn (0x425214):
     * a gadget starts when the previous one reaches its third frame, each
     * lasts 28 ticks, and the DCSS logo only starts once all eight are done. */
    const irect_t first_button = {138, 314, 179, 25}, last_button = {318, 392, 179, 25};
    const irect_t logo_rect = {130, 0, 378, 123};
    screenshot(&app, surface, "/private/tmp/dc-menu-main-entrance.bmp");
    uint64_t first_at_start = region_sum(surface, first_button);
    uint64_t last_at_start = region_sum(surface, last_button);
    for (int i = 0; i < 35; ++i) { SDL_Delay(17); M_Ticker(); }
    screenshot(&app, surface, "/private/tmp/dc-menu-main-midway.bmp");
    uint64_t first_midway = region_sum(surface, first_button);
    uint64_t last_midway = region_sum(surface, last_button);
    uint64_t logo_midway = region_sum(surface, logo_rect);
    for (int i = 0; i < 95; ++i) { SDL_Delay(17); M_Ticker(); }
    screenshot(&app, surface, "/private/tmp/dc-menu-main.bmp");
    CHECK(first_at_start != region_sum(surface, first_button));
    CHECK(first_midway == region_sum(surface, first_button));
    CHECK(last_at_start != region_sum(surface, last_button));
    CHECK(last_midway != region_sum(surface, last_button) && last_midway != last_at_start);
    CHECK(logo_midway != region_sum(surface, logo_rect));
    /* Catch the tempting world-ticker reset-to-zero rule: the native menu
     * must retain DCSS's terminal logo pose after the entrance finishes. */
    spritesheet_t logo = {0};
    uint32_t palette[256];
    CHECK(load_dark_colony_sprite("data/DCOLONY/SPRITES/DCSS.SPR", &logo, palette));
    spritesheet_t menu_background = {0};
    CHECK(W_LoadGIFTexture("data/DCOLONY/INTRFACE/INTRO.GIF", &menu_background));
    int compared = 0;
    const spritecell_t *cell = &logo.cells[28];
    for (int y = 0; y < cell->rect.h; ++y) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)surface->pixels +
            (y + cell->displacement.y) * surface->pitch);
        for (int x = 0; x < cell->rect.w; ++x) {
            uint8_t index = logo.lumps[28].indices[y * cell->rect.w + x];
            if ((logo.source_palette[index] >> 24) != 255) continue;
            CHECK(row[130 + x + cell->displacement.x] == menu_background.source_palette[index]);
            ++compared;
        }
    }
    CHECK(compared > 0);
    R_FreeSprite(&logo);
    R_FreeSprite(&menu_background);
    key(&app, SDLK_ESCAPE, false);
    CHECK(menuactive); /* No level to resume on the title screen. */
    for (int race = 0; race < 2; ++race) {
        key(&app, SDLK_RETURN, false); /* New Campaign. */
        click(&app, race ? 390 : 230, 35);
        click(&app, 220, 312);
        SDL_Event text = {.type = SDL_TEXTINPUT};
        strcpy(text.text.text, "COMMANDER");
        CHECK(M_Responder(&app, &text, false));
        screenshot(&app, surface, race ? "/private/tmp/dc-menu-gray.bmp" : "/private/tmp/dc-menu-human.bmp");
        click(&app, 470, 360); /* Start Campaign. */
        CHECK(menuactive && !menumap && !level.mission);
        screenshot(&app, surface, "/private/tmp/dc-menu-story.bmp");
        click(&app, 580, 460); /* Next: mission briefing. */
        screenshot(&app, surface, "/private/tmp/dc-menu-briefing.bmp");
        click(&app, 450, 460); /* To Battle. */
        CHECK(!menuactive && menumap);
        CHECK(!strcmp(menumap, race ? "SCENARIO/ALIEN/ALIEN01.MAP" : "SCENARIO/HUMAN/HUMAN01.MAP"));
        CHECK(!level.mission); /* Loading belongs to the driver, not menu input. */
        menumap = NULL;
        M_StartControlPanel(&app);
    }
    key(&app, SDLK_DOWN, false);
    key(&app, SDLK_RETURN, false); /* Training. */
    click(&app, 390, 35); /* Gray. */
    click(&app, 470, 360);
    click(&app, 450, 460);
    CHECK(!menuactive && menumap && !strcmp(menumap, "SCENARIO/TEST/ATRAIN1.MAP"));
    menumap = NULL;
    M_StartControlPanel(&app);
    click(&app, 400, 325); /* Multi Player War. */
    for (int i = 0; i < 70; ++i) { SDL_Delay(17); M_Ticker(); }
    screenshot(&app, surface, "/private/tmp/dc-menu-network.bmp");
    CHECK(menuactive && !netgame);
    click(&app, 530, 390); /* Act as Server. */
    screenshot(&app, surface, "/private/tmp/dc-menu-session-name.bmp");
    key(&app, SDLK_RETURN, false);
    screenshot(&app, surface, "/private/tmp/dc-menu-lan-setup.bmp");
    click(&app, 110, 65); /* Three LAN slots. */
    click(&app, 110, 84); /* Four LAN slots. */
    click(&app, 150, 205); /* Select a map with enough slots. */
    click(&app, 570, 465); /* Create and advertise. */
    CHECK(menuactive && netgame && doomcom->numplayers == 4 && !menumap);
    M_Ticker();
    screenshot(&app, surface, "/private/tmp/dc-menu-lan-wait.bmp");
    key(&app, SDLK_ESCAPE, false);
    CHECK(menuactive && !netgame && !menumap);
    click(&app, 530, 420); /* Browse LAN. */
    M_Ticker();
    screenshot(&app, surface, "/private/tmp/dc-menu-lan-browser.bmp");
    click(&app, 250, 460); /* Direct address. */
    screenshot(&app, surface, "/private/tmp/dc-menu-lan-address.bmp");
    key(&app, SDLK_RETURN, false); /* Join localhost asynchronously. */
    CHECK(menuactive && netgame && I_NetJoining() && !menumap);
    M_Ticker();
    key(&app, SDLK_ESCAPE, false);
    CHECK(menuactive && !netgame && !menumap);
    key(&app, SDLK_ESCAPE, false);
    join_lan_menu(&app, surface);
    click(&app, 400, 350); /* Single Player War. */
    for (int i = 0; i < 70; ++i) { SDL_Delay(17); M_Ticker(); }
    screenshot(&app, surface, "/private/tmp/dc-menu-skirmish.bmp");
    CHECK(menuactive && !menumap);
    for (int i = 2; i < 8; ++i) click(&app, 110, 25 + i * 19);
    SDL_Event drag = {.type = SDL_MOUSEBUTTONDOWN};
    drag.button.button = SDL_BUTTON_LEFT;
    drag.button.x = 600;
    drag.button.y = 285;
    CHECK(M_Responder(&app, &drag, false));
    drag.type = SDL_MOUSEMOTION;
    drag.motion.x = 600;
    drag.motion.y = 180; /* Capture continues outside the scrollbar. */
    CHECK(M_Responder(&app, &drag, false));
    drag.type = SDL_MOUSEBUTTONUP;
    drag.button.button = SDL_BUTTON_LEFT;
    CHECK(M_Responder(&app, &drag, false));
    /* Eight active players filter the catalog to eight-player maps. */
    click(&app, 150, 205); /* Armageddon. */
    for (int i = 0; i < 25; ++i) {
        click(&app, 610, 405);
        click(&app, 610, 423);
        click(&app, 610, 441);
    }
    click(&app, 215, 25); /* Gray local player. */
    screenshot(&app, surface, "/private/tmp/dc-menu-skirmish-eight.bmp");
    /* Native fixed-width spacing preserves the catalog's padded columns. */
    bitmapfont_t font = {0};
    CHECK(DC_LoadFont("data/DCOLONY", "INTRFACE/MFONTO5.SPR", &font));
    CHECK(HU_TextWidth(&font, "A A", 1) == 24);
    /* Translation to palette index zero must produce opaque black glyphs
     * against the cyan selection, rather than making the letters disappear. */
    const spritecell_t *glyph = &font.sprite.cells['A' - 31];
    int ink = 0;
    for (int y = 0; y < glyph->rect.h; ++y)
        for (int x = 0; x < glyph->rect.w; ++x) {
            if (!font.sprite.lumps['A' - 31].indices[y * glyph->rect.w + x]) continue;
            const uint32_t *row = (const uint32_t *)((const uint8_t *)surface->pixels +
                (200 + y + glyph->displacement.y) * surface->pitch);
            CHECK(row[29 + x + glyph->displacement.x] == 0xff000000u);
            ++ink;
        }
    CHECK(ink > 0);
    HU_FreeFont(&font);
    /* Hovering another control must not hide player-name control zero. */
    int name_pixels = 0;
    for (int y = 21; y < 34; ++y) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)surface->pixels + y * surface->pitch);
        for (int x = 247; x < 303; ++x) name_pixels += (row[x] & 0x00ffffffu) != 0;
    }
    CHECK(name_pixels > 0);
    click(&app, 570, 465);
    CHECK(!menuactive && menumap);
    CHECK(!strcmp(menumap, "SCENARIO/MPLAYER/D8PLAY01.MAP"));
    dc_skirmish_t setup;
    CHECK(DC_TakeSkirmish(menumap, &setup));
    CHECK(setup.quantity == 20 && setup.flow == 20 && setup.rank == 3);
    CHECK(setup.players[0].race == 1 && setup.players[0].type == DC_PLAYER_HUMAN);
    for (int i = 1; i < 8; ++i) CHECK(setup.players[i].type == DC_PLAYER_AI);
    CHECK(!DC_TakeSkirmish(menumap, &setup));
    menumap = NULL;
    M_StartControlPanel(&app);
    key(&app, SDLK_ESCAPE, true);
    CHECK(!menuactive);
    DC_OpenQuitDialog(&app);
    CHECK(menuactive);
    screenshot(&app, surface, "/private/tmp/dc-menu-quit.bmp");
    click(&app, 360, 284); /* LQCE 57: continue. */
    CHECK(!menuactive && app.running);
    DC_OpenQuitDialog(&app);
    key(&app, SDLK_ESCAPE, true);
    CHECK(!menuactive);
    SDL_Event repeat = {.type = SDL_KEYDOWN};
    repeat.key.keysym.sym = SDLK_ESCAPE;
    repeat.key.repeat = 1;
    CHECK(M_Responder(&app, &repeat, true));
    CHECK(!menuactive && app.running);
    app.dragging_select = true;
    key(&app, SDLK_ESCAPE, true);
    CHECK(menuactive && !app.dragging_select);
    key(&app, SDLK_ESCAPE, true);
    DC_OpenQuitDialog(&app);
    SDL_Event confirm = {.type = SDL_MOUSEBUTTONDOWN};
    confirm.button.button = SDL_BUTTON_LEFT;
    confirm.button.x = 360;
    confirm.button.y = 236;
    CHECK(M_Responder(&app, &confirm, true));
    CHECK(!menuactive && !app.running);
    app.running = true;
    M_StartControlPanel(&app);
    SDL_Event quit = {.type = SDL_QUIT};
    CHECK(M_Responder(&app, &quit, true));
    CHECK(!app.running);
    M_Shutdown();
    R_FreeSpriteBuffer();
    SDL_DestroyRenderer(app.renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    SDL_Quit();
    puts("Menu OK: native screens, campaigns, training, LAN create/browse/direct join/cancel, eight-player skirmish, resume, quit");
    return 0;
}
