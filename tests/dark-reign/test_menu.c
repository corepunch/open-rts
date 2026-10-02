#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "engine.h"
#include "dark-reign.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void click(app_t *app, int x, int y) {
    SDL_Event event = {.type = SDL_MOUSEMOTION};
    event.motion.x = x;
    event.motion.y = y;
    M_Responder(app, &event, level.width > 0);
    event = (SDL_Event){.type = SDL_MOUSEBUTTONDOWN};
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    CHECK(M_Responder(app, &event, level.width > 0));
    event.type = SDL_MOUSEBUTTONUP;
    M_Responder(app, &event, level.width > 0);
}

static void key(app_t *app, SDL_Keycode code) {
    SDL_Event event = {.type = SDL_KEYDOWN};
    event.key.keysym.sym = code;
    CHECK(M_Responder(app, &event, level.width > 0));
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

int main(void) {
    CHECK(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    app_t app = {.win = {640, 480}, .running = true};
    V_AllocScreen(640, 480);
    G_InitGame();
    CHECK(M_Init(&app, "data/REIGN/dark"));
    CHECK(!menuactive && !menumap);
    M_StartControlPanel(&app);
    CHECK(menuactive);
    /* Main menu: SHELLCFG.H puts the 160x30 text buttons at x 240. */
    const irect_t single = {240, 86, 160, 30};
    screenshot(&app, surface, "/private/tmp/dr-menu-main.bmp");
    uint64_t normal = region_sum(surface, single);
    SDL_Event motion = {.type = SDL_MOUSEMOTION};
    motion.motion.x = 320; motion.motion.y = 100;
    M_Responder(&app, &motion, false);
    screenshot(&app, surface, "/private/tmp/dr-menu-main-hover.bmp");
    CHECK(region_sum(surface, single) != normal); /* font12o replaces font12n */
    click(&app, 320, 100);
    screenshot(&app, surface, "/private/tmp/dr-menu-single.bmp");
    CHECK(region_sum(surface, single) != normal);
    click(&app, 320, 250); /* PLAY CUSTOM MISSION */
    screenshot(&app, surface, "/private/tmp/dr-menu-custom.bmp");
    click(&app, 80, 430); /* PREVIOUS MENU */
    click(&app, 320, 200); /* LOAD SAVED GAME */
    screenshot(&app, surface, "/private/tmp/dr-menu-load.bmp");
    key(&app, SDLK_ESCAPE);
    CHECK(menuactive && app.running);
    click(&app, 320, 355); /* CREDITS */
    for (int i = 0; i < 120; ++i) M_Ticker();
    screenshot(&app, surface, "/private/tmp/dr-menu-credits.bmp");
    key(&app, SDLK_ESCAPE);
    click(&app, 320, 100); /* SINGLE PLAYER */
    click(&app, 320, 150); /* START NEW GAME: the campaign mission map */
    screenshot(&app, surface, "/private/tmp/dr-menu-missions.bmp");
    click(&app, 380, 120); /* Imperium logo */
    screenshot(&app, surface, "/private/tmp/dr-menu-briefing-imperium.bmp");
    click(&app, 50, 430); /* BACK */
    click(&app, 255, 120); /* Freedom Guard logo */
    screenshot(&app, surface, "/private/tmp/dr-menu-briefing.bmp");
    click(&app, 330, 435); /* LAUNCH */
    CHECK(!menuactive && menumap && !strcmp(menumap, "scenario/FIXED/M01F/M01F.SCN"));
    menumap = NULL;
    M_StartControlPanel(&app);
    click(&app, 320, 100);
    click(&app, 320, 150);
    click(&app, 40, 240); /* sidebar: options */
    screenshot(&app, surface, "/private/tmp/dr-menu-options.bmp");
    click(&app, 180, 415); /* QUIT TO MAIN MENU */
    CHECK(menuactive && app.running);
    click(&app, 320, 425); /* QUIT */
    CHECK(!app.running);
    M_Shutdown();
    SDL_FreeSurface(surface);
    SDL_Quit();
    puts("Dark Reign native shell: OK");
    return 0;
}
