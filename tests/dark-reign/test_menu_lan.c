#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "engine.h"
#include "dark-reign.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

/* Two processes drive the native multiplayer screens: the host creates a
 * LAN game on a two-player map and launches; the joiner connects by
 * Manual IP. Both menus must hand the same map and setup to the loader. */
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void click(app_t *app, int x, int y) {
    SDL_Event event = {.type = SDL_MOUSEMOTION};
    event.motion.x = x;
    event.motion.y = y;
    M_Responder(app, &event, false);
    event = (SDL_Event){.type = SDL_MOUSEBUTTONDOWN};
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    CHECK(M_Responder(app, &event, false));
    event.type = SDL_MOUSEBUTTONUP;
    M_Responder(app, &event, false);
}

/* Each process starts SDL after the fork. */
static void open_menu(app_t *app) {
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
    V_AllocScreen(640, 480);
    G_InitGame();
    CHECK(M_Init(app, "data/REIGN/dark"));
    M_StartControlPanel(app);
    CHECK(menuactive);
    click(app, 320, 150); /* MULTI PLAYER */
}

static bool run_until_launched(void) {
    uint64_t deadline = SDL_GetTicks64() + 10000;
    while (menuactive && SDL_GetTicks64() < deadline) {
        M_Ticker();
        SDL_Delay(1);
    }
    return !menuactive && menumap;
}

int main(void) {
    CHECK(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
    int ready[2];
    CHECK(pipe(ready) == 0);
    fflush(NULL);
    pid_t joiner = fork();
    CHECK(joiner >= 0);
    if (!joiner) {
        close(ready[1]);
        app_t app = {.win = {640, 480}, .running = true};
        char byte;
        CHECK(read(ready[0], &byte, 1) == 1);
        open_menu(&app);
        click(&app, 320, 356); /* MANUAL IP; the address defaults to 127.0.0.1 */
        click(&app, 561, 430); /* Join Game */
        CHECK(menuactive && !menumap);
        CHECK(run_until_launched());
        dr_skirmish_t setup;
        CHECK(!strcmp(menumap, "scenario/MULTI/2ALASKA/2ALASKA.SCN") && DR_TakeSkirmish(menumap, &setup));
        CHECK(setup.count == 2 && setup.slots[0].type == DR_SLOT_HUMAN && setup.slots[1].type == DR_SLOT_HUMAN);
        CHECK(setup.slots[0].side == DR_SIDE_IMPERIUM && setup.credits == 9000);
        I_ShutdownNetwork();
        _exit(0);
    }
    close(ready[0]);
    app_t app = {.win = {640, 480}, .running = true};
    open_menu(&app);
    click(&app, 320, 200); /* Local Area Network */
    click(&app, 448, 430); /* Create Game */
    click(&app, 535, 250); /* Select Map */
    click(&app, 200, 139); /* 2ALASKA */
    click(&app, 275, 313); /* SELECT MAP */
    click(&app, 260, 74);  /* host side: Freedom Guard */
    click(&app, 260, 74);  /* -> Imperium */
    click(&app, 540, 363); /* Credits */
    for (const char *p = "9000"; *p; ++p) {
        SDL_Event text = {.type = SDL_TEXTINPUT};
        text.text.text[0] = *p;
        M_Responder(&app, &text, false);
    }
    click(&app, 321, 426); /* LAUNCH: row 1 stays Available for the joiner */
    if (!I_NetMenuSession()) {
        /* Another process holds UDP 5029. */
        kill(joiner, SIGKILL);
        waitpid(joiner, NULL, 0);
        puts("SKIP: UDP port 5029 is in use");
        return 0;
    }
    CHECK(write(ready[1], "J", 1) == 1);
    CHECK(run_until_launched());
    dr_skirmish_t setup;
    CHECK(!strcmp(menumap, "scenario/MULTI/2ALASKA/2ALASKA.SCN") && DR_TakeSkirmish(menumap, &setup));
    fprintf(stderr, "host setup: %d slots, side %d, credits %d\n", setup.count, setup.slots[0].side, setup.credits);
    CHECK(setup.count == 2 && setup.slots[0].side == DR_SIDE_IMPERIUM && setup.credits == 9000);
    int status;
    CHECK(waitpid(joiner, &status, 0) == joiner && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    I_ShutdownNetwork();
    M_Shutdown();
    SDL_Quit();
    puts("Dark Reign LAN menus: OK");
    return 0;
}
