#include "engine.h"

/* The front end is the game's screens and the engine's loop over them: the
 * game builds the screen Escape opens; the engine routes input to whichever
 * screen is open, ticks and draws it. */
bool menuactive;
bool menuerror;
bool menuleave;
bool menuinlevel;
const char *menumap;
menu_t *currentmenu;

void M_SetupNextMenu(menu_t *menu) {
    currentmenu = menu;
    menuactive = menu != NULL;
}

void M_ClearMenus(void) {
    menuactive = false;
    if (currentmenu) currentmenu->held = NULL;
    SDL_StopTextInput();
}

bool M_Init(app_t *app, const char *root) {
    menuactive = menuerror = false;
    M_StopMessage();
    menumap = NULL;
    currentmenu = NULL;
    return G_InitMenus(app, root);
}

void M_StartControlPanel(app_t *app) {
    if (menuactive) return;
    menuinlevel = level.width > 0;
    menu_t *menu = G_ControlPanel(app, menuinlevel);
    if (!menu || menuerror) return;
    menu->app = app;
    M_SetupNextMenu(menu);
    app->dragging_select = false;
    app->selection_rect = (irect_t){0};
}

bool M_Responder(app_t *app, const SDL_Event *event, bool inlevel) {
    if (event->type == SDL_QUIT) { app->running = false; return true; }
    if (event->type == SDL_WINDOWEVENT) return false;
    if (!menuactive) {
        if (event->type != SDL_KEYDOWN || event->key.keysym.sym != SDLK_ESCAPE) return false;
        if (!event->key.repeat) M_StartControlPanel(app);
        return true;
    }
    menuinlevel = inlevel;
    return !currentmenu || M_MenuResponder(currentmenu, app, event);
}

void M_Ticker(void) {
    if (!menuactive || !currentmenu) {
        /* Dark Colony loops a hum while a screen is open. */
        S_StopUISound(UI_SOUND_SCREEN);
        return;
    }
    if (currentmenu->ticker) currentmenu->ticker(currentmenu);
    else M_MenuTicker(currentmenu);
}

void M_Drawer(const app_t *app) {
    (void)app;
    if (menuactive && currentmenu) M_MenuDrawer(currentmenu);
}

void M_Shutdown(void) {
    if (currentmenu) M_ClearMenus();
    G_ShutdownMenus();
    M_StopMessage();
    menuactive = false;
    menumap = NULL;
    currentmenu = NULL;
}

/* ── the fallback front end ─────────────────────────────────────────────── */

static void close_menu(menu_t *menu) {
    (void)menu;
    M_ClearMenus();
}

static void menu_escape(menu_t *menu) {
    if (level.width) close_menu(menu);
}

void M_MenuBeginLevel(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action != MA_ACTIVATE) return;
    if (!level.width && !netgame) menumap = g_game_default_map;
    close_menu(menu);
}

void M_MenuQuitGame(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action != MA_ACTIVATE) return;
    menu->app->running = false;
    close_menu(menu);
}

/* A table with a Begin Level button: it resumes inside a level. */
menu_t *M_SimpleControlPanel(menu_t *menu) {
    menu->modal = true;
    menu->escape = menu_escape;
    menu->held = NULL;
    menu->itemOn = -1;
    for (int i = 0; i < menu->numitems; ++i) {
        menuitem_t *item = &menu->items[i];
        if (item->routine == M_MenuBeginLevel)
            snprintf(item->text, sizeof(item->text), "%s", level.width ? "RESUME GAME" : "START GAME");
        if (menu->itemOn < 0 && item->visible && item->enabled && item->kind != MI_STATIC)
            menu->itemOn = i;
    }
    return menu;
}
