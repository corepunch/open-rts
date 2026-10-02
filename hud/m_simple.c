#include "engine.h"

/* The games without a native front end share only the lifecycle; each
 * supplies its own static item array, like Doom's menu definitions. */
#if !defined(RTS_GAME_DARK_COLONY) && !defined(RTS_GAME_DARK_REIGN)
bool menuactive;
bool menuerror;
const char *menumap;

static void close_menu(menu_t *menu) {
    menuactive = false;
    menu->held = NULL;
    SDL_StopTextInput();
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
    ((app_t *)menu->owner)->running = false;
    close_menu(menu);
}

bool M_Init(app_t *app, const char *root) {
    (void)root;
    menuactive = menuerror = false;
    M_StopMessage();
    menumap = NULL;
    gamemenu.owner = app;
    gamemenu.modal = true;
    gamemenu.escape = menu_escape;
    return true;
}

void M_StartControlPanel(app_t *app) {
    gamemenu.owner = app;
    gamemenu.held = NULL;
    gamemenu.itemOn = -1;
    for (int i = 0; i < gamemenu.numitems; ++i) {
        menuitem_t *item = &gamemenu.items[i];
        if (item->routine == M_MenuBeginLevel)
            snprintf(item->text, sizeof(item->text), "%s", level.width ? "RESUME GAME" : "START GAME");
        if (gamemenu.itemOn < 0 && item->visible && item->enabled && item->kind != MI_STATIC)
            gamemenu.itemOn = i;
    }
    menuactive = true;
}

bool M_Responder(app_t *app, const SDL_Event *event, bool inlevel) {
    (void)inlevel;
    if (event->type == SDL_QUIT) { app->running = false; return true; }
    if (event->type == SDL_WINDOWEVENT) return false;
    if (!menuactive) {
        if (event->type != SDL_KEYDOWN || event->key.keysym.sym != SDLK_ESCAPE) return false;
        if (!event->key.repeat) M_StartControlPanel(app);
        return true;
    }
    return M_MenuResponder(&gamemenu, app, event);
}

void M_Drawer(const app_t *app) {
    (void)app;
    if (menuactive) M_MenuDrawer(&gamemenu);
}

void M_Ticker(void) { if (menuactive) M_MenuTicker(&gamemenu); }
void M_Shutdown(void) { close_menu(&gamemenu); M_StopMessage(); menumap = NULL; gamemenu.owner = NULL; }
#endif
