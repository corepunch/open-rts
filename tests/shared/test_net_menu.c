#include "t_local.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

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

static const char *race_name(int index) { return index ? "Orc" : "Human"; }

/* The open list sits under the control when the screen has room. */
static bool click_dropdown_row(app_t *app, int row) {
    if (!currentmenu || !currentmenu->dropdown || !currentmenu->dropdown->row_height) return false;
    menuitem_t *item = currentmenu->dropdown;
    if (row < item->first_row || row >= item->rows) return false;
    irect_t r = M_MenuItemRect(currentmenu, item);
    int shown = item->popup_rows > 0 && item->popup_rows < item->rows ? item->popup_rows : item->rows;
    int height = item->row_height;
    int bottom = app->win.h > 0 ? app->win.h : 480;
    int room = bottom - r.y - r.h;
    bool above = room < shown * height && r.y > room;
    if (above) room = r.y;
    if (shown > room / height) shown = room / height;
    if (shown < 1) shown = 1;
    if (row >= item->first_row + shown) return false;
    int y0 = above ? r.y - shown * height : r.y + r.h;
    SDL_Event event = {.type = SDL_MOUSEBUTTONDOWN};
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = r.x + r.w / 2;
    event.button.y = y0 + (row - item->first_row) * height + height / 2;
    M_Responder(app, &event, false);
    event.type = SDL_MOUSEBUTTONUP;
    M_Responder(app, &event, false);
    return !currentmenu->dropdown && item->value == row;
}

static bool click_list_row(app_t *app, int row) {
    for (int i = 0; currentmenu && i < currentmenu->numitems; ++i) {
        menuitem_t *item = &currentmenu->items[i];
        if (!item->visible || item->kind != MI_LIST) continue;
        irect_t r = M_MenuItemRect(currentmenu, item);
        SDL_Event event = {.type = SDL_MOUSEBUTTONDOWN};
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = r.x + 8;
        event.button.y = r.y + row * item->row_height + item->row_height / 2;
        M_Responder(app, &event, false);
        event.type = SDL_MOUSEBUTTONUP;
        M_Responder(app, &event, false);
        return true;
    }
    return false;
}

static bool wait_byte(int fd, char *byte, uint64_t deadline) {
    while (SDL_GetTicks64() < deadline) {
        if (read(fd, byte, 1) == 1) return true;
        SDL_Delay(1);
    }
    return false;
}

/* Host and joiner each own a race. The joiner's seat starts on Orc; the list selects Human. */
static int host_lobby(app_t *app, int ready_fd, int done_fd) {
    netui_t ui = {.back = back, .map_count = count, .map_path = path, .map_title = title,
                  .max_players = 3, .race_count = 2, .race_name = race_name};
    M_NetOpen(app, &ui);
    CHECK(click_text(app, "Create Game") && click_text(app, "Create Game") && I_NetMenuSession());
    CHECK(write(ready_fd, "R", 1) == 1);
    CHECK(click_text(app, "Start"));
    uint64_t deadline = SDL_GetTicks64() + 10000;
    while (!menumap && SDL_GetTicks64() < deadline) {
        M_Ticker();
        SDL_Delay(1);
    }
    CHECK(menumap && !strcmp(menumap, "first.map"));
    CHECK(M_NetPlayerRace(0) == 0 && M_NetPlayerRace(1) == 0);
    /* The joiner learns the launch from its next join retry, so keep answering. */
    char map[512], byte = 0;
    uint64_t reply = SDL_GetTicks64() + 5000;
    bool quit = false;
    while (SDL_GetTicks64() < reply) {
        I_PollNetGame(map, sizeof(map));
        if (read(done_fd, &byte, 1) == 1) { quit = true; break; }
        SDL_Delay(1);
    }
    CHECK(quit && byte == 'Q');
    I_ShutdownNetwork();
    return 0;
}

static int join_lobby(app_t *app, int ready_fd, int done_fd) {
    char byte = 0;
    CHECK(wait_byte(ready_fd, &byte, SDL_GetTicks64() + 5000) && byte == 'R');
    netui_t ui = {.back = back, .map_count = count, .map_path = path, .map_title = title,
                  .max_players = 3, .race_count = 2, .race_name = race_name};
    M_NetOpen(app, &ui);
    CHECK(click_text(app, "Join Game"));
    int games = 0;
    uint64_t deadline = SDL_GetTicks64() + 3000;
    do { M_Ticker(); I_NetGames(&games); SDL_Delay(1); } while (!games && SDL_GetTicks64() < deadline);
    CHECK(games > 0 && click_list_row(app, 0) && click_text(app, "Join Game"));
    deadline = SDL_GetTicks64() + 5000;
    do { M_Ticker(); SDL_Delay(1); } while (!has_text("Orc") && SDL_GetTicks64() < deadline);
    CHECK(click_text(app, "Orc") && click_dropdown_row(app, 0) && click_text(app, "Start"));
    deadline = SDL_GetTicks64() + 10000;
    while (!menumap && SDL_GetTicks64() < deadline) { M_Ticker(); SDL_Delay(1); }
    CHECK(menumap && !strcmp(menumap, "first.map"));
    CHECK(M_NetPlayerRace(0) == 0 && M_NetPlayerRace(1) == 0);
    CHECK(write(done_fd, "Q", 1) == 1);
    return 0;
}

static int lobby_race(app_t *app) {
    int ready[2], done[2];
    CHECK(pipe(ready) == 0 && pipe(done) == 0);
    CHECK(fcntl(ready[0], F_SETFL, O_NONBLOCK) == 0 && fcntl(done[0], F_SETFL, O_NONBLOCK) == 0);
    I_ShutdownNetwork();
    fflush(NULL);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        close(ready[0]);
        close(done[1]);
        _exit(host_lobby(app, ready[1], done[0]));
    }
    close(ready[1]);
    close(done[0]);
    int code = join_lobby(app, ready[0], done[1]);
    if (code) kill(child, SIGTERM);
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    close(ready[0]);
    close(done[1]);
    if (code) return code;
    CHECK(WIFEXITED(status) && !WEXITSTATUS(status));
    I_CancelNetGame();
    CHECK(M_NetPlayerRace(0) == 0 && M_NetPlayerRace(1) == 0);
    return 0;
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
    CHECK(click_text(&app, "Players: 2") && click_dropdown_row(&app, 1) && has_text("Players: 3"));
    CHECK(click_text(&app, "Players: 3") && click_dropdown_row(&app, 0) && has_text("Players: 2"));
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

    /* A second session: each player presses Start, and the game begins. */
    CHECK(lobby_race(&app) == 0);
    M_Shutdown();
    puts("PASS: shared network menu creates, browses, joins and starts when every player has");
    return 0;
}
