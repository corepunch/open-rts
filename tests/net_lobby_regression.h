/* Two processes drive the actual game menus, then load the negotiated map. */
#ifndef __NET_LOBBY_REGRESSION__
#define __NET_LOBBY_REGRESSION__
#include "t_local.h"
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "lobby pid=%ld line %d: %s (%s)\n", (long)getpid(), __LINE__, #c, M_NetNotice()); return 1; } } while (0)
static app_t app = {.win = {640, 480}, .running = true};
#ifdef RTS_GAME_STARCRAFT
enum { LIST = 5, CREATE = 15, JOIN = 13, PLAYERS = 9, READY = 6, RACE = 29, RACE_STEP = 4, CHAT = 9, CANCEL = 7, GUEST_RACE = 2 };
static const char *root = "data/STARCRAFT";
#else
enum { LIST = 4, CREATE = 6, JOIN = 2, PLAYERS = 5, READY = 18, RACE = 20, RACE_STEP = 1, CHAT = 11, CANCEL = 7, GUEST_RACE = 0 };
static const char *root = "data/WAR2";
#endif

static bool click(int id) {
    menuitem_t *item = M_MenuFind(currentmenu, id);
    if (!item || !item->visible || !item->enabled) return false;
    irect_t r = M_MenuItemRect(currentmenu, item);
    SDL_Event e = {.type = SDL_MOUSEBUTTONDOWN};
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = r.x + r.w / 2; e.button.y = r.y + r.h / 2;
    M_Responder(&app, &e, false);
    e.type = SDL_MOUSEBUTTONUP;
    M_Responder(&app, &e, false);
    return true;
}

static bool choose(int id, int row) {
    if (!click(id) || !currentmenu->dropdown) return false;
    SDL_Event e = {.type = SDL_KEYDOWN};
    e.key.keysym.sym = SDLK_HOME; M_Responder(&app, &e, false);
    e.key.keysym.sym = SDLK_DOWN;
    for (int i = 0; i < row; ++i) M_Responder(&app, &e, false);
    e.key.keysym.sym = SDLK_RETURN; M_Responder(&app, &e, false);
    menuitem_t *item = M_MenuFind(currentmenu, id);
    return item && item->value == row && !currentmenu->dropdown;
}

static int open_browser(bool host) {
    G_InitGame(); P_InitThinkers(); V_AllocScreen(640, 480);
    CHECK(M_Init(&app, root)); M_StartControlPanel(&app);
#ifdef RTS_GAME_STARCRAFT
    CHECK(click(4) && click(9));
    (void)host;
#else
    w2_text_t label;
    CHECK(w2_label(STR_MAIN_MENU, 2, &label));
    int id = -1;
    for (int i = 0; i < currentmenu->numitems; ++i)
        if (!strcmp(currentmenu->items[i].text, label.text)) id = currentmenu->items[i].id;
    CHECK(id >= 0 && click(id));
    CHECK(click(host ? 1 : 2));
#endif
    CHECK(M_MenuFind(currentmenu, LIST));
    return 0;
}

static bool wait_byte(int fd) {
    uint64_t end = SDL_GetTicks64() + 10000;
    char byte;
    while (SDL_GetTicks64() < end) {
        M_Ticker();
        if (currentmenu && currentmenu->refresh) currentmenu->refresh(currentmenu);
        if (read(fd, &byte, 1) == 1) return true;
        SDL_Delay(1);
    }
    return false;
}

static bool log_contains(const char *text) {
    uint64_t end = SDL_GetTicks64() + 10000;
    do {
        M_Ticker();
        if (strstr(M_NetLog(), text)) return true;
        SDL_Delay(1);
    } while (SDL_GetTicks64() < end);
    return false;
}

static bool say(const char *text) {
    menuitem_t *field = M_MenuFind(currentmenu, CHAT);
    if (!field || field->kind != MI_TEXTFIELD) return false;
    snprintf(field->text, sizeof(field->text), "%s", text);
    field->routine(currentmenu, field, MA_ACTIVATE);
    return !field->text[0];
}

static int launched_map(void) {
    uint64_t end = SDL_GetTicks64() + 10000;
    while (!menumap && SDL_GetTicks64() < end) { M_Ticker(); SDL_Delay(1); }
    CHECK(menumap && !menuactive && netgame);
    CHECK(M_NetPlayerRace(0) == 1 && M_NetPlayerRace(1) == GUEST_RACE);
    CHECK(game_speed == 120);
    char path[1024]; snprintf(path, sizeof(path), "%s/%s", root, menumap);
    CHECK(G_DoLoadLevel(path, &level));
#ifdef RTS_GAME_WARCRAFT_2
    const w2_pud_t *pud = level.native_data;
    CHECK(pud && pud->owners[0] == 5 && pud->owners[1] == 5);
    CHECK(pud->sides[0] == 1 && pud->sides[1] == 0);
    CHECK(pud->view_player == consoleplayer);
#endif
    CHECK(P_LoadThings(path) > 0);
    mobjlist_t all = P_ListMobjs();
    int owned[2] = {0};
    for (int i = 0; i < all.count; ++i) {
        mobj_t *unit = all.items[i];
        if (unit->owner < 2) ++owned[unit->owner];
#ifdef RTS_GAME_STARCRAFT
        if (unit->owner == 0) CHECK(unit->type_id == MT_HATCHERY || unit->type_id == MT_DRONE);
        if (unit->owner == 1) CHECK(unit->type_id == MT_NEXUS || unit->type_id == MT_PROBE);
#endif
    }
    CHECK(owned[0] && owned[1]);
#ifdef RTS_GAME_STARCRAFT
    CHECK(owned[0] == 5 && owned[1] == 5);
#endif
    P_FreeMobjList(&all); P_FreeLevel(&level);
    return 0;
}

static int host(int notify, int reply) {
    CHECK(!open_browser(true));
    menuitem_t *list = M_MenuFind(currentmenu, LIST);
#ifdef RTS_GAME_STARCRAFT
    int row = -1;
    for (int i = 0; i < list->rows; ++i)
        if (!strcmp(list->row(list, i), "(4)lost temple.scm")) row = i;
    CHECK(row >= 0);
    list->value = row; list->routine(currentmenu, list, MA_CHANGE);
    CHECK(choose(PLAYERS, 2) && choose(PLAYERS, 0));
    CHECK(choose(12, 11) && game_speed == 120);
#else
    CHECK(choose(PLAYERS, 0));
    D_SetGameSpeed(120);
#endif
    char title[128];
#ifdef RTS_GAME_STARCRAFT
    snprintf(title, sizeof(title), "%s", M_MenuFind(currentmenu, 7)->text);
#else
    snprintf(title, sizeof(title), "%s", list->row(list, list->value));
#endif
    CHECK(click(CREATE) && M_NetHosting() && doomcom->numplayers == 2);
#ifdef RTS_GAME_STARCRAFT
    CHECK(!strcmp(M_MenuFind(currentmenu, 14)->text, title));
#endif
    CHECK(choose(RACE, 1));
    CHECK(click(READY) && M_NetSeat(0)->ready);
    CHECK(!M_MenuFind(currentmenu, RACE)->enabled);
    CHECK(click(READY) && !M_NetSeat(0)->ready);
    CHECK(write(notify, title, strlen(title) + 1) == (ssize_t)strlen(title) + 1);
    CHECK(wait_byte(reply));
    CHECK(I_NetPlayerCount() == 2 && say("host hello"));
    CHECK(log_contains("guest hello"));
    CHECK(wait_byte(reply));
    CHECK(M_NetSeat(1)->race == GUEST_RACE && M_NetSeat(1)->ready);
    CHECK(!menumap && click(READY));
    CHECK(!launched_map());
    /* Keep answering the joiner's final poll after the host starts. */
    uint64_t end = SDL_GetTicks64() + 10000; char map[512], byte;
    bool done = false;
    while (SDL_GetTicks64() < end) {
        I_PollNetGame(map, sizeof(map));
        if (read(reply, &byte, 1) == 1) { done = true; break; }
        SDL_Delay(1);
    }
    CHECK(done);
    return 0;
}

static int guest(int notify, int reply) {
    char title[128] = {0};
    uint64_t end = SDL_GetTicks64() + 15000;
    while (read(notify, title, sizeof(title)) <= 0 && SDL_GetTicks64() < end) SDL_Delay(1);
    CHECK(title[0] && !open_browser(false));
    I_QueryNetGames("127.0.0.1");
    int count = 0;
    end = SDL_GetTicks64() + 5000;
    do { M_Ticker(); I_NetGames(&count); SDL_Delay(1); } while (!count && SDL_GetTicks64() < end);
    CHECK(count == 1 && !strcmp(I_NetGames(&count)[0].name, title));
    if (currentmenu->refresh) currentmenu->refresh(currentmenu);
    menuitem_t *list = M_MenuFind(currentmenu, LIST);
    list->value = 0; list->routine(currentmenu, list, MA_CHANGE);
    CHECK(click(JOIN));
    end = SDL_GetTicks64() + 5000;
    do { M_Ticker(); SDL_Delay(1); } while (!M_NetInLobby() && SDL_GetTicks64() < end);
    CHECK(M_NetInLobby() && M_NetLocalSlot() == 1);
    if (currentmenu->refresh) currentmenu->refresh(currentmenu);
    CHECK(choose(RACE + RACE_STEP, GUEST_RACE));
    CHECK(!click(RACE)); /* A guest cannot change the host's race. */
    CHECK(write(reply, "J", 1) == 1 && log_contains("host hello"));
    CHECK(say("guest hello"));
    CHECK(click(READY) && M_NetSeat(1)->ready);
    CHECK(write(reply, "R", 1) == 1);
    CHECK(!launched_map());
    CHECK(write(reply, "Q", 1) == 1);
    return 0;
}

int main(void) {
    int notify[2], reply[2];
    CHECK(!pipe(notify) && !pipe(reply));
    CHECK(!fcntl(notify[0], F_SETFL, O_NONBLOCK) && !fcntl(reply[0], F_SETFL, O_NONBLOCK));
    pid_t child = fork(); CHECK(child >= 0);
    CHECK(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER));
    if (!child) {
        close(notify[1]); close(reply[0]);
        int result = guest(notify[0], reply[1]);
        I_ShutdownNetwork(); M_Shutdown(); SDL_Quit(); _exit(result);
    }
    close(notify[0]); close(reply[1]);
    int result = host(notify[1], reply[0]), status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    I_ShutdownNetwork(); M_Shutdown(); SDL_Quit();
    CHECK(!result && WIFEXITED(status) && !WEXITSTATUS(status));
    puts("PASS: native multiplayer discovery, map-title server, player count, race, speed, chat, readiness and map launch");
    return 0;
}
#endif
