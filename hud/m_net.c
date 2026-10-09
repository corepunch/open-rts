#include "engine.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The multiplayer screens every game shares, lifted from Dark Colony's LAN
 * lobby: create a game (map, players), browse the LAN or type an address to
 * join, wait in a lobby with chat, then launch. The session under those
 * screens — seats, ready, chat and the start rule — is the same one a game
 * uses when it draws its own lobby. With races, every joined player presses
 * Start. Without races, the host starts once the roster is full. */

enum { PAGE_MAIN, PAGE_HOST, PAGE_LOBBY, PAGE_BROWSE, PAGE_CONNECT };
enum {
    ID_CREATE = 1, ID_JOIN, ID_PREVIOUS, ID_LIST, ID_PLAYERS, ID_START, ID_CANCEL, ID_REFRESH,
    ID_ADDRESS_FIELD, ID_CONNECT, ID_CHAT, ID_DIRECT, ID_STATUS, ID_LOG,
    ID_READY = 18, ID_RACE = 20
};
enum { MAX_ITEMS = 48, BOX_W = 352, BOX_H = 352 };

static const char *const defaults[NETTEXT_COUNT] = {
    [NETTEXT_TITLE] = "Multiplayer", [NETTEXT_CREATE] = "Create Game", [NETTEXT_JOIN] = "Join Game",
    [NETTEXT_PREVIOUS] = "Previous Menu", [NETTEXT_START] = "Start", [NETTEXT_CANCEL] = "Cancel",
    [NETTEXT_SCENARIO] = "Select scenario", [NETTEXT_PLAYERS] = "Players",
    [NETTEXT_REFRESH] = "Refresh", [NETTEXT_ADDRESS] = "Address", [NETTEXT_CONNECT] = "Connect",
    [NETTEXT_SESSIONS] = "LAN games", [NETTEXT_READY] = "Ready", [NETTEXT_UNREADY] = "Unready",
    [NETTEXT_YOU] = "You", [NETTEXT_OPEN] = "Open",
};

static menuitem_t items[MAX_ITEMS];
static menu_t net_menu = {.items = items, .modal = true};
static netui_t ui;
static app_t *net_app;
static int page, players = 2, selected_map = -1, selected_game = -1;
static char address[128] = "127.0.0.1", status[256], launch_map[512], log_text[NETCHAT_LENGTH * 33 + 8];
static int status_item = -1, list_item = -1, log_item = -1, chat_item = -1, start_item = -1,
           players_item = -1, last_chat;
static char rows[16][128];
static int name_item[MAXPLAYERS], race_item[MAXPLAYERS], ready_item = -1;

/* Published setup: eight seats, sixteen host-owned option bytes, eight ready
 * flags. A joiner's choice is the four seat fields and its ready flag. */
enum { SETUP_BYTES = 56, CHOICE_BYTES = 5, OPTION_BYTES = 16 };
_Static_assert(SETUP_BYTES <= 64 && CHOICE_BYTES <= 8, "lobby records fit the menu transport");
_Static_assert(MAXPLAYERS == 8, "lobby setup is eight seats");
static netplay_t play;
static netseat_t seat[MAXPLAYERS];
static uint8_t options[OPTION_BYTES];
static bool waiting, hosting, browsing, in_lobby, picked, choice_dirty, have_setup;
static int launch_race[MAXPLAYERS];
static bool launch_races;

static const char *word(int id) { return ui.text[id] ? ui.text[id] : defaults[id]; }
static int max_players(void) {
    return ui.max_players >= 2 && ui.max_players <= MAXPLAYERS ? ui.max_players : 4;
}
static int race_count(void) {
    return ui.race_count >= 2 && ui.race_count <= 8 && ui.race_name ? ui.race_count : 0;
}
static int race_slots(void) {
    return play.race_count >= 2 && play.race_count <= 8 ? play.race_count : 0;
}

static int play_cap(void) {
    return play.max_players >= 2 && play.max_players <= MAXPLAYERS ? play.max_players : 4;
}

int M_NetLocalSlot(void) {
    if ((hosting || waiting || in_lobby) && doomcom) return doomcom->consoleplayer;
    return 0;
}

static int local_slot(void) { return M_NetLocalSlot(); }

const netseat_t *M_NetSeat(int player) {
    if (player < 0 || player >= MAXPLAYERS) return NULL;
    return &seat[player];
}

static bool publish(void) {
    if (!hosting) return true;
    uint8_t setup[SETUP_BYTES];
    memset(setup, 0, sizeof(setup));
    int live = doomcom ? doomcom->numplayers : MAXPLAYERS;
    for (int i = 0; i < MAXPLAYERS; ++i) {
        setup[i * 4] = seat[i].race;
        setup[i * 4 + 1] = seat[i].type;
        setup[i * 4 + 2] = seat[i].color;
        setup[i * 4 + 3] = seat[i].team;
        setup[48 + i] = i < live && seat[i].ready ? 1 : 0;
    }
    memcpy(setup + 32, options, OPTION_BYTES);
    if (!I_SetNetSetup(setup, sizeof(setup))) {
        if (!status[0])
            snprintf(status, sizeof(status), "%s", neterror[0] ? neterror : "The connection failed");
        return false;
    }
    have_setup = true;
    return true;
}

static void send_choice(void) {
    if (hosting || !waiting || race_slots() <= 0 || !in_lobby) return;
    int me = local_slot();
    if (me < 0 || me >= MAXPLAYERS) return;
    uint8_t choice[CHOICE_BYTES] = {
        seat[me].race, seat[me].type, seat[me].color, seat[me].team, seat[me].ready ? 1 : 0
    };
    if (I_SetNetChoice(choice, sizeof(choice))) choice_dirty = false;
}

/* A missing choice leaves the seat's race alone and drops a departed player's ready flag. */
static bool take_choices(void) {
    if (!hosting || race_slots() <= 0 || !doomcom) return true;
    bool changed = false;
    int races = race_slots();
    for (int i = 1; i < doomcom->numplayers && i < MAXPLAYERS; ++i) {
        uint8_t choice[CHOICE_BYTES];
        if (i < I_NetPlayerCount() && I_NetChoice(i, choice, sizeof(choice)) == sizeof(choice) && choice[4] <= 1) {
            int fields = play.joiner_fields;
            if ((fields & NET_FIELD_RACE) && choice[0] < races && seat[i].race != choice[0]) {
                seat[i].race = choice[0];
                changed = true;
            }
            if ((fields & NET_FIELD_TYPE) && seat[i].type != choice[1]) { seat[i].type = choice[1]; changed = true; }
            if ((fields & NET_FIELD_COLOR) && seat[i].color != choice[2]) { seat[i].color = choice[2]; changed = true; }
            if ((fields & NET_FIELD_TEAM) && seat[i].team != choice[3]) { seat[i].team = choice[3]; changed = true; }
            if (seat[i].ready != (choice[4] != 0)) { seat[i].ready = choice[4] != 0; changed = true; }
        } else if (i >= I_NetPlayerCount() && seat[i].ready) {
            seat[i].ready = false;
            changed = true;
        }
    }
    for (int i = doomcom->numplayers; i < MAXPLAYERS; ++i)
        if (seat[i].ready) { seat[i].ready = false; changed = true; }
    return !changed || publish();
}

/* I_NetSetup reports only as many bytes as asked for. Ask for the transport
 * limit so a shorter or longer record is not mistaken for this lobby. */
static size_t setup_bytes(void) {
    uint8_t wire[64];
    return I_NetSetup(wire, sizeof(wire));
}

static bool read_setup(bool final) {
    uint8_t setup[64];
    if (I_NetSetup(setup, sizeof(setup)) != SETUP_BYTES) return false;
    for (int i = 0; i < MAXPLAYERS; ++i) if (setup[48 + i] > 1) return false;
    int live = doomcom ? doomcom->numplayers : MAXPLAYERS;
    int me = local_slot();
    for (int i = 0; i < MAXPLAYERS; ++i) {
        netseat_t incoming = {
            .race = setup[i * 4], .type = setup[i * 4 + 1], .color = setup[i * 4 + 2],
            .team = setup[i * 4 + 3], .ready = i < live && setup[48 + i] == 1
        };
        if (!final && !hosting && i == me && picked) {
            int fields = play.joiner_fields;
            if (fields & NET_FIELD_RACE) incoming.race = seat[i].race;
            if (fields & NET_FIELD_TYPE) incoming.type = seat[i].type;
            if (fields & NET_FIELD_COLOR) incoming.color = seat[i].color;
            if (fields & NET_FIELD_TEAM) incoming.team = seat[i].team;
            incoming.ready = seat[i].ready;
        }
        seat[i] = incoming;
    }
    memcpy(options, setup + 32, OPTION_BYTES);
    have_setup = true;
    return true;
}

static bool everyone_ready(void) {
    if (race_slots() <= 0 || !hosting || !doomcom || I_NetPlayerCount() != doomcom->numplayers) return false;
    for (int i = 0; i < doomcom->numplayers; ++i) if (!seat[i].ready) return false;
    return true;
}

void M_NetUse(const netplay_t *next) {
    if (!next) return;
    play = *next;
    memset(seat, 0, sizeof(seat));
    memset(options, 0, sizeof(options));
    waiting = hosting = browsing = in_lobby = picked = choice_dirty = have_setup = false;
    launch_races = false;
    status[0] = '\0';
}

bool M_NetSetSeat(int player, const netseat_t *next) {
    if (!next || player < 0 || player >= MAXPLAYERS) return false;
    if (!waiting && !hosting) { seat[player] = *next; return true; }
    if (hosting) {
        seat[player] = *next;
        return publish();
    }
    if (player != local_slot()) return false;
    int fields = play.joiner_fields;
    bool changed = false;
    if ((fields & NET_FIELD_RACE) && seat[player].race != next->race) { seat[player].race = next->race; changed = true; }
    if ((fields & NET_FIELD_TYPE) && seat[player].type != next->type) { seat[player].type = next->type; changed = true; }
    if ((fields & NET_FIELD_COLOR) && seat[player].color != next->color) { seat[player].color = next->color; changed = true; }
    if ((fields & NET_FIELD_TEAM) && seat[player].team != next->team) { seat[player].team = next->team; changed = true; }
    picked = true;
    if (changed) choice_dirty = true;
    if (choice_dirty) send_choice();
    return true;
}

void M_NetSetOptions(const void *data, size_t size) {
    if (!data || (waiting && !hosting)) return;
    if (size > OPTION_BYTES) size = OPTION_BYTES;
    memset(options, 0, sizeof(options));
    memcpy(options, data, size);
    if (hosting) publish();
}

size_t M_NetOptions(void *data, size_t capacity) {
    if (!data || !capacity || !have_setup) return 0;
    size_t n = capacity < OPTION_BYTES ? capacity : OPTION_BYTES;
    memcpy(data, options, n);
    return n;
}

/* The seat's race, as an index into the game's list. A dropdown reports the
 * row; Dark Colony's two-state button still steps with M_NetCycleRace. */
static bool assign_race(int player, int value) {
    int n = race_slots();
    if (n <= 0 || value < 0 || value >= n || player < 0 || player >= MAXPLAYERS || seat[player].ready)
        return false;
    if ((waiting || hosting) && player != local_slot()) return false;
    if (waiting && !hosting && !(play.joiner_fields & NET_FIELD_RACE)) return false;
    seat[player].race = (uint8_t)value;
    picked = true;
    if (hosting) publish();
    else if (waiting) { choice_dirty = true; send_choice(); }
    return true;
}

void M_NetCycleRace(int player) {
    int n = race_slots();
    if (n <= 0 || player < 0 || player >= MAXPLAYERS) return;
    assign_race(player, (seat[player].race + 1) % n);
}

void M_NetToggleReady(void) {
    int me = local_slot();
    if (me < 0 || me >= MAXPLAYERS || (!waiting && !hosting)) return;
    seat[me].ready = !seat[me].ready;
    picked = true;
    if (hosting) publish();
    else { choice_dirty = true; send_choice(); }
}

bool M_NetChat(const char *text) {
    if (!text || !text[0] || (!waiting && !hosting)) return false;
    char speaker[32];
    const char *who = play.chat_name ? play.chat_name() : NULL;
    if (!who || !who[0]) who = hosting ? "Host" : M_va("Player %d", local_slot() + 1);
    snprintf(speaker, sizeof(speaker), "%s", who);
    char line[NETCHAT_LENGTH];
    snprintf(line, sizeof(line), "%s: %s", speaker, text);
    return I_SendNetChat(line);
}

const char *M_NetLog(void) {
    log_text[0] = '\0';
    int count = I_NetChatCount();
    for (int id = count > 32 ? count - 31 : 1; id <= count; ++id) {
        const char *line = I_NetChatLine(id);
        if (!line) continue;
        size_t used = strlen(log_text);
        snprintf(log_text + used, sizeof(log_text) - used, "%s\n", line);
    }
    return log_text;
}

const char *M_NetNotice(void) { return status; }
bool M_NetHosting(void) { return hosting; }
bool M_NetInLobby(void) { return in_lobby; }

bool M_NetHost(const char *title, const char *path, int count) {
    if (!path || !path[0] || count < 2) return false;
    int cap = play_cap();
    if (count > cap) count = cap;
    snprintf(launch_map, sizeof(launch_map), "%s", path);
    if (!I_HostNetGame(g_game_id, title && title[0] ? title : path, launch_map, count)) {
        snprintf(status, sizeof(status), "%s", neterror[0] ? neterror : "The connection failed");
        waiting = hosting = browsing = in_lobby = false;
        if (I_NetMenuSession()) I_CancelNetGame();
        return false;
    }
    hosting = waiting = in_lobby = true;
    browsing = false;
    picked = false;
    if (!publish()) {
        waiting = hosting = in_lobby = false;
        I_CancelNetGame();
        return false;
    }
    return true;
}

bool M_NetSetPlayers(int count) {
    if (!hosting || count > play_cap() || !I_SetNetPlayers(count)) return false;
    return publish();
}

bool M_NetJoinAddress(const char *where) {
    if (!where || !where[0]) return false;
    char copy[128];
    snprintf(copy, sizeof(copy), "%s", where);
    snprintf(address, sizeof(address), "%s", copy);
    if (browsing || waiting || hosting || I_NetMenuSession()) I_CancelNetGame();
    browsing = waiting = hosting = in_lobby = false;
    picked = false;
    choice_dirty = true;
    have_setup = false;
    if (!I_JoinNetGame(g_game_id, where)) {
        snprintf(status, sizeof(status), "%s", neterror[0] ? neterror : "The connection failed");
        if (I_NetMenuSession()) I_CancelNetGame();
        return false;
    }
    waiting = true;
    return true;
}

bool M_NetBrowse(void) {
    if (browsing || waiting || hosting || I_NetMenuSession()) I_CancelNetGame();
    waiting = hosting = in_lobby = browsing = false;
    if (!I_OpenNetBrowser(g_game_id)) {
        snprintf(status, sizeof(status), "%s", neterror[0] ? neterror : "The connection failed");
        if (I_NetMenuSession()) I_CancelNetGame();
        return false;
    }
    browsing = true;
    return true;
}

bool M_NetJoinListed(int index) {
    int count = 0;
    const netgame_t *games = I_NetGames(&count);
    if (index < 0 || index >= count) return false;
    char where[64];
    snprintf(where, sizeof(where), "%s", games[index].address);
    return M_NetJoinAddress(where);
}

void M_NetStop(void) {
    if (waiting || hosting || browsing || I_NetMenuSession()) I_CancelNetGame();
    waiting = hosting = browsing = in_lobby = false;
    picked = choice_dirty = false;
    SDL_StopTextInput();
}

static void finish_launch(void) {
    read_setup(true);
    launch_races = race_slots() > 0 && doomcom;
    for (int i = 0; i < MAXPLAYERS; ++i)
        launch_race[i] = launch_races && i < doomcom->numplayers ? seat[i].race : -1;
    if (play.commit) play.commit();
    menumap = launch_map;
    SDL_StopTextInput();
    waiting = hosting = in_lobby = false;
}

int M_NetPoll(void) {
    if (!waiting) return 0;
    int result = I_PollNetGame(launch_map, sizeof(launch_map));
    if (result < 0) {
        snprintf(status, sizeof(status), "%s", neterror[0] ? neterror : "The connection failed");
        M_NetStop();
        return -1;
    }
    if (result > 0) {
        /* No setup at all is a command-line host. Any other size is not this lobby. */
        if (setup_bytes() && !read_setup(true)) {
            snprintf(status, sizeof(status), "Host sent an invalid game setup");
            M_NetStop();
            menumap = NULL;
            return -1;
        }
        finish_launch();
        return 1;
    }
    if (hosting) {
        if (!take_choices()) { M_NetStop(); return -1; }
        if (everyone_ready()) I_LaunchNetGame();
    } else if (I_NetLobby()) {
        in_lobby = true;
        size_t got = setup_bytes();
        if (got && got != SETUP_BYTES) {
            snprintf(status, sizeof(status), "Host sent an invalid game setup");
            M_NetStop();
            return -1;
        }
        if (got == SETUP_BYTES && !read_setup(false)) {
            snprintf(status, sizeof(status), "Host sent an invalid game setup");
            M_NetStop();
            return -1;
        }
        if (have_setup && choice_dirty) send_choice();
    }
    return 0;
}

const char *M_NetLaunchMap(void) { return launch_map; }

/* A map that names its person-slot count caps the host's player button. */
static int player_cap(void) {
    int cap = max_players();
    if (ui.map_players && selected_map >= 0) {
        int seats = ui.map_players(selected_map);
        if (seats >= 2 && seats < cap) cap = seats;
    }
    return cap;
}

static void take_map_players(void) {
    if (!ui.map_players || selected_map < 0) return;
    int seats = ui.map_players(selected_map);
    if (seats < 2) seats = 2;
    if (seats > max_players()) seats = max_players();
    players = seats;
}

static const char *race_label(int index) {
    const char *name = ui.race_name ? ui.race_name(index) : NULL;
    return name && name[0] ? name : "?";
}

/* A game's own "Players:" already ends in a colon. */
static void players_label(char *out, size_t size, int count) {
    const char *name = word(NETTEXT_PLAYERS);
    size_t length = strlen(name);
    snprintf(out, size, length && name[length - 1] == ':' ? "%s %d" : "%s: %d", name, count);
}

static void players_text(char *out, size_t size) { players_label(out, size, players); }

static const char *players_row(const menuitem_t *item, int row) {
    (void)item;
    static char label[64];
    players_label(label, sizeof(label), row + 2);
    return label;
}

static const char *race_row(const menuitem_t *item, int row) {
    (void)item;
    return race_label(row);
}

static void open_page(int next);
static void paint_lobby(void);
static void routine(menu_t *menu, menuitem_t *item, menuaction_t action);

static menuitem_t *add(menuitemkind_t kind, int id, irect_t rect, const char *text) {
    menuitem_t *item = &items[net_menu.numitems < MAX_ITEMS ? net_menu.numitems++ : MAX_ITEMS - 1];
    memset(item, 0, sizeof(*item));
    item->kind = kind;
    item->id = id;
    item->rect = rect;
    item->visible = true;
    item->enabled = kind != MI_STATIC;
    item->link = -1;
    item->font = ui.font;
    item->align = MALIGN_CENTER;
    item->routine = routine;
    snprintf(item->text, sizeof(item->text), "%s", text ? text : "");
    return item;
}

static menuitem_t *label(irect_t rect, const char *text, int align) {
    menuitem_t *item = add(MI_STATIC, 0, rect, text);
    item->align = align;
    item->ink = 0xffffe84au;
    if (ui.style_label) ui.style_label(item);
    return item;
}

static menuitem_t *button(int id, irect_t rect, const char *text, SDL_Keycode key) {
    menuitem_t *item = add(MI_BUTTON, id, rect, text);
    item->hotkey = key;
    item->fill = 0xff18242du;
    item->color = 0xffdce6dcu;
    item->ink = 0xffffe84au;
    if (ui.style_button) ui.style_button(item);
    return item;
}

/* The engine opens, hits and cancels the list. The closed label stays in
 * text so a screen can find the current choice by its words. A game's button
 * style may replace the plain face with its own dropdown art. */
static menuitem_t *dropdown(int id, irect_t rect, SDL_Keycode key,
                            const char *(*row)(const menuitem_t *, int), int count, int value) {
    menuitem_t *item = add(MI_DROPDOWN, id, rect, "");
    item->hotkey = key;
    item->fill = 0xff18242du;
    item->color = 0xff5a3c14u;
    item->ink = 0xffffe84au;
    item->row = row;
    item->rows = count > 0 ? count : 0;
    item->value = value >= 0 && value < item->rows ? value : 0;
    item->row_height = rect.h > 0 ? rect.h : 18;
    item->popup_rows = item->rows > 8 ? 8 : 0;
    if (ui.style_button) ui.style_button(item);
    if (item->row_height <= 0) item->row_height = rect.h > 0 ? rect.h : 18;
    if (item->row && item->value >= 0 && item->value < item->rows)
        snprintf(item->text, sizeof(item->text), "%s", item->row(item, item->value));
    return item;
}

static void field(int id, irect_t rect, const char *text) {
    menuitem_t *item = add(MI_TEXTFIELD, id, rect, text);
    item->align = MALIGN_LEFT;
    item->fill = 0xff0c1216u;
    item->border = 0xffdce6dcu;
    item->ink = 0xffffffffu;
    item->maxchars = id == ID_CHAT ? NETCHAT_LENGTH - 24 : 100;
    item->inset = (ivec2_t){4, 0};
}

static int box_x(void) { return (640 - BOX_W) / 2; }
static int box_y(void) { return (480 - BOX_H) / 2; }

static void set_status(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void set_status(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(status, sizeof(status), format, args);
    va_end(args);
    if (status_item >= 0) {
        menuitem_t *item = &items[status_item];
        item->prose = status;
        item->text[0] = '\0';
    }
}

static const char *row_text(const menuitem_t *item, int row) {
    (void)item;
    return row >= 0 && row < 16 ? rows[row] : "";
}

static void failure(void) {
    if (neterror[0]) snprintf(status, sizeof(status), "%s", neterror);
    else if (!status[0]) snprintf(status, sizeof(status), "The connection failed");
    neterror[0] = '\0';
    M_NetStop();
    open_page(PAGE_MAIN);
}

/* The host's seats, stored before the session opens so the first publish
 * already carries the map's races. */
static void stock_prepare_seats(void) {
    int n = race_count();
    memset(seat, 0, sizeof(seat));
    memset(options, 0, sizeof(options));
    have_setup = false;
    for (int i = 0; i < MAXPLAYERS; ++i)
        seat[i] = (netseat_t){.race = n > 0 ? (uint8_t)(i % n) : 0, .type = 1,
                              .color = (uint8_t)i, .team = (uint8_t)i};
    if (n > 0 && ui.slot_race && selected_map >= 0)
        for (int i = 0; i < players && i < MAXPLAYERS; ++i) {
            int authored = ui.slot_race(selected_map, i);
            if (authored >= 0 && authored < n) seat[i].race = (uint8_t)authored;
        }
}

static void refresh_log(void) {
    M_NetLog();
    int lines = 0;
    for (const char *p = log_text; *p; ++p) lines += *p == '\n';
    if (log_item >= 0) {
        menuitem_t *item = &items[log_item];
        const bitmapfont_t *font = item->font;
        int per = font && font->line_h > 0 ? item->rect.h / font->line_h : 8;
        item->prose = log_text;
        item->first_row = lines > per ? lines - per : 0;
    }
    last_chat = I_NetChatCount();
}

static void build_main(void) {
    int x = box_x(), y = box_y();
    label((irect_t){x, y + 12, BOX_W, 24}, word(NETTEXT_TITLE), MALIGN_CENTER);
    button(ID_CREATE, (irect_t){x + 64, y + 100, 224, 28}, word(NETTEXT_CREATE), SDLK_c);
    button(ID_JOIN, (irect_t){x + 64, y + 140, 224, 28}, word(NETTEXT_JOIN), SDLK_j);
    button(ID_PREVIOUS, (irect_t){x + 64, y + 180, 224, 28}, word(NETTEXT_PREVIOUS), SDLK_ESCAPE);
    status_item = net_menu.numitems;
    label((irect_t){x + 16, y + 240, BOX_W - 32, 60}, "", MALIGN_LEFT);
    items[status_item].prose = status;
}

static const char *map_row(const menuitem_t *item, int row) {
    (void)item;
    const char *title = ui.map_title ? ui.map_title(row) : NULL;
    return title ? title : "";
}

static void build_host(void) {
    int x = box_x(), y = box_y(), count = ui.map_count ? ui.map_count() : 0;
    label((irect_t){x, y + 8, BOX_W, 24}, word(NETTEXT_SCENARIO), MALIGN_CENTER);
    menuitem_t *list = add(MI_LIST, ID_LIST, (irect_t){x + 16, y + 40, 300, 180}, "");
    list->align = MALIGN_LEFT;
    list->ink = 0xffffe84au;
    list->look[MS_PUSHED].ink = 0xffffffffu;
    list->color = 0xff5a3c14u;
    list->row_height = list->font && list->font->line_h > 0 ? list->font->line_h + 2 : 18;
    list->row = map_row;
    list->rows = count;
    list->inset = (ivec2_t){4, 1};
    list_item = net_menu.numitems - 1;
    list->value = selected_map;
    menuitem_t *bar = add(MI_SCROLLBAR, 0, (irect_t){x + 322, y + 40, 12, 180}, "");
    bar->link = list_item;
    bar->color = 0xffb89040u;
    if (ui.style_list) ui.style_list(&items[list_item], bar);
    dropdown(ID_PLAYERS, (irect_t){x + 64, y + 236, 224, 28}, SDLK_p, players_row, player_cap() - 1, players - 2);
    players_item = net_menu.numitems - 1;
    button(ID_START, (irect_t){x + 48, y + 318, 106, 28}, word(NETTEXT_CREATE), SDLK_c);
    start_item = net_menu.numitems - 1;
    items[start_item].enabled = selected_map >= 0 && selected_map < count;
    if (!count) set_status("No maps were found");
    button(ID_CANCEL, (irect_t){x + 198, y + 318, 106, 28}, word(NETTEXT_CANCEL), SDLK_ESCAPE);
    status_item = net_menu.numitems;
    label((irect_t){x + 16, y + 272, BOX_W - 32, 36}, "", MALIGN_LEFT);
    items[status_item].prose = status;
}

static void seat_name(char *out, size_t size, int slot) {
    int joined = I_NetPlayerCount();
    if (slot >= joined) snprintf(out, size, "%s", word(NETTEXT_OPEN));
    else if (slot == local_slot()) snprintf(out, size, "%s", word(NETTEXT_YOU));
    else snprintf(out, size, "Player %d", slot + 1);
}

static void paint_lobby(void) {
    int seats = doomcom ? doomcom->numplayers : 0;
    if (seats > MAXPLAYERS) seats = MAXPLAYERS;
    for (int i = 0; i < seats; ++i) {
        if (name_item[i] >= 0) seat_name(items[name_item[i]].text, sizeof(items[0].text), i);
        if (race_item[i] >= 0) {
            menuitem_t *item = &items[race_item[i]];
            int n = race_count();
            if (n > 0 && seat[i].race < n) item->value = seat[i].race;
            if (item->row && item->value >= 0 && item->value < item->rows)
                snprintf(item->text, sizeof(item->text), "%s", item->row(item, item->value));
            bool mine = i == local_slot() && i < I_NetPlayerCount();
            item->enabled = item->visible = mine || i < I_NetPlayerCount();
            if (!mine) item->enabled = false;
            if (mine && seat[i].ready) item->enabled = false;
        }
    }
    if (ready_item >= 0)
        snprintf(items[ready_item].text, sizeof(items[0].text), "%s",
                 seat[local_slot()].ready ? word(NETTEXT_UNREADY) : word(NETTEXT_START));
    if (start_item >= 0) {
        bool full = doomcom && I_NetPlayerCount() == doomcom->numplayers;
        bool armed = full;
        if (race_count() > 0)
            for (int i = 0; armed && doomcom && i < doomcom->numplayers; ++i) armed = seat[i].ready;
        items[start_item].enabled = armed;
    }
}

static void build_lobby(void) {
    int x = box_x(), y = box_y();
    int seats = doomcom && doomcom->numplayers > 0 ? doomcom->numplayers : players;
    if (seats > MAXPLAYERS) seats = MAXPLAYERS;
    const char *shown = I_NetMap();
    if (hosting && ui.map_title && selected_map >= 0 && ui.map_title(selected_map))
        shown = ui.map_title(selected_map);
    label((irect_t){x, y + 4, BOX_W, 22}, shown && shown[0] ? shown : word(NETTEXT_TITLE), MALIGN_CENTER);
    int row_h = 22, rows_y = 28;
    for (int i = 0; i < MAXPLAYERS; ++i) name_item[i] = race_item[i] = -1;
    if (race_count() > 0) {
        for (int i = 0; i < seats; ++i) {
            name_item[i] = net_menu.numitems;
            label((irect_t){x + 16, y + rows_y + i * row_h, 140, row_h}, "", MALIGN_LEFT);
            race_item[i] = net_menu.numitems;
            dropdown(ID_RACE + i, (irect_t){x + 164, y + rows_y + i * row_h, 168, row_h - 2}, 0,
                     race_row, race_count(), seat[i].race);
        }
        rows_y += seats * row_h;
    }
    int buttons_y = BOX_H - 34;
    int field_y = buttons_y - 48;
    int log_y = rows_y + 4;
    int log_h = field_y - log_y - 4;
    if (log_h < 28) log_h = 28;
    log_item = net_menu.numitems;
    menuitem_t *log = label((irect_t){x + 16, y + log_y, BOX_W - 32, log_h}, "", MALIGN_LEFT);
    log->prose = log_text;
    chat_item = net_menu.numitems;
    field(ID_CHAT, (irect_t){x + 16, y + field_y, BOX_W - 32, 20}, "");
    net_menu.itemOn = chat_item;
    status_item = net_menu.numitems;
    label((irect_t){x + 16, y + field_y + 22, BOX_W - 32, 22}, "", MALIGN_LEFT);
    items[status_item].prose = status;
    /* With races, both players press the same Start. The host launches once
     * every joined player has. Without races, only the host has Start. */
    ready_item = start_item = -1;
    if (race_count() > 0) {
        button(ID_READY, (irect_t){x + 16, y + buttons_y, 106, 28}, word(NETTEXT_START), SDLK_s);
        ready_item = net_menu.numitems - 1;
        button(ID_CANCEL, (irect_t){x + 230, y + buttons_y, 106, 28}, word(NETTEXT_CANCEL), SDLK_ESCAPE);
    } else if (hosting) {
        button(ID_START, (irect_t){x + 48, y + buttons_y, 106, 28}, word(NETTEXT_START), SDLK_RETURN);
        start_item = net_menu.numitems - 1;
        items[start_item].enabled = false;
        button(ID_CANCEL, (irect_t){x + 198, y + buttons_y, 106, 28}, word(NETTEXT_CANCEL), SDLK_ESCAPE);
    } else {
        button(ID_CANCEL, (irect_t){x + 123, y + buttons_y, 106, 28}, word(NETTEXT_CANCEL), SDLK_ESCAPE);
    }
    paint_lobby();
    refresh_log();
}

static void build_browse(void) {
    int x = box_x(), y = box_y();
    label((irect_t){x, y + 8, BOX_W, 24}, word(NETTEXT_SESSIONS), MALIGN_CENTER);
    menuitem_t *list = add(MI_LIST, ID_LIST, (irect_t){x + 16, y + 40, 300, 180}, "");
    list->align = MALIGN_LEFT;
    list->ink = 0xffffe84au;
    list->look[MS_PUSHED].ink = 0xffffffffu;
    list->color = 0xff5a3c14u;
    list->row_height = list->font && list->font->line_h > 0 ? list->font->line_h + 2 : 18;
    list->row = row_text;
    list->inset = (ivec2_t){4, 1};
    list->value = -1;
    list_item = net_menu.numitems - 1;
    menuitem_t *bar = add(MI_SCROLLBAR, 0, (irect_t){x + 322, y + 40, 12, 180}, "");
    bar->link = list_item;
    bar->color = 0xffb89040u;
    if (ui.style_list) ui.style_list(&items[list_item], bar);
    button(ID_REFRESH, (irect_t){x + 16, y + 236, 106, 28}, word(NETTEXT_REFRESH), SDLK_r);
    button(ID_DIRECT, (irect_t){x + 130, y + 236, 106, 28}, word(NETTEXT_ADDRESS), SDLK_a);
    button(ID_JOIN, (irect_t){x + 48, y + 318, 106, 28}, word(NETTEXT_JOIN), SDLK_j);
    start_item = net_menu.numitems - 1;
    items[start_item].enabled = false;
    button(ID_CANCEL, (irect_t){x + 198, y + 318, 106, 28}, word(NETTEXT_CANCEL), SDLK_ESCAPE);
    status_item = net_menu.numitems;
    label((irect_t){x + 16, y + 272, BOX_W - 32, 36}, "", MALIGN_LEFT);
    items[status_item].prose = status;
}

static void build_connect(void) {
    int x = box_x(), y = box_y();
    label((irect_t){x, y + 8, BOX_W, 24}, word(NETTEXT_ADDRESS), MALIGN_CENTER);
    field(ID_ADDRESS_FIELD, (irect_t){x + 32, y + 100, BOX_W - 64, 24}, address);
    net_menu.itemOn = net_menu.numitems - 1;
    status_item = net_menu.numitems;
    label((irect_t){x + 16, y + 150, BOX_W - 32, 60}, "", MALIGN_LEFT);
    items[status_item].prose = status;
    button(ID_CONNECT, (irect_t){x + 48, y + 318, 106, 28}, word(NETTEXT_CONNECT), SDLK_RETURN);
    button(ID_CANCEL, (irect_t){x + 198, y + 318, 106, 28}, word(NETTEXT_CANCEL), SDLK_ESCAPE);
}

/* The page Warcraft asked to open is the root: Escape leaves the lobby. */
static bool net_root(void) {
    return page == PAGE_MAIN || (ui.first == 1 && page == PAGE_HOST) ||
           (ui.first == 2 && page == PAGE_BROWSE);
}

static void escape(menu_t *menu) {
    (void)menu;
    if (net_root()) {
        M_NetStop();
        if (ui.back) ui.back(net_app);
        else M_ClearMenus();
        return;
    }
    M_NetStop();
    status[0] = '\0';
    open_page(PAGE_MAIN);
}

static void open_page(int next) {
    page = next;
    net_menu.dropdown = NULL;
    memset(items, 0, sizeof(items));
    net_menu.numitems = 0;
    net_menu.itemOn = -1;
    net_menu.held = NULL;
    net_menu.editing = NULL;
    net_menu.escape = escape;
    net_menu.drawitem = ui.drawitem;
    net_menu.background = ui.background && ui.background->numlumps ? ui.background : NULL;
    net_menu.palette = net_menu.background ? ui.palette ? ui.palette : ui.background->source_palette : NULL;
    status_item = list_item = log_item = chat_item = start_item = players_item = ready_item = -1;
    for (int i = 0; i < MAXPLAYERS; ++i) name_item[i] = race_item[i] = -1;
    int x = box_x(), y = box_y();
    if (ui.panel && ui.panel->numlumps) {
        menuitem_t *panel = add(MI_STATIC, 0, (irect_t){x, y, BOX_W, BOX_H}, "");
        panel->sheet = ui.panel;
        panel->opaque = true;
        panel->stretch = true;
        for (int s = 0; s < MS_STATES; ++s) panel->look[s].cell = 0;
    } else {
        menuitem_t *box = add(MI_STATIC, 0, (irect_t){x, y, BOX_W, BOX_H}, "");
        box->fill = 0xff0c1216u;
        box->border = 0xffdce6dcu;
    }
    switch (page) {
    case PAGE_MAIN: build_main(); break;
    case PAGE_HOST: build_host(); break;
    case PAGE_LOBBY: build_lobby(); break;
    case PAGE_BROWSE: build_browse(); break;
    case PAGE_CONNECT: build_connect(); break;
    }
    if (status[0] && status_item >= 0) items[status_item].prose = status;
    net_menu.app = net_app;
    M_SetupNextMenu(&net_menu);
}

static void send_chat(void) {
    menuitem_t *field_item = &items[chat_item];
    if (!field_item->text[0]) return;
    if (M_NetChat(field_item->text)) field_item->text[0] = '\0';
    else set_status("Chat is busy; try again");
}

static void join(const char *where) {
    snprintf(address, sizeof(address), "%s", where);
    status[0] = '\0';
    if (!M_NetJoinAddress(address)) { failure(); return; }
    open_page(PAGE_CONNECT);
    set_status("Connecting... Escape cancels");
}

static void routine(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (item->id == ID_LIST && action == MA_CHANGE) {
        if (page == PAGE_HOST) {
            selected_map = item->value;
            take_map_players();
            if (players_item >= 0) {
                menuitem_t *choice = &items[players_item];
                int cap = player_cap();
                if (players > cap) players = cap;
                if (players < 2) players = 2;
                choice->rows = cap > 2 ? cap - 1 : 1;
                choice->value = players - 2;
                if (choice->value < 0 || choice->value >= choice->rows) choice->value = 0;
                players = choice->value + 2;
                players_text(choice->text, sizeof(choice->text));
            }
            items[start_item].enabled = selected_map >= 0;
        } else if (page == PAGE_BROWSE) {
            selected_game = item->value;
            items[start_item].enabled = selected_game >= 0;
        }
        return;
    }
    if (action == MA_CHANGE && item->id == ID_PLAYERS) {
        int cap = player_cap();
        int next = item->value + 2;
        if (next < 2) next = 2;
        if (next > cap) next = cap;
        players = next;
        item->value = players - 2;
        players_text(item->text, sizeof(item->text));
        return;
    }
    if (action == MA_CHANGE && item->id >= ID_RACE && item->id < ID_RACE + MAXPLAYERS) {
        assign_race(item->id - ID_RACE, item->value);
        if (page == PAGE_LOBBY) paint_lobby();
        return;
    }
    if (action != MA_ACTIVATE) return;
    switch (item->id) {
    case ID_CREATE:
        status[0] = '\0';
        take_map_players();
        open_page(PAGE_HOST);
        break;
    case ID_JOIN:
        if (page == PAGE_BROWSE) {
            status[0] = '\0';
            if (!M_NetJoinListed(selected_game)) { if (status[0]) failure(); break; }
            open_page(PAGE_CONNECT);
            set_status("Connecting... Escape cancels");
            break;
        }
        status[0] = '\0';
        open_page(PAGE_BROWSE);
        selected_game = -1;
        if (!M_NetBrowse()) failure();
        break;
    case ID_PREVIOUS:
    case ID_CANCEL:
        escape(&net_menu);
        break;
    case ID_START:
        if (page == PAGE_HOST) {
            int count = ui.map_count ? ui.map_count() : 0;
            if (selected_map < 0 || selected_map >= count || !ui.map_path) break;
            const char *path = ui.map_path(selected_map);
            const char *title = ui.map_title ? ui.map_title(selected_map) : path;
            status[0] = '\0';
            stock_prepare_seats();
            if (!M_NetHost(title, path, players)) { failure(); break; }
            open_page(PAGE_LOBBY);
        } else if (page == PAGE_LOBBY && hosting) {
            if (!I_LaunchNetGame()) set_status("Waiting for every player to join");
        }
        break;
    case ID_REFRESH:
        status[0] = '\0';
        selected_game = -1;
        items[list_item].first_row = 0;
        I_QueryNetGames(NULL);
        break;
    case ID_DIRECT:
        M_NetStop();
        status[0] = '\0';
        open_page(PAGE_CONNECT);
        break;
    case ID_CONNECT:
    case ID_ADDRESS_FIELD:
        for (int i = 0; i < net_menu.numitems; ++i)
            if (items[i].id == ID_ADDRESS_FIELD) {
                char where[128];
                snprintf(where, sizeof(where), "%s", items[i].text);
                join(where);
                break;
            }
        break;
    case ID_CHAT:
        send_chat();
        break;
    case ID_READY:
        M_NetToggleReady();
        if (page == PAGE_LOBBY) paint_lobby();
        break;
    default:
        break;
    }
}

static void ticker(menu_t *menu) {
    M_MenuTicker(menu);
    if (neterror[0] && (browsing || page == PAGE_BROWSE)) { failure(); return; }
    if (waiting) {
        int result = M_NetPoll();
        if (result < 0) { failure(); return; }
        if (result > 0) { M_ClearMenus(); return; }
        if (page == PAGE_CONNECT && M_NetInLobby()) open_page(PAGE_LOBBY);
        else if (page == PAGE_LOBBY) {
            paint_lobby();
            if (hosting) {
                bool full = doomcom && I_NetPlayerCount() == doomcom->numplayers;
                bool armed = full;
                if (race_count() > 0)
                    for (int i = 0; armed && i < doomcom->numplayers; ++i) armed = seat[i].ready;
                if (!full) set_status("Waiting for players: %d/%d", I_NetPlayerCount(), doomcom->numplayers);
                else if (race_count() > 0 && !armed)
                    set_status("All %d players joined.\nThe game starts when everyone has.", doomcom->numplayers);
                else if (race_count() > 0) set_status("Starting the game.");
                else set_status("All %d players joined.\nStart the game when ready.", doomcom->numplayers);
                if (start_item >= 0) items[start_item].enabled = armed;
            } else {
                set_status(race_count() > 0 ? "Choose your race, then start.\nThe game starts when everyone has." :
                           "Joined %s\nWaiting for the host to start", I_NetMap());
            }
            if (I_NetChatCount() != last_chat) refresh_log();
        }
    }
    if (page == PAGE_BROWSE) {
        int count;
        const netgame_t *games = I_NetGames(&count);
        if (count > 16) count = 16;
        for (int i = 0; i < count; ++i)
            snprintf(rows[i], sizeof(rows[i]), "%s - %s (%d/%d)", games[i].name, games[i].map,
                     games[i].players, games[i].capacity);
        menuitem_t *list = &items[list_item];
        M_MenuSetRows(list, count);
        if (selected_game >= count) { selected_game = -1; list->value = -1; }
        items[start_item].enabled = selected_game >= 0;
        if (!count) set_status("Searching the LAN...");
        else status[0] = '\0';
    }
}

void M_NetOpen(app_t *app, const netui_t *style) {
    ui = *style;
    int races = style->race_name && style->race_count >= 2 && style->race_count <= 8 ? style->race_count : 0;
    netplay_t net = {.max_players = style->max_players, .race_count = races,
                     .joiner_fields = races ? NET_FIELD_RACE : 0, .commit = style->commit};
    M_NetUse(&net);
    net_app = app;
    net_menu.ticker = ticker;
    selected_map = ui.map_count && ui.map_count() > 0 ? 0 : -1;
    selected_game = -1;
    if (players > max_players()) players = max_players();
    take_map_players();
    if (ui.first == 1) open_page(PAGE_HOST);
    else if (ui.first == 2) {
        open_page(PAGE_BROWSE);
        if (!M_NetBrowse()) failure();
    } else open_page(PAGE_MAIN);
}

/* ── the fallback front end's Multiplayer button ────────────────────────── */

static menu_t *back_menu;

static int default_count(void) { return 1; }
static const char *default_path(int index) { (void)index; return g_game_default_map; }
static const char *default_title(int index) { (void)index; return g_game_default_map; }
static void default_back(app_t *app) {
    if (back_menu) {
        back_menu->app = app;
        M_SetupNextMenu(back_menu);
    } else M_ClearMenus();
}

int M_NetPlayerRace(int player) {
    if (!launch_races || player < 0 || player >= MAXPLAYERS) return -1;
    return launch_race[player];
}

void M_MenuMultiplayer(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action != MA_ACTIVATE) return;
    if (level.width) { M_StartMessage("Leave the current game first."); return; }
    back_menu = menu;
    netui_t style = {.back = default_back, .map_count = default_count, .map_path = default_path,
                     .map_title = default_title, .max_players = 4};
    M_NetOpen(menu->app, &style);
}
