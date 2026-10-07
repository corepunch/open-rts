#include "engine.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The multiplayer screens every game shares, lifted from Dark Colony's LAN
 * lobby: create a game (map, players), browse the LAN or type an address to
 * join, wait in a lobby with chat, then launch. With races, each player owns
 * their seat's race and a ready flag, the same split Dark Colony uses for its
 * own lobby. Sessions run on the I_* menu transport; the poll in the ticker
 * moves a joiner or host into the level. */

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
static bool waiting, hosting;
static char address[128] = "127.0.0.1", status[256], launch_map[512], log_text[NETCHAT_LENGTH * 33 + 8];
static int status_item = -1, list_item = -1, log_item = -1, chat_item = -1, start_item = -1,
           players_item = -1, last_chat;
static char rows[16][128];
/* Lobby seats. A race of -1 in launch_race means this lobby had no races. */
/* The published setup is eight race bytes, then eight ready flags. */
_Static_assert(MAXPLAYERS == 8, "lobby setup is eight races and eight ready flags");
static int race[MAXPLAYERS], launch_race[MAXPLAYERS];
static bool ready[MAXPLAYERS], launch_races, picked, choice_dirty;
static int name_item[MAXPLAYERS], race_item[MAXPLAYERS], ready_item = -1;

static const char *word(int id) { return ui.text[id] ? ui.text[id] : defaults[id]; }
static int max_players(void) {
    return ui.max_players >= 2 && ui.max_players <= MAXPLAYERS ? ui.max_players : 4;
}
static int race_count(void) {
    return ui.race_count >= 2 && ui.race_count <= 8 && ui.race_name ? ui.race_count : 0;
}
static int local_slot(void) {
    return hosting || !doomcom ? 0 : doomcom->consoleplayer;
}

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
static void players_text(char *out, size_t size) {
    const char *name = word(NETTEXT_PLAYERS);
    size_t length = strlen(name);
    snprintf(out, size, length && name[length - 1] == ':' ? "%s %d" : "%s: %d", name, players);
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

static void leave_session(void) {
    if (waiting || I_NetMenuSession() || page == PAGE_BROWSE) I_CancelNetGame();
    waiting = hosting = false;
    SDL_StopTextInput();
}

static void failure(void) {
    char reason[256];
    snprintf(reason, sizeof(reason), "%s", neterror[0] ? neterror : "The connection failed");
    neterror[0] = '\0';
    leave_session();
    open_page(PAGE_MAIN);
    set_status("%s", reason);
}

static void begin_level(void) {
    launch_races = race_count() > 0 && doomcom;
    for (int i = 0; i < MAXPLAYERS; ++i)
        launch_race[i] = launch_races && i < doomcom->numplayers ? race[i] : -1;
    if (ui.commit) ui.commit();
    waiting = hosting = false;
    menumap = launch_map;
    M_ClearMenus();
    SDL_StopTextInput();
}

/* Sixteen bytes: race per seat, then that seat's ready flag. 0xff is unused. */
static void publish(void) {
    if (!hosting || !doomcom || race_count() <= 0) return;
    uint8_t setup[MAXPLAYERS * 2];
    memset(setup, 0xff, MAXPLAYERS);
    memset(setup + MAXPLAYERS, 0, MAXPLAYERS);
    for (int i = 0; i < doomcom->numplayers && i < MAXPLAYERS; ++i) {
        setup[i] = (uint8_t)race[i];
        setup[MAXPLAYERS + i] = ready[i] ? 1 : 0;
    }
    I_SetNetSetup(setup, sizeof(setup));
}

static void send_choice(void) {
    if (hosting || race_count() <= 0) return;
    int me = local_slot();
    if (me < 0 || me >= MAXPLAYERS) return;
    uint8_t choice[2] = {(uint8_t)race[me], ready[me] ? 1 : 0};
    if (I_SetNetChoice(choice, sizeof(choice))) choice_dirty = false;
}

static void enter_lobby(void) {
    int n = race_count();
    picked = false;
    choice_dirty = true;
    for (int i = 0; i < MAXPLAYERS; ++i) {
        race[i] = n > 0 ? i % n : 0;
        ready[i] = false;
    }
    if (hosting && n > 0 && ui.slot_race && selected_map >= 0 && doomcom)
        for (int i = 0; i < doomcom->numplayers && i < MAXPLAYERS; ++i) {
            int authored = ui.slot_race(selected_map, i);
            if (authored >= 0 && authored < n) race[i] = authored;
        }
    if (hosting) publish();
    open_page(PAGE_LOBBY);
}

static void sync_lobby(void) {
    if (race_count() <= 0 || !doomcom) return;
    int n = race_count();
    if (hosting) {
        bool changed = false;
        for (int i = 1; i < doomcom->numplayers && i < MAXPLAYERS; ++i) {
            uint8_t choice[2];
            if (i < I_NetPlayerCount() && I_NetChoice(i, choice, 2) == 2 && choice[0] < n && choice[1] <= 1) {
                changed |= race[i] != choice[0] || ready[i] != (choice[1] != 0);
                race[i] = choice[0];
                ready[i] = choice[1] != 0;
            } else if (i >= I_NetPlayerCount() && ready[i]) {
                ready[i] = false;
                changed = true;
            }
        }
        if (changed) publish();
    } else {
        uint8_t setup[MAXPLAYERS * 2];
        if (I_NetSetup(setup, sizeof(setup)) == sizeof(setup)) {
            int me = local_slot();
            for (int i = 0; i < doomcom->numplayers && i < MAXPLAYERS; ++i) {
                if (i == me) {
                    if (!picked && setup[i] < n && race[i] != setup[i]) {
                        race[i] = setup[i];
                        choice_dirty = true;
                    }
                    continue;
                }
                if (setup[i] < n) race[i] = setup[i];
                ready[i] = setup[MAXPLAYERS + i] == 1;
            }
        }
        if (choice_dirty) send_choice();
    }
    if (page == PAGE_LOBBY) paint_lobby();
}

static void refresh_log(void) {
    log_text[0] = '\0';
    int count = I_NetChatCount(), lines = 0;
    for (int id = count > 32 ? count - 31 : 1; id <= count; ++id) {
        const char *line = I_NetChatLine(id);
        if (!line) continue;
        size_t used = strlen(log_text);
        snprintf(log_text + used, sizeof(log_text) - used, "%s\n", line);
        ++lines;
    }
    if (log_item >= 0) {
        menuitem_t *item = &items[log_item];
        const bitmapfont_t *font = item->font;
        int per = font && font->line_h > 0 ? item->rect.h / font->line_h : 8;
        item->prose = log_text;
        item->first_row = lines > per ? lines - per : 0;
    }
    last_chat = count;
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
    button(ID_PLAYERS, (irect_t){x + 64, y + 236, 224, 28}, "", SDLK_p);
    players_item = net_menu.numitems - 1;
    players_text(items[players_item].text, sizeof(items[players_item].text));
    button(ID_START, (irect_t){x + 48, y + 318, 106, 28}, word(NETTEXT_CREATE), SDLK_c);
    start_item = net_menu.numitems - 1;
    items[start_item].enabled = selected_map >= 0 && selected_map < count;
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
            snprintf(item->text, sizeof(item->text), "%s", race_label(race[i]));
            bool mine = i == local_slot() && i < I_NetPlayerCount();
            item->enabled = item->visible = mine || i < I_NetPlayerCount();
            if (!mine) item->enabled = false;
            if (mine && ready[i]) item->enabled = false;
        }
    }
    if (ready_item >= 0)
        snprintf(items[ready_item].text, sizeof(items[0].text), "%s",
                 ready[local_slot()] ? word(NETTEXT_UNREADY) : word(NETTEXT_READY));
    if (start_item >= 0) {
        bool full = doomcom && I_NetPlayerCount() == doomcom->numplayers;
        bool armed = full;
        if (race_count() > 0)
            for (int i = 0; armed && doomcom && i < doomcom->numplayers; ++i) armed = ready[i];
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
            button(ID_RACE + i, (irect_t){x + 164, y + rows_y + i * row_h, 168, row_h - 2},
                   race_label(race[i]), 0);
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
    ready_item = -1;
    if (race_count() > 0) {
        button(ID_READY, (irect_t){x + 16, y + buttons_y, 106, 28}, word(NETTEXT_READY), SDLK_r);
        ready_item = net_menu.numitems - 1;
    }
    if (hosting) {
        int start_x = race_count() > 0 ? x + 130 : x + 48;
        button(ID_START, (irect_t){start_x, y + buttons_y, 106, 28}, word(NETTEXT_START), SDLK_RETURN);
        start_item = net_menu.numitems - 1;
        items[start_item].enabled = false;
        button(ID_CANCEL, (irect_t){x + 244, y + buttons_y, 96, 28}, word(NETTEXT_CANCEL), SDLK_ESCAPE);
    } else if (race_count() > 0) {
        start_item = -1;
        button(ID_CANCEL, (irect_t){x + 230, y + buttons_y, 106, 28}, word(NETTEXT_CANCEL), SDLK_ESCAPE);
    } else {
        start_item = -1;
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
        if (ui.back) ui.back(net_app);
        else M_ClearMenus();
        return;
    }
    leave_session();
    status[0] = '\0';
    open_page(PAGE_MAIN);
}

static void open_page(int next) {
    page = next;
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
    char line[NETCHAT_LENGTH];
    menuitem_t *field_item = &items[chat_item];
    if (!field_item->text[0]) return;
    snprintf(line, sizeof(line), "%s: %s", hosting ? "Host" : M_va("Player %d", doomcom->consoleplayer + 1),
             field_item->text);
    if (I_SendNetChat(line)) field_item->text[0] = '\0';
    else set_status("Chat is busy; try again");
}

static void join(const char *where) {
    snprintf(address, sizeof(address), "%s", where);
    status[0] = '\0';
    if (!I_JoinNetGame(g_game_id, address)) { failure(); return; }
    waiting = true;
    hosting = false;
    open_page(PAGE_CONNECT);
    set_status("Connecting... Escape cancels");
}

static void routine(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (item->id == ID_LIST && action == MA_CHANGE) {
        if (page == PAGE_HOST) {
            selected_map = item->value;
            take_map_players();
            if (players_item >= 0) players_text(items[players_item].text, sizeof(items[players_item].text));
            items[start_item].enabled = selected_map >= 0;
        } else if (page == PAGE_BROWSE) {
            selected_game = item->value;
            items[start_item].enabled = selected_game >= 0;
        }
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
            int count;
            const netgame_t *games = I_NetGames(&count);
            if (selected_game >= 0 && selected_game < count && selected_game < 16) {
                char where[64];
                snprintf(where, sizeof(where), "%s", games[selected_game].address);
                I_CancelNetGame();
                join(where);
            }
            break;
        }
        status[0] = '\0';
        open_page(PAGE_BROWSE);
        selected_game = -1;
        if (!I_OpenNetBrowser(g_game_id)) failure();
        break;
    case ID_PREVIOUS:
    case ID_CANCEL:
        escape(&net_menu);
        break;
    case ID_PLAYERS:
        players = players >= player_cap() ? 2 : players + 1;
        players_text(item->text, sizeof(item->text));
        break;
    case ID_START:
        if (page == PAGE_HOST) {
            int count = ui.map_count ? ui.map_count() : 0;
            if (selected_map < 0 || selected_map >= count) break;
            const char *path = ui.map_path(selected_map);
            const char *title = ui.map_title ? ui.map_title(selected_map) : path;
            snprintf(launch_map, sizeof(launch_map), "%s", path);
            status[0] = '\0';
            if (!I_HostNetGame(g_game_id, title ? title : path, launch_map, players)) { failure(); break; }
            waiting = hosting = true;
            enter_lobby();
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
        I_CancelNetGame();
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
    case ID_READY: {
        int me = local_slot();
        if (me < 0 || me >= MAXPLAYERS) break;
        ready[me] = !ready[me];
        picked = true;
        if (hosting) publish();
        else choice_dirty = true;
        if (page == PAGE_LOBBY) paint_lobby();
        break;
    }
    default:
        if (item->id >= ID_RACE && item->id < ID_RACE + MAXPLAYERS && race_count() > 0) {
            int slot = item->id - ID_RACE;
            if (slot != local_slot() || ready[slot]) break;
            race[slot] = (race[slot] + 1) % race_count();
            picked = true;
            if (hosting) publish();
            else choice_dirty = true;
            paint_lobby();
        }
        break;
    }
}

static void ticker(menu_t *menu) {
    M_MenuTicker(menu);
    if (neterror[0] && (waiting || page == PAGE_BROWSE)) { failure(); return; }
    if (waiting) {
        int result = I_PollNetGame(launch_map, sizeof(launch_map));
        if (result < 0) { failure(); return; }
        if (result > 0) { begin_level(); return; }
        if (page == PAGE_CONNECT && I_NetLobby()) {
            enter_lobby();
        } else if (page == PAGE_LOBBY) {
            sync_lobby();
            if (hosting) {
                bool full = I_NetPlayerCount() == doomcom->numplayers;
                bool armed = full;
                if (race_count() > 0)
                    for (int i = 0; armed && i < doomcom->numplayers; ++i) armed = ready[i];
                if (!full) set_status("Waiting for players: %d/%d", I_NetPlayerCount(), doomcom->numplayers);
                else if (race_count() > 0 && !armed)
                    set_status("All %d players joined.\nEach player readies, then start.", doomcom->numplayers);
                else set_status("All %d players joined.\nStart the game when ready.", doomcom->numplayers);
                if (start_item >= 0) items[start_item].enabled = armed;
            } else {
                set_status(race_count() > 0 ? "Choose your race, then ready." :
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
    net_app = app;
    net_menu.ticker = ticker;
    status[0] = '\0';
    selected_map = ui.map_count && ui.map_count() > 0 ? 0 : -1;
    selected_game = -1;
    waiting = hosting = false;
    launch_races = false;
    if (players > max_players()) players = max_players();
    take_map_players();
    if (ui.first == 1) open_page(PAGE_HOST);
    else if (ui.first == 2) {
        open_page(PAGE_BROWSE);
        if (!I_OpenNetBrowser(g_game_id)) failure();
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
