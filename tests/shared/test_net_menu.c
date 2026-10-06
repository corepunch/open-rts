#include "t_local.h"

#define CHECK(c) RTS_CHECK(c, "network menu", #c)

static int count(void) { return 2; }
static const char *path(int i) { return i ? "second.map" : "first.map"; }
static const char *title(int i) { return i ? "Second" : "First"; }
static bool backed;
static void back(app_t *app) { (void)app; backed = true; M_ClearMenus(); }

static bool has_text(const char *text) {
    for (int i = 0; currentmenu && i < currentmenu->numitems; ++i)
        if (currentmenu->items[i].visible && !strcmp(currentmenu->items[i].text, text)) return true;
    return false;
}

static bool click_text(app_t *app, const char *text) {
    for (int i = 0; currentmenu && i < currentmenu->numitems; ++i) {
        menuitem_t *item = &currentmenu->items[i];
        if (!item->visible || !item->enabled || strcmp(item->text, text)) continue;
        irect_t r = M_MenuItemRect(currentmenu, item);
        SDL_Event event = {.type = SDL_MOUSEBUTTONDOWN};
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = r.x + r.w / 2;
        event.button.y = r.y + r.h / 2;
        M_Responder(app, &event, false);
        event.type = SDL_MOUSEBUTTONUP;
        M_Responder(app, &event, false);
        return true;
    }
    return false;
}

static void escape(app_t *app) {
    SDL_Event event = {.type = SDL_KEYDOWN};
    event.key.keysym.sym = SDLK_ESCAPE;
    M_Responder(app, &event, false);
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    app_t app = {.win = {640, 480}, .running = true};
    V_AllocScreen(640, 480);
    menuactive = false;
    level.width = 0;
    netui_t ui = {.back = back, .map_count = count, .map_path = path, .map_title = title, .max_players = 3};
    M_NetOpen(&app, &ui);
    CHECK(menuactive && has_text("Create Game") && has_text("Join Game") && has_text("Previous Menu"));
    V_BeginFrame(0xff000000u);
    M_Drawer(&app);

    /* Create: the host picks a scenario and a player count within the game's limit. */
    CHECK(click_text(&app, "Create Game"));
    CHECK(has_text("Select scenario") && has_text("Players: 2"));
    CHECK(click_text(&app, "Players: 2") && has_text("Players: 3"));
    CHECK(click_text(&app, "Players: 3") && has_text("Players: 2"));
    V_BeginFrame(0xff000000u);
    M_Drawer(&app);
    escape(&app);
    CHECK(has_text("Create Game"));

    /* Join: the LAN browser opens and closes without a session left behind. */
    CHECK(click_text(&app, "Join Game"));
    CHECK(has_text("LAN games") && has_text("Refresh") && has_text("Address"));
    M_Ticker();
    escape(&app);
    CHECK(has_text("Join Game"));
    CHECK(click_text(&app, "Join Game") && click_text(&app, "Address"));
    CHECK(has_text("Connect"));
    escape(&app);
    CHECK(has_text("Create Game") && !I_NetMenuSession());

    /* Previous Menu leaves to the game's own screen. */
    CHECK(click_text(&app, "Previous Menu") && backed && !menuactive);
    M_Shutdown();
    puts("PASS: shared network menu creates, browses and joins through the engine transport");
    return 0;
}
