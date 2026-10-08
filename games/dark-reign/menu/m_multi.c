#define _DEFAULT_SOURCE
#include "engine.h"
#include "dark-reign.h"
#include "dr_menu.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The multiplayer shell is a second toolkit (dkreign.exe 0x511cd0): panels
 * over graphics/INTFACE/MULTMENU bitmaps, nine PCX fonts and captions named
 * <widget>StaticTitle/ButtonTitle in MLSTRING.CFG. Instant action opens the
 * game-setup ("Chat", 0x516710) panel directly over MM_IA.BMP. Layouts:
 * docs/DR_EXE_FINDINGS.md, Native shell. */

typedef enum { MPMAIN, MPLAN, MPMANUAL, MPCHAT } mppage_t;
typedef enum { POP_NONE, POP_ERROR, POP_MAP } mppopup_t;

/* Font slots of 0x5120db. */
enum { F16BLUE, F14BLUE, F14BLUEO, F14BLUEG, F12GOLD, F12TEAM, F12BLUEN, F12BLUEO, F12BLUEG, NUMFONTS };
/* Control bitmaps. */
enum { ART_BUTTON, ART_LAUNCH, ART_DROP, ART_DROP2, ART_COLOUR, ART_POPERROR, ART_POPMAP, ART_LIGHTS, NUMART };

enum {
    ID_INTERNET = 1, ID_IPX, ID_MODEM, ID_SERIAL, ID_MANUAL, ID_MAINMENU, ID_NAME,
    ID_GAMES, ID_CREATE, ID_JOIN, ID_BACK, ID_ADDRESS,
    ID_SELECTMAP, ID_FOG, ID_UNITS, ID_PLACEMENT, ID_DISPLAY, ID_GIVE, ID_VIEW, ID_TOGRAN,
    ID_HANDICAPS, ID_ALLIANCES, ID_CREDITS, ID_GAMENAME, ID_LAUNCH, ID_KICK, ID_LOCK,
    ID_POPOK, ID_MAPLIST, ID_MAPOK, ID_MAPCANCEL, ID_MESSAGE,
    ID_COLOUR = 40,           /* nine: the default and colours 0..7 */
    ID_ROWTYPE = 50, ID_ROWSIDE = 60, ID_ROWTEAM = 70, ID_ROWHANDICAP = 80,
};

typedef struct {
    char name[32], path[256], terrain[16];
    int players, w, h;
} mpmap_t;

static bool active, instant, hosting, joined, waiting;
static mppage_t page, origin;
static mppopup_t popup;
static char popup_title[96], popup_text[256];
static char root[1024], mapname[512];
static char playername[16] = "Player", gamename[24], address[64] = "127.0.0.1", credits[9];
static bitmapfont_t fonts[NUMFONTS];
static spritesheet_t background, art[NUMART];
static mpmap_t *maps;
static int nummaps, selectedmap = -1, popupmap = -1, selectedgame = -1, shownplayers = -1;
static dr_skirmish_t setup;
/* Values of the option dropdowns, as their native variables 0x671be0.. */
static int fog, units, placement, display, give, view, handicaps, alliances, colour;
static int handicap[8];
/* Lobby: each player's ready check. Handicap stays on this screen. */
static bool ready[8];
static bool dr_bad, locked_choice;
static int shownchat = -1;
static uint8_t shown_lobby[56];
static char message[NETCHAT_LENGTH - 24];
static void (*shell_escape)(menu_t *);

static const char *caption(const char *key, const char *fallback) {
    const char *value = DR_String(key);
    return !strcmp(value, key) ? fallback : value;
}

/* ── assets ─────────────────────────────────────────────────────────────── */

static bool load_bmp(const char *name, spritesheet_t *out) {
    char path[1024];
    M_PathJoin(path, sizeof(path), root, M_va("graphics/INTFACE/MULTMENU/%s", name));
    return W_LoadIndexedSheet(path, out);
}

/* PCX strips: a marker row of separator pixels above the glyphs. */
static bool load_font(const char *name, bitmapfont_t *out) {
    spritesheet_t strip;
    if (!load_bmp(name, &strip)) return false;
    bool ok = DR_StripFont(&strip, 1, out);
    R_FreeSprite(&strip);
    return ok;
}

static void free_assets(void) {
    for (int i = 0; i < NUMFONTS; ++i) HU_FreeFont(&fonts[i]);
    for (int i = 0; i < NUMART; ++i) R_FreeSprite(&art[i]);
    R_FreeSprite(&background);
    free(maps);
    maps = NULL;
    nummaps = 0;
}

static int compare_maps(const void *a, const void *b) {
    const mpmap_t *l = a, *r = b;
    return l->players != r->players ? l->players - r->players : strcasecmp(l->name, r->name);
}

/* Multiplayer maps are scenario/MULTI/<name>/<name>.SCN; a team with
 * SetStartLocation is a player position. */
static bool scan_maps(void) {
    char directory[1024];
    M_PathJoin(directory, sizeof(directory), root, "scenario/MULTI");
    DIR *dir = opendir(directory);
    if (!dir) return false;
    struct dirent *entry;
    bool ok = true;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.' || !strcasecmp(entry->d_name, "DEFAULT")) continue;
        mpmap_t map = {0};
        char path[1024], line[256];
        snprintf(map.name, sizeof(map.name), "%s", entry->d_name);
        snprintf(map.path, sizeof(map.path), "scenario/MULTI/%s/%s.SCN", entry->d_name, entry->d_name);
        M_PathJoin(path, sizeof(path), root, map.path);
        FILE *file = fopen(path, "r");
        if (!file) continue;
        while (fgets(line, sizeof(line), file)) {
            char *p = line;
            while (isspace((unsigned char)*p)) ++p;
            if (!strncmp(p, "SetStartLocation(", 17)) ++map.players;
            sscanf(p, "SetDefaultTerrain(%15[A-Za-z]", map.terrain);
        }
        fclose(file);
        M_PathJoin(path, sizeof(path), root, M_va("scenario/MULTI/%s/%s.MAP", entry->d_name, entry->d_name));
        blob_t header;
        if (W_ReadFile(path, &header)) {
            if (header.size >= 16 && !memcmp(header.bytes, "MAP_", 4)) {
                map.w = read_i32_le(header.bytes + 8);
                map.h = read_i32_le(header.bytes + 12);
            }
            W_FreeFile(&header);
        }
        if (map.players < 2 || map.players > 8) continue;
        mpmap_t *grown = realloc(maps, (size_t)(nummaps + 1) * sizeof(*maps));
        if (!grown) { ok = false; break; }
        maps = grown;
        maps[nummaps++] = map;
    }
    closedir(dir);
    if (nummaps) qsort(maps, nummaps, sizeof(*maps), compare_maps);
    return ok && nummaps > 0;
}

/* ── setup ──────────────────────────────────────────────────────────────── */

static const char *const type_keys[] = {"ChatPlayerTypeAvailable", "ChatPlayerTypeHuman",
    "ChatPlayerTypeComputer1", "ChatPlayerTypeComputer2", "ChatPlayerTypeComputer3", "ChatPlayerTypeClosed"};
static const char *const type_names[] = {"Available", "Human", "Computer (Easy)", "Computer (Medium)",
    "Computer (Hard)", "Closed"};
static const char *const side_keys[] = {"ChatPlayerSideDefault", "ChatPlayerSideFreedomGuard",
    "ChatPlayerSideImperium"};
static const char *const side_names[] = {"Default", "Freedom Guard", "Imperium"};

/* Retail DROP.BMP / DROP2.BMP choices. The engine draws the open list. */
typedef struct { const char *key, *fallback; } caption_t;
static const caption_t give_rows[] = {{"ChatGiveMoneyOff", "No Giving"}, {"ChatGiveMoneyOn", "Give Units/Money"}};
static const caption_t view_rows[] = {{"ChatViewResourcesOff", "Hide Allied Resources"},
                                      {"ChatViewResourcesOn", "View Allied Resources"}};
static const caption_t hcap_rows[] = {{"ChatHandicapsOff", "Handicaps OFF"}, {"ChatHandicapsOn", "Handicaps ON"}};
static const caption_t ally_rows[] = {{"ChatAlliancesOff", "Teams"}, {"ChatAlliancesOn", "Alliances"}};
static const caption_t fog_rows[] = {{"ChatFogStyleOnOn", "Fog On, Shroud On"}, {"ChatFogStyleOffOn", "Fog Off, Shroud On"},
                                    {"ChatFogStyleOnOff", "Fog On, Shroud Off"}, {"ChatFogStyleOffOff", "Fog Off, Shroud Off"}};
static const caption_t unit_rows[] = {{"ChatStartingUnitsDefault", "Default Units"},
                                     {"ChatStartingUnitsThreeRigs", "Three Rigs"}};
static const caption_t place_rows[] = {{"ChatRandomLocationsButtonTitle", "Random Placement"},
                                      {"ChatFixedLocations", "Fixed Placement"}};
static const caption_t display_rows[] = {{"ChatDisplayLocationsOff", "Display Off"},
                                        {"ChatDisplayLocationsAvailable", "Show Available"},
                                        {"ChatDisplayLocationsAllies", "Show Allies"},
                                        {"ChatDisplayLocationsAll", "Show All"}};
static const caption_t togran_rows[] = {{"ChatAllowTogranOff", "No Togran"}};

static void default_setup(int players) {
    setup = (dr_skirmish_t){.count = players};
    for (int i = 0; i < 8; ++i) handicap[i] = 0;
    for (int i = 0; i < players; ++i)
        setup.slots[i].type = i == 0 ? DR_SLOT_HUMAN : instant && i == 1 ? DR_SLOT_MEDIUM : DR_SLOT_AVAILABLE;
    snprintf(setup.slots[0].name, sizeof(setup.slots[0].name), "%s", playername);
}

/* In a network game the humans are players 0..n-1 (D_PlayerIsHuman): the
 * host first, then every slot left Available for a LAN player. */
static int order_humans(void) {
    dr_skirmish_t ordered = setup;
    int n = 0, humans = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < setup.count; ++i) {
            bool human = setup.slots[i].type == DR_SLOT_HUMAN || setup.slots[i].type == DR_SLOT_AVAILABLE;
            if (human != (pass == 0)) continue;
            ordered.slots[n] = setup.slots[i];
            if (human) { ordered.slots[n].type = DR_SLOT_HUMAN; ++humans; }
            ++n;
        }
    setup = ordered;
    return humans;
}

/* The engine owns the lobby. A seat's race is the side and its team is the
 * team; the option bytes are credits, fog, starting units and the map's
 * slot count. Handicap is not part of the match. */
static const char *dr_chat_name(void) { return playername; }

static bool push_lobby(void) {
    for (int i = 0; i < 8; ++i) {
        const dr_slot_t *slot = &setup.slots[i];
        netseat_t seat = {.race = slot->side, .type = slot->type, .team = slot->team, .ready = ready[i]};
        if (!M_NetSetSeat(i, &seat)) return false;
    }
    uint8_t opt[8] = {0};
    uint32_t money = (uint32_t)setup.credits;
    for (int i = 0; i < 4; ++i) opt[i] = (uint8_t)(money >> (8 * i));
    opt[4] = (uint8_t)fog;
    opt[5] = (uint8_t)units;
    opt[6] = (uint8_t)setup.count;
    M_NetSetOptions(opt, sizeof(opt));
    return true;
}

static bool pull_lobby(void) {
    uint8_t opt[16];
    if (M_NetOptions(opt, sizeof(opt)) != sizeof(opt)) return true;
    uint32_t money = 0;
    for (int i = 0; i < 4; ++i) money |= (uint32_t)opt[i] << (8 * i);
    if (opt[6] < 2 || opt[6] > 8 || money > 99999999u || opt[4] > 3 || opt[5] > 1) return false;
    char names[8][24];
    netseat_t seats[8];
    for (int i = 0; i < 8; ++i) snprintf(names[i], sizeof(names[i]), "%s", setup.slots[i].name);
    for (int i = 0; i < 8; ++i) {
        const netseat_t *seat = M_NetSeat(i);
        if (!seat || seat->type > DR_SLOT_CLOSED || seat->race > DR_SIDE_IMPERIUM || seat->team > 8) return false;
        seats[i] = *seat;
    }
    dr_skirmish_t shared = {.count = opt[6], .credits = (int)money};
    for (int i = 0; i < 8; ++i) {
        shared.slots[i] = (dr_slot_t){.type = seats[i].type, .side = seats[i].race, .team = seats[i].team};
        snprintf(shared.slots[i].name, sizeof(shared.slots[i].name), "%s", names[i]);
        ready[i] = seats[i].ready;
    }
    setup = shared;
    fog = opt[4];
    units = opt[5];
    if (setup.credits) snprintf(credits, sizeof(credits), "%d", setup.credits);
    else credits[0] = '\0';
    const char *map = I_NetMap();
    if (!map || !map[0]) map = M_NetLaunchMap();
    selectedmap = -1;
    for (int i = 0; map && i < nummaps; ++i)
        if (!strcasecmp(maps[i].path, map)) selectedmap = i;
    return true;
}

static void dr_net_commit(void) {
    dr_bad = !pull_lobby() || M_NetOptions(&(uint8_t){0}, 1) == 0;
    if (!dr_bad) DR_RequestSkirmish(M_NetLaunchMap(), &setup);
}

static void use_net(void) {
    netplay_t net = {.max_players = 8, .race_count = 3,
                     .joiner_fields = NET_FIELD_RACE | NET_FIELD_TEAM,
                     .commit = dr_net_commit, .chat_name = dr_chat_name};
    M_NetUse(&net);
    dr_bad = locked_choice = false;
}

/* ── items ──────────────────────────────────────────────────────────────── */

static void routine(menu_t *screen, menuitem_t *item, menuaction_t action);

static menuitem_t *label(irect_t rect, const char *key, const char *fallback, int font, bool centre) {
    return DR_Text(rect, centre ? MALIGN_HCENTER : 0, caption(key, fallback), &fonts[font], NULL, NULL);
}

static menuitem_t *backdrop(irect_t rect, int sheet) {
    menuitem_t *item = DR_Text(rect, 0, "", NULL, NULL, NULL);
    if (!item) return NULL;
    item->sheet = &art[sheet];
    item->opaque = true;
    item->layer = true;
    for (int s = 0; s < MS_STATES; ++s) item->look[s].cell = 0;
    return item;
}

/* BUTTON 0x588380: normal, hover and pressed crops of one bitmap, step
 * pixels apart; text in f12blue n/o/g. */
static menuitem_t *button(irect_t rect, int id, const char *text, int sheet, int step) {
    menuitem_t *item = DR_Button(rect, id, text, &fonts[F12BLUEN], &fonts[F12BLUEO], &fonts[F12BLUEG]);
    if (!item) return NULL;
    item->routine = routine;
    item->sheet = &art[sheet];
    for (int s = 0; s < MS_STATES; ++s)
        item->look[s] = (menulook_t){.cell = 0, .palette = -1, .part = {step * s, 0, rect.w, rect.h},
                                     .font = item->look[s].font};
    return item;
}

static menuitem_t *bar_button(int x, int y, int id, const char *key, const char *fallback) {
    return button((irect_t){x, y, 110, 32}, id, caption(key, fallback), ART_BUTTON, 110);
}

static const char *copied(const char *text) {
    static char label[64];
    snprintf(label, sizeof(label), "%s", text ? text : "");
    return label;
}

static const char *option_row(const menuitem_t *item, int row) {
    const caption_t *rows = item->userdata;
    if (!rows || row < 0 || row >= item->rows) return "";
    return copied(caption(rows[row].key, rows[row].fallback));
}

static const char *side_row(const menuitem_t *item, int row) {
    (void)item;
    if (row < 0 || row >= 3) return "";
    return copied(caption(side_keys[row], side_names[row]));
}

static const char *team_row(const menuitem_t *item, int row) {
    (void)item;
    if (row <= 0) return copied(caption("ChatPlayerTeamNone", "No Team"));
    if (row > 8) return "";
    char key[32], fallback[16];
    snprintf(key, sizeof(key), "ChatPlayerTeam%c", 'A' + row - 1);
    snprintf(fallback, sizeof(fallback), "Team %c", 'A' + row - 1);
    return copied(caption(key, fallback));
}

static const char *handicap_row(const menuitem_t *item, int row) {
    (void)item;
    static char label[16];
    if (row < 0 || row >= 21) return "";
    snprintf(label, sizeof(label), "%d.%02d", 1 + row / 4, row % 4 * 25);
    return label;
}

/* The closed face is the native DROP bitmap. The engine owns the open list. */
static void use_dropdown(menuitem_t *item, const char *(*row)(const menuitem_t *, int), int count, int value) {
    if (!item || count <= 0) return;
    item->kind = MI_DROPDOWN;
    item->row = row;
    item->rows = count;
    item->value = value >= 0 && value < count ? value : 0;
    item->row_height = item->rect.h > 0 ? item->rect.h : 13;
    item->popup_rows = count > 8 ? 8 : 0;
    item->color = 0xff1a2744u;
    if (row) snprintf(item->text, sizeof(item->text), "%s", row(item, item->value));
}

static menuitem_t *choice(irect_t rect, int id, const caption_t *rows, int count, int value) {
    bool wide = rect.w >= 150;
    menuitem_t *item = button(rect, id, "", wide ? ART_DROP : ART_DROP2, wide ? 156 : 135);
    if (!item) return NULL;
    item->userdata = rows;
    use_dropdown(item, option_row, count, value);
    return item;
}

/* A button with no state art: only its text font changes (normal, then
 * the over and glow fonts that follow it in the slot table). */
static menuitem_t *text_button(irect_t rect, int id, const char *text, int normal) {
    menuitem_t *item = DR_Button(rect, id, text, &fonts[normal], &fonts[normal + 1], &fonts[normal + 2]);
    if (item) item->routine = routine;
    return item;
}

/* Player-row cells use f12gold, brightened while hovered. */
static menuitem_t *cell(irect_t rect, int id, const char *text, bool enabled) {
    menuitem_t *item = DR_Button(rect, id, text, &fonts[F12GOLD], &fonts[F12BLUEO], &fonts[F12BLUEG]);
    if (!item) return NULL;
    item->routine = routine;
    item->enabled = enabled;
    item->align = MALIGN_VCENTER;
    item->rect.x += 2;
    return item;
}

static menuitem_t *field(irect_t rect, int id, const char *text, int maxchars) {
    menuitem_t *item = DR_Text(rect, 0, text, &fonts[F12GOLD], NULL, NULL);
    if (!item) return NULL;
    item->kind = MI_TEXTFIELD;
    item->maxchars = maxchars;
    item->rect.x += 5;
    item->rect.y += 3;
    item->routine = routine;
    item->id = id;
    return item;
}

static const char *map_row(int row) {
    if (row < 0 || row >= nummaps) return "";
    return M_va("%s  (%d)", maps[row].name, maps[row].players);
}

static const char *game_row(int row) {
    int count;
    const netgame_t *games = I_NetGames(&count);
    if (row < 0 || row >= count) return "";
    return M_va("%.32s", games[row].name[0] ? games[row].name : M_FileName(games[row].map));
}

static const char *list_row(const menuitem_t *item, int row) {
    return item->id == ID_MAPLIST ? map_row(row) : game_row(row);
}

static menuitem_t *list(irect_t rect, int id, int rows, int value, int row_height) {
    /* Rows in f12gold; the selected one glows. */
    menuitem_t *item = DR_Text(rect, 0, "", &fonts[F12GOLD], NULL, &fonts[F12BLUEG]);
    if (!item) return NULL;
    item->kind = MI_LIST;
    item->row = list_row;
    item->inset = (ivec2_t){2, 0};
    item->row_height = row_height;
    item->rows = rows;
    item->value = value;
    item->routine = routine;
    item->id = id;
    return item;
}

/* ── screens ────────────────────────────────────────────────────────────── */

static int human_rows(void) {
    int n = 0;
    for (int i = 0; i < setup.count; ++i) n += setup.slots[i].type == DR_SLOT_HUMAN;
    return n;
}

/* Who may edit a row: the host owns the seats and computer players; in a
 * lobby each joiner owns its own side and team (0x5322f0, 0x532420). */
static bool row_editable(int i, bool type) {
    if (joined) return !type && i == doomcom->consoleplayer && !ready[i];
    if (!hosting) return !type || i > 0;
    return !type && (i == 0 ? !ready[0] : setup.slots[i].type != DR_SLOT_HUMAN);
}

static void draw_light(const menu_t *screen, const menuitem_t *item, irect_t rect) {
    (void)screen;
    int i = (item->rect.y - 68) / 13;
    /* LIGHTS.BMP: 0 off, 19 ready. */
    irect_t src = {ready[i] ? 19 : 0, 0, 19, 13}, dst = {rect.x, rect.y, 19, 13};
    R_DrawSprite(&art[ART_LIGHTS], 0, -1, &src, &dst, 0, 16);
}

static void build_rows(void) {
    /* 0x525c10: eight rows from y 68, 13 apart. */
    for (int i = 0; i < 8; ++i) {
        int y = 68 + 13 * i;
        if (i >= setup.count) continue;
        const dr_slot_t *slot = &setup.slots[i];
        bool open = slot->type != DR_SLOT_AVAILABLE && slot->type != DR_SLOT_CLOSED;
        const char *type = slot->type == DR_SLOT_HUMAN && slot->name[0] ? slot->name :
            caption(type_keys[slot->type], type_names[slot->type]);
        int me = joined ? doomcom->consoleplayer : 0;
        if (i == me) type = playername;
        else if ((joined || hosting) && slot->type == DR_SLOT_HUMAN)
            type = i < I_NetPlayerCount() ? M_va("Player %d", i + 1) : caption(type_keys[0], type_names[0]);
        if (hosting || joined) {
            /* ChatRowLaunch: a lobby player's ready light. */
            menuitem_t *light = DR_Text((irect_t){0, y, 19, 13}, 0, "", NULL, NULL, NULL);
            if (light && slot->type == DR_SLOT_HUMAN) light->ownerdraw = draw_light;
        }
        cell((irect_t){19, y, 181, 13}, ID_ROWTYPE + i, type, row_editable(i, true));
        if (!open) continue;
        use_dropdown(cell((irect_t){219, y, 90, 13}, ID_ROWSIDE + i, "", row_editable(i, false)),
                     side_row, 3, slot->side);
        use_dropdown(cell((irect_t){309, y, 60, 13}, ID_ROWTEAM + i, "", row_editable(i, false)),
                     team_row, 9, slot->team);
        use_dropdown(cell((irect_t){369, y, 81, 13}, ID_ROWHANDICAP + i, "", !joined && row_editable(i, false)),
                     handicap_row, 21, handicap[i]);
    }
}

static const char *chat_log(void) {
    static char log[NETCHAT_LENGTH * 7];
    size_t used = 0;
    log[0] = '\0';
    for (int id = I_NetChatCount() - 5; id <= I_NetChatCount(); ++id) {
        const char *line = I_NetChatLine(id);
        if (line && used < sizeof(log)) used += (size_t)snprintf(log + used, sizeof(log) - used, "%s\n", line);
    }
    return log;
}

static void build_chat(void) {
    /* Once a lobby is open the seats, map and options belong to it. */
    bool mp = !instant, locked = joined || hosting;
    label((irect_t){220, 0, 200, 23}, mp ? "ChatStaticTitle" : "ChatStaticTitleIA",
          mp ? "Multi-Player Setup" : "Instant Action Setup", F16BLUE, true);
    label((irect_t){19, 47, 100, 23}, "ChatStaticPlayerStaticTitle", "Player", F14BLUE, false);
    label((irect_t){219, 47, 100, 23}, "ChatStaticSideStaticTitle", "Side", F14BLUE, false);
    label((irect_t){309, 47, 100, 23}, alliances ? "ChatStaticAllianceStaticTitle" : "ChatAlliancesOff",
          alliances ? "Alliances" : "Teams", F14BLUE, false);
    label((irect_t){369, 47, 100, 23}, "ChatStaticHandicapStaticTitle", "Handicap", F14BLUE, false);
    label((irect_t){463, 25, 100, 16}, "ChatGameNameStaticTitle", "GAME NAME", F14BLUE, false);
    label((irect_t){463, 68, 100, 16}, "ChatGameTypeStaticTitle", "GAME TYPE", F14BLUE, false);
    label((irect_t){458, 90, 156, 15}, "ChatGameTypeNormal", "Normal", F12BLUEN, true);
    label((irect_t){463, 109, 100, 16}, "ChatOptionsStaticTitle", "OPTIONS", F14BLUE, false);
    label((irect_t){23, 169, 100, 16}, "ChatRulesStaticTitle", "Rules", F14BLUE, false);
    if (mp) label((irect_t){23, 249, 100, 16}, "ChatMessagesStaticTitle", "Messages", F14BLUE, false);
    label((irect_t){463, 217, 100, 16}, "ChatMapsStaticTitle", "MAPS", F14BLUE, false);
    const mpmap_t *m = selectedmap >= 0 ? &maps[selectedmap] : NULL;
    DR_Text((irect_t){463, 262, 155, 16}, MALIGN_HCENTER, m ? M_va("%s %dx%d %dplr", m->name, m->w, m->h, m->players) :
            mp ? "Unknown MAP" : "None Selected", &fonts[F12GOLD], NULL, NULL);
    label((irect_t){463, 308, 100, 16}, "ChatStartingUnitsStaticTitle", "STARTING UNITS", F14BLUE, false);
    label((irect_t){463, 353, 50, 16}, "ChatCreditsStaticTitle", "Credits", F14BLUE, false);
    if (mp) field((irect_t){459, 47, 155, 15}, ID_GAMENAME, gamename, 20)->enabled = !locked;
    field((irect_t){515, 356, 90, 15}, ID_CREDITS, credits, 8)->enabled = !locked;
    /* Colour strip: column 0 is the default, 1..8 colours 0..7; COLOUR.BMP
     * rows 1/2/3 are normal, hover and pressed. */
    for (int k = 0; k < 9; ++k) {
        menuitem_t *c = button((irect_t){27 + 25 * k, 33, 25, 14}, ID_COLOUR + k, "", ART_COLOUR, 0);
        if (!c) continue;
        c->kind = MI_CHECK;
        c->group = 1;
        c->value = colour == k;
        for (int s = 0; s < MS_STATES; ++s) c->look[s].part = (irect_t){25 * k, 14 * (s + 1), 25, 14};
    }
    text_button((irect_t){458, 237, 155, 25}, ID_SELECTMAP,
                caption("ChatCurrentMapButtonTitle", "Select Map"), F12BLUEN)->enabled = !locked;
    if (mp) {
        menuitem_t *give_item = choice((irect_t){456, 132, 156, 15}, ID_GIVE, give_rows, 2, give);
        menuitem_t *view_item = choice((irect_t){456, 149, 156, 15}, ID_VIEW, view_rows, 2, view);
        /* Togran is not a playable side in this port. */
        menuitem_t *togran = choice((irect_t){456, 166, 156, 15}, ID_TOGRAN, togran_rows, 1, 0);
        menuitem_t *hand = choice((irect_t){456, 183, 156, 15}, ID_HANDICAPS, hcap_rows, 2, handicaps);
        menuitem_t *ally = choice((irect_t){456, 201, 156, 15}, ID_ALLIANCES, ally_rows, 2, alliances);
        if (give_item) give_item->enabled = !locked;
        if (view_item) view_item->enabled = !locked;
        if (togran) togran->enabled = false;
        if (hand) hand->enabled = !locked;
        if (ally) ally->enabled = !locked;
    }
    menuitem_t *fog_item = choice((irect_t){458, 290, 156, 15}, ID_FOG, fog_rows, 4, fog);
    menuitem_t *units_item = choice((irect_t){458, 331, 156, 15}, ID_UNITS, unit_rows, 2, units);
    menuitem_t *place_item = choice((irect_t){319, 191, 135, 16}, ID_PLACEMENT, place_rows, 2, placement);
    menuitem_t *display_item = choice((irect_t){319, 208, 135, 16}, ID_DISPLAY, display_rows, 4, display);
    if (fog_item) fog_item->enabled = !locked;
    if (units_item) units_item->enabled = !locked;
    if (place_item) place_item->enabled = !locked;
    if (display_item) display_item->enabled = !locked;
    build_rows();
    if (mp) {
        bar_button(27, 414, ID_BACK, "ChatBackButtonTitle", "Previous Menu");
        bar_button(506, 414, ID_KICK, "ChatKickButtonTitle", "Kick Player")->enabled = false;
        bar_button(460, 447, ID_LOCK, "ChatLockButtonTitle", "Deny Access")->enabled = false;
    }
    bar_button(mp ? 140 : 27, 414, ID_MAINMENU, "ChatMainButtonTitle", "Main Menu");
    /* LAUNCH opens the lobby, then starts it; a joiner's LAUNCH is its
     * ready check. */
    const char *launch = joined || (hosting && I_NetPlayerCount() < human_rows()) ?
        (ready[joined ? doomcom->consoleplayer : 0] ? "UNREADY" : "READY") : caption("ChatLauchButtonTitle", "LAUNCH");
    button((irect_t){266, 410, 110, 32}, ID_LAUNCH, launch, ART_LAUNCH, 110)->enabled = joined || hosting ||
        selectedmap >= 0 || !mp;
    /* The rules list shows the session state. */
    const char *status = joined ? (ready[doomcom->consoleplayer] ? "Ready: waiting for the host to launch" :
                                   "Choose your side and team, then READY.") :
        hosting ? (I_NetPlayerCount() < human_rows() ?
                   M_va("Waiting for LAN players: %d/%d", I_NetPlayerCount(), human_rows()) :
                   "Everyone has joined. LAUNCH starts when all are ready.") :
        mp ? "Leave slots Available for LAN players, then LAUNCH to open the lobby." : "";
    menuitem_t *rules = DR_Text((irect_t){23, 192, 270, mp ? 55 : 170}, 0, " ", &fonts[F12GOLD], NULL, NULL);
    if (rules) rules->prose = status;
    if (mp && (hosting || joined)) {
        /* ChatMsgList and ChatMessageEntry: the lobby chat. */
        menuitem_t *log = DR_Text((irect_t){21, 273, 274, 81}, 0, " ", &fonts[F12GOLD], NULL, NULL);
        if (log) log->prose = chat_log();
        field((irect_t){108, 359, 206, 19}, ID_MESSAGE, message, (int)sizeof(message) - 1);
    }
}

static void build_popup(void) {
    /* Popups are modal and centred on (320, 220) (0x58ca80). */
    for (int i = 0; i < drscreen.count; ++i) drscreen.items[i].enabled = false;
    if (popup == POP_ERROR) {
        irect_t box = {125, 119, 390, 202};
        backdrop(box, ART_POPERROR);
        DR_Text((irect_t){box.x + 37, box.y + 4, 316, 16}, MALIGN_HCENTER, popup_title, &fonts[F12GOLD], NULL, NULL);
        menuitem_t *desc = DR_Text((irect_t){box.x + 37, box.y + 39, 316, 70}, 0, " ", &fonts[F12GOLD], NULL, NULL);
        if (desc) desc->prose = popup_text;
        text_button((irect_t){box.x + 120, box.y + 132, 138, 44}, ID_POPOK, caption("PopOkButtonTitle", "Ok"), F12BLUEN);
    } else if (popup == POP_MAP) {
        irect_t box = {120, 91, 400, 259};
        const mpmap_t *m = popupmap >= 0 ? &maps[popupmap] : NULL;
        backdrop(box, ART_POPMAP);
        DR_Text((irect_t){box.x + 100, box.y + 2, 200, 24}, MALIGN_HCENTER, "SELECT MAP", &fonts[F16BLUE], NULL, NULL);
        label((irect_t){box.x + 35, box.y + 19, 114, 24}, "ChatSelectMapAvailmapsStaticTitle", "Available Maps", F14BLUE, false);
        label((irect_t){box.x + 266, box.y + 39, 114, 24}, "ChatSelectMapMapSizeStaticTitle", "Map Size", F14BLUE, true);
        label((irect_t){box.x + 266, box.y + 91, 114, 24}, "ChatSelectMapNumPlayersStaticTitle", "Number of Players", F14BLUE, true);
        label((irect_t){box.x + 266, box.y + 143, 114, 24}, "ChatSelectMapMapTypeStaticTitle", "Map Type", F14BLUE, true);
        DR_Text((irect_t){box.x + 266, box.y + 64, 114, 24}, MALIGN_HCENTER, m ? M_va("%dx%d", m->w, m->h) : "-",
                &fonts[F12GOLD], NULL, NULL);
        DR_Text((irect_t){box.x + 266, box.y + 118, 114, 24}, MALIGN_HCENTER, m ? M_va("%d", m->players) : "-",
                &fonts[F12GOLD], NULL, NULL);
        DR_Text((irect_t){box.x + 266, box.y + 169, 114, 24}, MALIGN_HCENTER, m ? m->terrain : "-", &fonts[F12GOLD], NULL, NULL);
        menuitem_t *maplist = list((irect_t){box.x + 28, box.y + 45, 220, 162}, ID_MAPLIST, nummaps, popupmap, 13);
        if (maplist && popupmap >= 162 / 13) maplist->first_row = popupmap - 162 / 13 + 1;
        text_button((irect_t){box.x + 110, box.y + 210, 90, 25}, ID_MAPOK,
                    caption("ChatSelectMapOkButtonTitle", "SELECT MAP"), F12BLUEN)->enabled = m != NULL;
        text_button((irect_t){box.x + 278, box.y + 210, 90, 25}, ID_MAPCANCEL,
                    caption("ChatSelectMapCancelButtonTitle", "CANCEL"), F12BLUEN);
    }
}

static bool build(void) {
    static const char *const backgrounds[] = {"MM_MAIN.BMP", "MM_LAN.BMP", "MM_MANU.BMP", "MM_SETU.BMP"};
    int first_row = 0;
    menuitem_t *games = M_MenuFind(&drscreen.menu, ID_GAMES);
    if (games && page == MPLAN) first_row = games->first_row;
    int focus = drscreen.menu.itemOn;
    DR_ScreenClear();
    R_FreeSprite(&background);
    if (!load_bmp(page == MPCHAT && instant ? "MM_IA.BMP" : backgrounds[page], &background)) return false;
    drscreen.menu.background = &background;
    drscreen.menu.palette = background.source_palette;
    switch (page) {
    case MPMAIN:
        label((irect_t){160, 0, 320, 20}, "MainTitleStaticTitle", "Multi-Player Connection", F16BLUE, true);
        label((irect_t){226, 59, 188, 30}, "MainPlayerNameStaticStaticTitle", "Player Name:", F14BLUE, false);
        field((irect_t){226, 92, 188, 24}, ID_NAME, playername, 15);
        /* ActiveNet, modem and serial links are outside this port. */
        text_button((irect_t){226, 134, 188, 30}, ID_INTERNET, caption("MainActiveNetButtonTitle", "INTERNET"),
                    F14BLUE)->enabled = false;
        text_button((irect_t){226, 186, 188, 30}, ID_IPX, caption("MainIPXButtonTitle", "Local Area Network"), F14BLUE);
        text_button((irect_t){226, 238, 188, 30}, ID_MODEM, caption("MainModemButtonTitle", "Dial-Up MODEM"),
                    F14BLUE)->enabled = false;
        text_button((irect_t){226, 290, 188, 30}, ID_SERIAL, caption("MainSerialButtonTitle", "SERIAL/NULL MODEM"),
                    F14BLUE)->enabled = false;
        text_button((irect_t){226, 342, 188, 30}, ID_MANUAL, caption("MainInternetButtonTitle", "MANUAL IP"), F14BLUE);
        bar_button(266, 410, ID_MAINMENU, "MainButtonTitle", "MAIN MENU");
        break;
    case MPLAN: {
        int count;
        const netgame_t *found = I_NetGames(&count);
        label((irect_t){220, 0, 200, 20}, "IpxTitleStaticTitle", "Local Area Network", F16BLUE, true);
        label((irect_t){34, 81, 100, 20}, "IpxOwnerStaticTitle", "Games found on Network", F14BLUE, false);
        label((irect_t){462, 81, 100, 20}, "IpxPlayersStaticTitle", "Players in Game", F14BLUE, false);
        menuitem_t *l = list((irect_t){39, 105, 358, 261}, ID_GAMES, count, selectedgame, 20);
        if (l) l->first_row = first_row;
        if (selectedgame >= 0 && selectedgame < count)
            DR_Text((irect_t){468, 107, 130, 20}, 0, M_va("%d / %d", found[selectedgame].players,
                    found[selectedgame].capacity), &fonts[F12GOLD], NULL, NULL);
        bar_button(27, 414, ID_BACK, "IpxBackButtonTitle", "Previous Menu");
        bar_button(140, 414, ID_MAINMENU, "IpxMainButtonTitle", "Main Menu");
        bar_button(393, 414, ID_CREATE, "IpxCreateButtonTitle", "Create Game");
        bar_button(506, 414, ID_JOIN, "IpxJoinButtonTitle", "Join Game")->enabled =
            selectedgame >= 0 && selectedgame < count;
        break;
    }
    case MPMANUAL:
        label((irect_t){160, 0, 320, 20}, "InetTitleStaticTitle", "Manual IP", F16BLUE, true);
        label((irect_t){232, 317, 177, 18}, "InetEdIpStaticTitle", "Player's Internet IP", F12GOLD, true);
        field((irect_t){178, 339, 283, 18}, ID_ADDRESS, address, 63);
        bar_button(27, 414, ID_BACK, "InetBackButtonTitle", "Previous Menu");
        bar_button(140, 414, ID_MAINMENU, "InetMainButtonTitle", "Main Menu");
        bar_button(393, 414, ID_CREATE, "InetCreateButtonTitle", "Create Game");
        bar_button(506, 414, ID_JOIN, "InetJoinButtonTitle", "Join Game")->enabled = address[0] != '\0';
        break;
    case MPCHAT:
        build_chat();
        break;
    }
    if (popup) build_popup();
    drscreen.menu.itemOn = focus < drscreen.count ? focus : -1;
    for (int i = 0; i < drscreen.count; ++i)
        if (drscreen.items[i].kind != MI_STATIC && !drscreen.items[i].routine) return false;
    return true;
}

/* ── actions ────────────────────────────────────────────────────────────── */

static void show_error(const char *title, const char *text) {
    snprintf(popup_title, sizeof(popup_title), "%s", title);
    snprintf(popup_text, sizeof(popup_text), "%s", text);
    popup = POP_ERROR;
}

static void network_error(const char *why) {
    if (!why || !why[0]) why = M_NetNotice();
    if (!why || !why[0]) why = neterror[0] ? neterror : "Network error";
    show_error(caption("NetRefusalUnknownStaticTitle", "Warning"), why);
    M_NetStop();
    waiting = joined = hosting = locked_choice = false;
    shownplayers = shownchat = -1;
}

static void leave_session(void) {
    if (waiting || joined || hosting || page == MPLAN) M_NetStop();
    waiting = joined = hosting = locked_choice = false;
    shownplayers = shownchat = -1;
    memset(shown_lobby, 0, sizeof(shown_lobby));
}

static void start_level(const char *map) {
    snprintf(mapname, sizeof(mapname), "%s", map);
    DR_RequestSkirmish(mapname, &setup);
    menumap = mapname;
    M_ClearMenus();
    DR_MultiClose();
}

/* 0x52ca40: a map is required; a network game needs a LAN player. */
static void launch(void) {
    if (selectedmap < 0) {
        show_error(caption("ChatLaunchNoMapStaticTitle", "Warning"), caption("ChatLaunchNoMapDesc", "Select a map first."));
        return;
    }
    setup.credits = atoi(credits);
    if (instant) {
        start_level(maps[selectedmap].path);
        return;
    }
    if (hosting) {
        /* Ready is the host's own check. The engine starts the match. */
        M_NetToggleReady();
        if (M_NetSeat(0)) ready[0] = M_NetSeat(0)->ready;
        return;
    }
    dr_skirmish_t edited = setup;
    int humans = order_humans();
    if (humans < 2) {
        setup = edited;
        show_error(caption("NetRefusalUnknownStaticTitle", "Warning"), "Leave at least one slot Available for a LAN player.");
        return;
    }
    memset(ready, 0, sizeof(ready));
    push_lobby();
    if (!M_NetHost(gamename[0] ? gamename : playername, maps[selectedmap].path, humans)) {
        network_error(NULL);
        return;
    }
    hosting = waiting = true;
}

static void send_chat(void) {
    if (!message[0]) return;
    if (M_NetChat(message)) message[0] = '\0';
}

static void open_chat(bool preserve) {
    origin = page;
    page = MPCHAT;
    if (!preserve) M_NetStop();
    hosting = joined = waiting = locked_choice = false;
    shownplayers = shownchat = -1;
    memset(shown_lobby, 0, sizeof(shown_lobby));
    selectedmap = -1;
    default_setup(0);
    snprintf(gamename, sizeof(gamename), "%.14s's Game", playername);
}

static void join(const char *where) {
    if (!M_NetJoinAddress(where)) { network_error(NULL); return; }
    open_chat(true);
    joined = waiting = true;
    locked_choice = false;
}

static void cycle(int *value, int count, bool back) {
    *value = (*value + (back ? count - 1 : 1)) % count;
}

/* The type cell shows the player name, so it stays a button. Available is an
 * empty slot in instant action and an open seat for a LAN player. */
static void row_action(int id, bool back) {
    int i = id % 10;
    dr_slot_t *slot = &setup.slots[i];
    if (i <= 0 || i >= setup.count || id >= ID_ROWSIDE) return;
    static const uint8_t order[] = {DR_SLOT_AVAILABLE, DR_SLOT_EASY, DR_SLOT_MEDIUM, DR_SLOT_HARD, DR_SLOT_CLOSED};
    int at = 0;
    for (int k = 0; k < 5; ++k) if (order[k] == slot->type) at = k;
    cycle(&at, 5, back);
    slot->type = order[at];
}

static void activate(app_t *app, int id, bool back) {
    if (popup) {
        if (id == ID_POPOK || id == ID_MAPCANCEL) popup = POP_NONE;
        else if (id == ID_MAPOK && popupmap >= 0) {
            popup = POP_NONE;
            selectedmap = popupmap;
            default_setup(maps[selectedmap].players);
        }
        return;
    }
    if (id == ID_MAINMENU) {
        leave_session();
        DR_ShellReturn(app);
        return;
    }
    switch (page) {
    case MPMAIN: {
        size_t n = strlen(playername);
        if (n < 3 || playername[0] == ' ' || playername[n - 1] == ' ') {
            show_error(caption("MainNameNoGoodStaticTitle", "Warning"),
                       caption("MainNameNoGoodDesc", "Enter a player name of at least three letters."));
            return;
        }
        if (id == ID_IPX) {
            page = MPLAN;
            selectedgame = -1;
            if (!M_NetBrowse()) network_error(NULL);
        } else if (id == ID_MANUAL) page = MPMANUAL;
        break;
    }
    case MPLAN:
    case MPMANUAL:
        if (id == ID_BACK) { leave_session(); page = MPMAIN; }
        else if (id == ID_CREATE) { M_NetStop(); open_chat(false); }
        else if (id == ID_JOIN) {
            int count;
            const netgame_t *games = I_NetGames(&count);
            if (page == MPMANUAL) join(address);
            else if (selectedgame >= 0 && selectedgame < count) join(games[selectedgame].address);
        }
        break;
    case MPCHAT:
        if (id == ID_BACK) {
            leave_session();
            page = origin;
            if (page == MPLAN && !M_NetBrowse()) network_error(NULL);
        } else if (id == ID_LAUNCH && joined) {
            int me = doomcom->consoleplayer;
            M_NetToggleReady();
            if (M_NetSeat(me)) ready[me] = M_NetSeat(me)->ready;
        } else if (id == ID_LAUNCH) launch();
        else if (joined) return;
        else if (id == ID_SELECTMAP) { popup = POP_MAP; popupmap = selectedmap; }
        else if (id >= ID_ROWTYPE && id < ID_ROWSIDE) row_action(id, back);
        else if (id >= ID_COLOUR && id < ID_COLOUR + 9) colour = id - ID_COLOUR;
        break;
    }
}

static void rebuild(app_t *app) {
    if (active && !build()) {
        fprintf(stderr, "Could not load Dark Reign multiplayer screen %d\n", page);
        menuerror = true;
        app->running = false;
    }
}

static bool choice_id(int id) {
    return id == ID_FOG || id == ID_UNITS || id == ID_PLACEMENT || id == ID_DISPLAY ||
           id == ID_GIVE || id == ID_VIEW || id == ID_HANDICAPS || id == ID_ALLIANCES ||
           (id >= ID_ROWSIDE && id < ID_ROWHANDICAP + 10);
}

/* A dropdown's value is the chosen row. A joiner publishes only its own seat. */
static void apply_choice(const menuitem_t *item) {
    int id = item->id, value = item->value;
    if (id == ID_FOG) fog = value;
    else if (id == ID_UNITS) units = value;
    else if (id == ID_PLACEMENT) placement = value;
    else if (id == ID_DISPLAY) display = value;
    else if (id == ID_GIVE) give = value;
    else if (id == ID_VIEW) view = value;
    else if (id == ID_HANDICAPS) handicaps = value;
    else if (id == ID_ALLIANCES) alliances = value;
    else if (id >= ID_ROWSIDE && id < ID_ROWHANDICAP + 10) {
        int i = id % 10;
        if (i < 0 || i >= 8) return;
        dr_slot_t *slot = &setup.slots[i];
        if (id < ID_ROWTEAM) slot->side = (uint8_t)value;
        else if (id < ID_ROWHANDICAP) slot->team = (uint8_t)value;
        else handicap[i] = value;
        if (joined && doomcom && (id == ID_ROWSIDE + doomcom->consoleplayer ||
                                  id == ID_ROWTEAM + doomcom->consoleplayer)) {
            const netseat_t *cur = M_NetSeat(doomcom->consoleplayer);
            if (cur) {
                netseat_t seat = *cur;
                seat.race = slot->side;
                seat.team = slot->team;
                M_NetSetSeat(doomcom->consoleplayer, &seat);
            }
        }
    }
}

static void routine(menu_t *screen, menuitem_t *item, menuaction_t action) {
    app_t *app = screen->app;
    int id = item->id;
    if (action == MA_CHANGE) {
        if (id == ID_NAME) snprintf(playername, sizeof(playername), "%s", item->text);
        else if (id == ID_ADDRESS) snprintf(address, sizeof(address), "%s", item->text);
        else if (id == ID_GAMENAME) snprintf(gamename, sizeof(gamename), "%s", item->text);
        else if (id == ID_MESSAGE) snprintf(message, sizeof(message), "%s", item->text);
        else if (id == ID_CREDITS) {
            /* The credits edit is numeric (flags 2). */
            char *q = item->text;
            for (const char *p = item->text; *p; ++p) if (isdigit((unsigned char)*p)) *q++ = *p;
            *q = '\0';
            snprintf(credits, sizeof(credits), "%s", item->text);
        } else if (id == ID_GAMES) selectedgame = item->value;
        else if (id == ID_MAPLIST) popupmap = item->value;
        else if (choice_id(id)) apply_choice(item);
        if (id == ID_GAMES || id == ID_MAPLIST || choice_id(id)) {
            if (choice_id(id) && active && hosting && page == MPCHAT && !push_lobby()) network_error(NULL);
            if (active) {
                drscreen.menu.held = NULL;
                rebuild(app);
            }
        }
        return;
    }
    /* Enter in the message line sends it ("<name>: <text>"). */
    if (id == ID_MESSAGE && action == MA_ACTIVATE && screen->held != item) {
        send_chat();
        rebuild(app);
        return;
    }
    bool type_cell = id >= ID_ROWTYPE && id < ID_ROWSIDE;
    if ((action != MA_ACTIVATE && !(action == MA_SECONDARY && type_cell)) ||
        item->kind == MI_TEXTFIELD || item->kind == MI_LIST || item->kind == MI_DROPDOWN) return;
    activate(app, id, action == MA_SECONDARY);
    /* The host's edits reach the joiners at once. A joiner publishes only its own row. */
    if (active && hosting && page == MPCHAT && !push_lobby()) network_error(NULL);
    if (active) {
        drscreen.menu.held = NULL;
        rebuild(app);
    }
}

static void escape(menu_t *screen) {
    app_t *app = screen->app;
    if (popup) popup = POP_NONE;
    else if (page == MPMAIN || instant) {
        leave_session();
        DR_ShellReturn(app);
        return;
    } else activate(app, ID_BACK, false);
    rebuild(app);
}

/* ── lifecycle ──────────────────────────────────────────────────────────── */

bool DR_MultiOpen(app_t *app, const char *data_root, bool ia) {
    drscreen.menu.app = app;
    static const char *const names[] = {"F16BLUE.PCX", "F14BLUE.PCX", "F14BLUEO.PCX", "F14BLUEG.PCX", "F12GOLD.PCX",
                                        "F12TEAM.PCX", "F12BLUEN.PCX", "F12BLUEO.PCX", "F12BLUEG.PCX"};
    static const char *const bitmaps[] = {"BUTTON.BMP", "LAUNCH.BMP", "DROP.BMP", "DROP2.BMP", "COLOUR.BMP",
                                          "POP_1PIC.BMP", "POP_MAP.BMP", "LIGHTS.BMP"};
    snprintf(root, sizeof(root), "%s", data_root);
    free_assets();
    bool ok = scan_maps();
    for (int i = 0; ok && i < NUMFONTS; ++i) ok = load_font(names[i], &fonts[i]);
    for (int i = 0; ok && i < NUMART; ++i) ok = load_bmp(bitmaps[i], &art[i]);
    if (!ok) { free_assets(); return false; }
    instant = ia;
    popup = POP_NONE;
    hosting = joined = waiting = false;
    page = MPMAIN;
    credits[0] = '\0';
    fog = units = placement = display = give = view = handicaps = alliances = colour = 0;
    if (!instant) use_net();
    if (instant) open_chat(false);
    shell_escape = drscreen.menu.escape;
    drscreen.menu.escape = escape;
    drscreen.menu.itemOn = -1;
    active = true;
    if (!build()) { DR_MultiClose(); return false; }
    return true;
}

void DR_MultiClose(void) {
    if (!active) return;
    active = false;
    drscreen.menu.escape = shell_escape;
    free_assets();
}

bool DR_MultiActive(void) { return active; }

void DR_MultiTicker(void) {
    app_t *app = drscreen.menu.app;
    if (!active) return;
    if (page == MPLAN && !popup) {
        int count;
        I_NetGames(&count);
        const menuitem_t *games = M_MenuFind(&drscreen.menu, ID_GAMES);
        if (selectedgame >= count) selectedgame = -1;
        if (neterror[0]) network_error(NULL);
        if (popup || (games && games->rows != count)) rebuild(app);
    }
    if (!waiting) return;
    int status = M_NetPoll();
    if (status < 0) { network_error(NULL); rebuild(app); return; }
    if (status > 0) {
        waiting = joined = hosting = false;
        if (dr_bad) {
            menumap = NULL;
            show_error(caption("NetRefusalUnknownStaticTitle", "Warning"), "Host sent an invalid game setup");
            M_NetStop();
            rebuild(app);
        } else {
            DR_MultiClose();
            M_ClearMenus();
        }
        return;
    }
    uint8_t opt[16];
    bool got = M_NetOptions(opt, sizeof(opt)) == sizeof(opt);
    if (!hosting && M_NetInLobby() && got && !locked_choice && doomcom) {
        /* A joiner starts on Default, then chooses its own side and team. */
        int me = doomcom->consoleplayer;
        const netseat_t *cur = M_NetSeat(me);
        if (cur) {
            netseat_t seat = *cur;
            seat.race = 0;
            seat.team = 0;
            M_NetSetSeat(me, &seat);
        }
        locked_choice = true;
    }
    if ((hosting || M_NetInLobby()) && got && !pull_lobby()) {
        network_error("Host sent an invalid game setup");
        rebuild(app);
        return;
    }
    /* Redraw the lobby as it changes; typing keeps its focus. */
    uint8_t image[56] = {0};
    if (got) {
        for (int i = 0; i < 8; ++i) {
            const netseat_t *seat = M_NetSeat(i);
            if (!seat) continue;
            image[i * 4] = seat->race;
            image[i * 4 + 1] = seat->type;
            image[i * 4 + 2] = seat->color;
            image[i * 4 + 3] = seat->team;
            image[48 + i] = seat->ready;
        }
        memcpy(image + 32, opt, sizeof(opt));
    }
    if (I_NetPlayerCount() != shownplayers || I_NetChatCount() != shownchat || memcmp(image, shown_lobby, sizeof(image))) {
        shownplayers = I_NetPlayerCount();
        shownchat = I_NetChatCount();
        memcpy(shown_lobby, image, sizeof(image));
        rebuild(app);
    }
}
