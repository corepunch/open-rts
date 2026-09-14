#include "engine.h"
#include "game.h"
#include "m_menu.h"
#include "w_spr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    CHECK(app->running);
}

static void screenshot(app_t *app, SDL_Surface *surface, const char *name) {
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    M_Drawer(app);
    SDL_RenderPresent(app->renderer);
    CHECK(SDL_SaveBMP(surface, name) == 0);
}

int main(void) {
    CHECK(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    app_t app = {.win = {640, 480}, .running = true};
    r_renderer = app.renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(app.renderer);
    G_InitGame();
    CHECK(M_Init(&app, "data/DCOLONY"));
    CHECK(!menuactive && !menumap && !level.mission);
    M_StartControlPanel(&app);
    CHECK(menuactive);
    /* Let the native one-off logo/button sequences finish. */
    for (int i = 0; i < 70; ++i) { SDL_Delay(17); M_Ticker(); }
    screenshot(&app, surface, "/private/tmp/dc-menu-main.bmp");
    /* Catch the tempting world-ticker reset-to-zero rule: the native menu
     * must retain DCSS's terminal logo pose after the entrance finishes. */
    spritesheet_t logo = {0};
    uint32_t palette[256];
    CHECK(load_dark_colony_sprite("data/DCOLONY/SPRITES/DCSS.SPR", &logo, palette));
    int compared = 0;
    const spritecell_t *cell = &logo.cells[28];
    for (int y = 0; y < cell->rect.h; ++y) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)surface->pixels + y * surface->pitch);
        for (int x = 0; x < cell->rect.w; ++x) {
            uint8_t index = logo.lumps[28].indices[y * cell->rect.w + x];
            if ((logo.source_palette[index] >> 24) != 255) continue;
            CHECK(row[130 + x] == logo.source_palette[index]);
            ++compared;
        }
    }
    CHECK(compared > 0);
    R_FreeSprite(&logo);
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
    SDL_Event quit = {.type = SDL_QUIT};
    CHECK(M_Responder(&app, &quit, true));
    CHECK(!app.running);
    M_Shutdown();
    R_FreeSpriteBuffer();
    SDL_DestroyRenderer(app.renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    SDL_Quit();
    puts("Menu OK: native screens, both campaign starts, training, input capture, resume, quit");
    return 0;
}
