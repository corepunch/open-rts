#include "t_local.h"

#define CHECK(c) RTS_CHECK(c, "fallback menu", #c)

int main(void) {
#ifdef RTS_GAME_DARK_COLONY
    puts("SKIP: Dark Colony uses native menu screens");
#else
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    app_t app = {.win = {640,480}, .running = true};
    CHECK(M_Init(&app, g_game_default_root) && !menuactive && !menumap);
    M_StartControlPanel(&app);
    CHECK(menuactive);
    V_AllocScreen(640,480);
    uint32_t colors[256];
    for (int i = 0; i < 256; ++i) colors[i] = 0xff000000u | (unsigned)i * 0x010101u;
    I_SetPalette(colors);
    V_BeginFrame(0xff000000u);
    M_Drawer(&app);
    int drawn = 0;
    for (int y = 175; y < 285; ++y) for (int x = 220; x < 420; ++x)
        drawn += screens[0].pixels[y * 640 + x] > 100;
    CHECK(drawn > 0); /* Labels and focus are visible without a native font. */
    SDL_Event event = {.key = {.type = SDL_KEYDOWN, .keysym.sym = SDLK_RETURN}};
    /* A failed session leaves a usable offline menu, with no click-through. */
    netactive = true;
    snprintf(neterror, sizeof(neterror), "Game speed mismatch: yours is 100%%, player 2 uses 150%%.");
    D_NetGameError(&app);
    CHECK(app.running && menuactive && !menumap && !neterror[0]);
    CHECK(!netgame && !netactive && !netready && !consoleplayer);
    CHECK(doomcom->numplayers == 1 && doomcom->numnodes == 1 && doomcom->ticdup == 1);
    CHECK(M_Responder(&app, &event, false) && menuactive && !menumap);
    event.key.keysym.sym = SDLK_ESCAPE;
    CHECK(M_Responder(&app, &event, false) && menuactive && !menumap);
    event.key.keysym.sym = SDLK_RETURN;
    CHECK(M_Responder(&app,&event,false));
    CHECK(!menuactive && menumap && !strcmp(menumap,g_game_default_map));
    menumap = NULL;
    level.width = 1;
    event.key.keysym.sym = SDLK_ESCAPE;
    CHECK(D_MenuResponder(&app,&event,NULL,NULL,0) && menuactive);
    CHECK(!strcmp(gamemenu.items[gamemenu.itemOn].text,"RESUME GAME"));
    event.key.keysym.sym = SDLK_RETURN;
    CHECK(D_MenuResponder(&app,&event,NULL,NULL,0) && !menuactive && !menumap);
    M_StartControlPanel(&app);
    event.key.keysym.sym = SDLK_DOWN;
    CHECK(D_MenuResponder(&app,&event,NULL,NULL,0));
    event.key.keysym.sym = SDLK_RETURN;
    CHECK(D_MenuResponder(&app,&event,NULL,NULL,0) && !menuactive && !app.running);
    M_Shutdown();
    CHECK(!menuactive && !menumap);
    level.width = 0;
    V_FreeScreen();
    SDL_Quit();
    puts("PASS: static fallback menu drawing, start, resume, Escape and quit");
#endif
    return 0;
}
