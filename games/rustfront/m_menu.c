#define _DEFAULT_SOURCE
#include "engine.h"
#include "rustfront.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Rustfront's front end: the title screen and its pages, drawn over assets/title.pcx (art/title.py)
 * in the game's own faces (assets/font_*.pcx, art/font.py).
 *
 *   Main          Campaign, Skirmish, Multiplayer, Load Game, Options, Quit
 *   Campaign      a faction's five missions, the chosen one's briefing and objectives, Start
 *   Skirmish      a map with starts, and for every start who plays it (you, the computer at a
 *                 difficulty, nobody), as which faction and on which team
 *   Multiplayer   the engine's shared host / join / lobby screens (hud/m_net.c): every seat
 *                 picks a faction, and the lobby's launch turns the seats into a start plan
 *   Load, Save    eight slots under the user directory
 *   Options       effects and music volume, game speed; kept in settings.cfg
 *   In a level    Resume, the mission's objectives, Save, Load, Options, Leave, Quit
 *
 * The campaign's progress, missions and briefings come from g_campaign.c; maps, starts and the
 * start plan from w_starts.c; the computer's difficulty from g_ai.c. */

enum { MAIN, CAMPAIGN, SKIRMISH, LOAD, SAVE, OPTIONS, PAUSE };
enum {
    NONE, GO_CAMPAIGN, GO_SKIRMISH, GO_MULTI, GO_LOAD, GO_SAVE, GO_OPTIONS, QUIT, BACK, RESUME, LEAVE,
    FACTION, MISSION, START, BRIEFING, OBJECTIVES, MAPS, MAPINFO, SEAT_WHO, SEAT_FACTION, SEAT_TEAM,
    SLOTS, ACCEPT, SOUND, MUSIC, SPEED, LABEL, TITLE, NOTE, RESET,
};

#define SAVE_SLOTS 8
#define MAX_MAPS 32
enum { WHO_CLOSED, WHO_YOU, WHO_EASY, WHO_NORMAL, WHO_HARD, WHO_COUNT };
static const char *const WHO[WHO_COUNT] = {"Closed", "You", "Computer: easy", "Computer: normal",
                                           "Computer: hard"};
static const char *const FACTIONS[4] = {"Directorate", "Scrap Clans", "Lumen Concord", "Random"};
enum { RANDOM_FACTION = 3 };

/* Colours, in the title palette's exact inks (art/title.py keeps them). */
#define INK 0xffdce6dcu
#define INK_DIM 0xff96a096u
#define INK_HOT 0xffffc460u
#define PANEL 0xff0c1216u
#define PANEL_RIM 0xff3c4642u
#define BUTTON 0xff182428u
#define BUTTON_RIM 0xff5a2818u
#define FOCUS 0xffe8963cu
#define ROW 0xff283842u

static app_t *front_app;
static menuitem_t items[96];
static menu_t menu;
static int page = MAIN, return_page = MAIN;
static bool initialized, inlevel;
static spritesheet_t backdrop;
static bitmapfont_t body, head;
static bool have_backdrop, have_fonts;
static char root_dir[1024];

/* Campaign page. */
static int campaign_faction, campaign_mission;
static rf_missioninfo_t mission;
static char objectives_text[RF_MAX_OBJECTIVES * (RF_OBJECTIVE_TEXT + 4)];

/* Skirmish page. */
static rf_mapinfo_t maps[MAX_MAPS];
static int map_count, map_choice;
static struct { int who, faction, team; } seats[RF_MAX_STARTS];

/* Saves. */
static saveinfo_t slot_info[SAVE_SLOTS];
static bool slot_used[SAVE_SLOTS];

/* What a launched game was, to come back to its page afterwards. */
static char launch[1024], launch_title[64];

/* ── faces ──────────────────────────────────────────────────────────────── */

/* A face of assets/font.txt: a 16-column grid of ASCII 32..126 in equal cells. */
static bool load_face(const char *root, const char *name, bitmapfont_t *font) {
    memset(font, 0, sizeof(*font));
    char path[1200];
    snprintf(path, sizeof(path), "%s/assets/font.txt", root);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[1024];
    int cw = 0, ch = 0, ascent = 0, adv[95] = {0};
    bool found = false;
    while (!found && fgets(line, sizeof(line), f)) {
        char face[16];
        int n = 0;
        if (sscanf(line, "face %15s %d %d %d%n", face, &cw, &ch, &ascent, &n) != 4 || strcmp(face, name)) continue;
        const char *p = line + n;
        found = true;
        for (int i = 0; i < 95; ++i) {
            int used = 0;
            if (sscanf(p, "%d%n", &adv[i], &used) != 1) { found = false; break; }
            p += used;
        }
    }
    fclose(f);
    if (!found || cw <= 0 || ch <= 0) return false;
    snprintf(path, sizeof(path), "%s/assets/font_%s.pcx", root, name);
    spritesheet_t grid;
    if (!W_LoadIndexedSheet(path, &grid)) return false;
    int gw = grid.frame_size.w;
    if (gw < 16 * cw || grid.frame_size.h < 6 * ch || !R_AllocSpriteCells(&font->sprite, 95)) {
        R_FreeSprite(&grid);
        return false;
    }
    spritesheet_t *s = &font->sprite;
    s->indexed = true;
    s->frame_size = (isize2_t){cw, ch};
    memcpy(s->palette, grid.source_palette, sizeof(s->palette));
    memcpy(s->source_palette, grid.source_palette, sizeof(s->source_palette));
    for (int i = 0; i < 256; ++i) font->glyph_index[i] = -1;
    for (int i = 0; i < 95; ++i) {
        uint8_t *pixels = calloc((size_t)cw * ch, 1);
        if (!pixels) { R_FreeSprite(&grid); R_FreeSprite(s); return false; }
        int ox = (i % 16) * cw, oy = (i / 16) * ch;
        for (int y = 0; y < ch; ++y) memcpy(pixels + y * cw, grid.lumps[0].indices + (oy + y) * gw + ox, (size_t)cw);
        s->lumps[i].indices = pixels;
        s->cells[i].rect = s->cells[i].bounds = (irect_t){0, 0, cw, ch};
        font->glyph_index[32 + i] = i;
        font->glyph_width[32 + i] = (uint8_t)adv[i];
    }
    R_FreeSprite(&grid);
    font->glyph_size = (isize2_t){cw, ch};
    font->line_h = ch + 1;
    font->draw_divisor = 1;
    font->own_palette = true;
    font->glyph_limit = 128;
    return true;
}

/* ── items ─────────────────────────────────────────────────────────────── */

static void escape(menu_t *m);
static void refresh(menu_t *m);
static void routine(menu_t *m, menuitem_t *item, menuaction_t action);

static menuitem_t *add(menuitemkind_t kind, int id, irect_t rect) {
    if (menu.numitems >= (int)(sizeof(items) / sizeof(*items))) return &items[menu.numitems - 1];
    menuitem_t *item = &items[menu.numitems++];
    *item = (menuitem_t){.kind = kind, .id = id, .rect = rect, .visible = true, .enabled = kind != MI_STATIC,
                         .release = true, .link = -1, .routine = routine, .ink = INK,
                         .font = have_fonts ? &body : NULL};
    for (int s = 0; s < MS_STATES; ++s) item->look[s] = (menulook_t){.cell = -1, .palette = -1};
    return item;
}

static menuitem_t *panel(irect_t rect) {
    menuitem_t *item = add(MI_STATIC, NONE, rect);
    item->fill = PANEL;
    item->border = PANEL_RIM;
    return item;
}

static menuitem_t *label(int id, irect_t rect, const char *text, int align) {
    menuitem_t *item = add(MI_STATIC, id, rect);
    snprintf(item->text, sizeof(item->text), "%s", text ? text : "");
    item->align = align;
    return item;
}

static menuitem_t *heading(irect_t rect, const char *text) {
    menuitem_t *item = label(TITLE, rect, text, MALIGN_LEFT | MALIGN_VCENTER);
    if (have_fonts) item->font = &head;
    item->ink = INK_HOT;
    return item;
}

static menuitem_t *button(int id, irect_t rect, const char *text, SDL_Keycode key) {
    menuitem_t *item = add(MI_BUTTON, id, rect);
    snprintf(item->text, sizeof(item->text), "%s", text);
    item->align = MALIGN_CENTER;
    item->fill = BUTTON;
    item->border = BUTTON_RIM;
    item->color = FOCUS;
    item->hotkey = key;
    item->look[MS_FOCUS].ink = INK_HOT;
    item->look[MS_PUSHED].ink = INK_HOT;
    item->look[MS_PUSHED].shift = (ivec2_t){1, 1};
    item->look[MS_DISABLED].ink = INK_DIM;
    return item;
}

static menuitem_t *list(int id, irect_t rect, int rows, const char *(*row)(const menuitem_t *, int)) {
    menuitem_t *item = add(MI_LIST, id, rect);
    item->rows = rows;
    item->row = row;
    item->row_height = have_fonts ? body.line_h + 3 : 12;
    item->value = rows ? 0 : -1;
    item->fill = PANEL;
    item->border = PANEL_RIM;
    item->color = ROW;
    item->inset = (ivec2_t){6, 2};
    return item;
}

static menuitem_t *prose(int id, irect_t rect, const char *text) {
    menuitem_t *item = add(MI_STATIC, id, rect);
    item->prose = text;
    return item;
}

static menuitem_t *slider(int id, irect_t rect, int min, int max, int value) {
    menuitem_t *item = add(MI_SLIDER, id, rect);
    item->range.min = min;
    item->range.max = max;
    item->value = value;
    item->fill = BUTTON;
    item->border = BUTTON_RIM;
    item->color = FOCUS;
    item->thumb.part = (irect_t){0, 0, 10, rect.h};
    return item;
}

/* ── the pages ─────────────────────────────────────────────────────────── */

static const char *mission_row(const menuitem_t *item, int index) {
    (void)item;
    rf_missioninfo_t info;
    if (!RF_MissionInfo(root_dir, campaign_faction, index, &info)) return "";
    bool open = RF_MissionUnlocked(campaign_faction, index), won = RF_CampaignWon(campaign_faction) > index;
    return M_va("%d. %s%s", index + 1, open ? info.title : "Locked", won ? "  - won" : "");
}

static const char *map_row(const menuitem_t *item, int index) {
    (void)item;
    if (index < 0 || index >= map_count) return "";
    return M_va("%s (%d)", maps[index].title, maps[index].players);
}

static const char *slot_row(const menuitem_t *item, int index) {
    (void)item;
    if (index < 0 || index >= SAVE_SLOTS) return "";
    return slot_used[index] ? M_va("%d. %s", index + 1, slot_info[index].name) : M_va("%d. Empty", index + 1);
}

static void slot_path(int slot, char *out, size_t size) {
    char name[32];
    snprintf(name, sizeof(name), "rustfront-%d.sav", slot + 1);
    M_PathJoin(out, size, D_UserDirectory(), name);
}

static void read_slots(void) {
    for (int i = 0; i < SAVE_SLOTS; ++i) {
        char path[1200];
        slot_path(i, path, sizeof(path));
        slot_used[i] = access(path, R_OK) == 0 && G_SaveInfo(path, &slot_info[i]);
    }
}

static void read_mission(void) {
    memset(&mission, 0, sizeof(mission));
    objectives_text[0] = '\0';
    if (!RF_MissionUnlocked(campaign_faction, campaign_mission)) {
        snprintf(mission.briefing, sizeof(mission.briefing), "Win the mission before this one to open it.");
        return;
    }
    if (!RF_MissionInfo(root_dir, campaign_faction, campaign_mission, &mission)) return;
    size_t at = 0;
    for (int i = 0; i < mission.objective_count && at < sizeof(objectives_text); ++i)
        at += (size_t)snprintf(objectives_text + at, sizeof(objectives_text) - at, "- %s\n", mission.objectives[i]);
}

static void read_maps(void) {
    rf_mapinfo_t all[MAX_MAPS];
    int n = RF_ListMaps(root_dir, all, MAX_MAPS);
    map_count = 0;
    for (int i = 0; i < n; ++i)
        if (all[i].players >= 2) maps[map_count++] = all[i];
    if (map_choice >= map_count) map_choice = 0;
}

/* Seats follow the map: you at the first start, the computer at normal on the rest. */
static void default_seats(void) {
    for (int i = 0; i < RF_MAX_STARTS; ++i)
        seats[i].who = i == 0 ? WHO_YOU : WHO_NORMAL, seats[i].faction = i % 3, seats[i].team = 0;
}

static void open_page(int next);

static void build_main(void) {
    int x = 220, y = 180, w = 200, h = 30, gap = 38;
    button(GO_CAMPAIGN, (irect_t){x, y, w, h}, "Campaign", SDLK_c);
    button(GO_SKIRMISH, (irect_t){x, y += gap, w, h}, "Skirmish", SDLK_s);
    button(GO_MULTI, (irect_t){x, y += gap, w, h}, "Multiplayer", SDLK_m);
    button(GO_LOAD, (irect_t){x, y += gap, w, h}, "Load Game", SDLK_l);
    button(GO_OPTIONS, (irect_t){x, y += gap, w, h}, "Options", SDLK_o);
    button(QUIT, (irect_t){x, y += gap, w, h}, "Quit", SDLK_q);
    label(NOTE, (irect_t){0, 456, 632, 16}, "Rustfront - an open-rts game", MALIGN_RIGHT | MALIGN_VCENTER)->ink = INK_DIM;
}

static void build_campaign(void) {
    panel((irect_t){24, 104, 592, 360});
    heading((irect_t){40, 110, 400, 28}, "Campaign");
    for (int f = 0; f < 3; ++f) {
        menuitem_t *b = button(FACTION, (irect_t){40 + f * 132, 144, 126, 26}, RF_CampaignName(f), SDLK_1 + f);
        b->value = f;
        if (f == campaign_faction) b->fill = ROW, b->ink = INK_HOT;
    }
    menuitem_t *l = list(MISSION, (irect_t){40, 182, 230, 5 * (have_fonts ? body.line_h + 3 : 12) + 4},
                         RF_CAMPAIGN_MISSIONS, mission_row);
    l->value = campaign_mission;
    label(LABEL, (irect_t){40, 296, 230, 16}, "Objectives", MALIGN_LEFT)->ink = INK_HOT;
    prose(OBJECTIVES, (irect_t){40, 316, 230, 100}, objectives_text);
    label(LABEL, (irect_t){290, 182, 310, 18}, mission.title[0] ? mission.title : "", MALIGN_LEFT)->ink = INK_HOT;
    prose(BRIEFING, (irect_t){290, 204, 310, 206}, mission.briefing);
    button(BACK, (irect_t){40, 424, 120, 28}, "Back", SDLK_ESCAPE);
    button(RESET, (irect_t){170, 424, 150, 28}, "Reset Progress", 0);
    button(START, (irect_t){480, 424, 120, 28}, "Start", SDLK_RETURN);
    menu.itemOn = (int)(l - items);
}

static void build_skirmish(void) {
    panel((irect_t){24, 104, 592, 360});
    heading((irect_t){40, 110, 400, 28}, "Skirmish");
    int rh = have_fonts ? body.line_h + 3 : 12;
    menuitem_t *l = list(MAPS, (irect_t){40, 144, 200, 10 * rh + 4}, map_count, map_row);
    l->value = map_count ? map_choice : -1;
    l->prose = "No maps with starts";
    label(MAPINFO, (irect_t){40, 150 + 10 * rh, 200, 40}, "", MALIGN_LEFT)->ink = INK_DIM;
    label(LABEL, (irect_t){256, 144, 140, 16}, "Player", MALIGN_LEFT)->ink = INK_HOT;
    label(LABEL, (irect_t){402, 144, 120, 16}, "Faction", MALIGN_LEFT)->ink = INK_HOT;
    label(LABEL, (irect_t){528, 144, 72, 16}, "Team", MALIGN_LEFT)->ink = INK_HOT;
    int players = map_count ? maps[map_choice].players : 0;
    for (int i = 0; i < players && i < RF_MAX_STARTS; ++i) {
        int y = 164 + i * 30;
        menuitem_t *who = button(SEAT_WHO, (irect_t){256, y, 140, 24}, WHO[seats[i].who], 0);
        menuitem_t *fac = button(SEAT_FACTION, (irect_t){402, y, 120, 24}, FACTIONS[seats[i].faction], 0);
        menuitem_t *team = button(SEAT_TEAM, (irect_t){528, y, 72, 24},
                                  seats[i].team ? M_va("%d", seats[i].team) : "-", 0);
        who->value = fac->value = team->value = i;
        if (seats[i].who == WHO_CLOSED) {
            fac->enabled = team->enabled = false;
            snprintf(fac->text, sizeof(fac->text), "-");
            snprintf(team->text, sizeof(team->text), "-");
        }
    }
    prose(NOTE, (irect_t){256, 300, 344, 60},
          "Click a box to change it, right-click to step back. Starts on the same team are allies; "
          "'-' plays alone.");
    button(BACK, (irect_t){40, 424, 120, 28}, "Back", SDLK_ESCAPE);
    button(START, (irect_t){480, 424, 120, 28}, "Start", SDLK_RETURN);
    menu.itemOn = (int)(l - items);
}

static void build_slots(bool saving) {
    panel((irect_t){120, 104, 400, 340});
    heading((irect_t){136, 110, 300, 28}, saving ? "Save Game" : "Load Game");
    read_slots();
    int rh = have_fonts ? body.line_h + 3 : 12;
    menuitem_t *l = list(SLOTS, (irect_t){136, 146, 368, SAVE_SLOTS * rh + 4}, SAVE_SLOTS, slot_row);
    if (!saving) {
        l->value = -1;
        for (int i = 0; i < SAVE_SLOTS && l->value < 0; ++i)
            if (slot_used[i]) l->value = i;
    }
    button(BACK, (irect_t){136, 400, 120, 28}, "Back", SDLK_ESCAPE);
    button(ACCEPT, (irect_t){384, 400, 120, 28}, saving ? "Save" : "Load", SDLK_RETURN);
    menu.itemOn = (int)(l - items);
}

static void build_options(void) {
    panel((irect_t){120, 120, 400, 260});
    heading((irect_t){136, 126, 300, 28}, "Options");
    label(LABEL, (irect_t){136, 176, 140, 20}, "Effects volume", MALIGN_LEFT | MALIGN_VCENTER);
    slider(SOUND, (irect_t){290, 178, 200, 16}, 0, 10, gamesettings.sound);
    label(LABEL, (irect_t){136, 216, 140, 20}, "Music volume", MALIGN_LEFT | MALIGN_VCENTER);
    slider(MUSIC, (irect_t){290, 218, 200, 16}, 0, 10, gamesettings.music);
    label(LABEL, (irect_t){136, 256, 140, 20}, "Game speed", MALIGN_LEFT | MALIGN_VCENTER);
    slider(SPEED, (irect_t){290, 258, 200, 16}, 5, 20, game_speed / 10);
    label(NOTE, (irect_t){136, 290, 354, 20}, "", MALIGN_LEFT | MALIGN_VCENTER)->ink = INK_DIM;
    button(BACK, (irect_t){136, 336, 120, 28}, "Back", SDLK_ESCAPE);
}

static void build_pause(void) {
    int in_mission = RF_InMission(NULL, NULL);
    panel((irect_t){180, 64, 280, in_mission ? 330 : 250});
    heading((irect_t){196, 70, 248, 28}, in_mission ? RF_MissionTitle() : "Paused");
    int y = 104;
    if (in_mission) {
        size_t at = 0;
        for (int i = 0; i < RF_ObjectiveCount() && at < sizeof(objectives_text); ++i) {
            int st = RF_ObjectiveState(i);
            at += (size_t)snprintf(objectives_text + at, sizeof(objectives_text) - at, "%s %s\n",
                                   st == RF_OBJECTIVE_DONE ? "[done]" : st == RF_OBJECTIVE_FAILED ? "[failed]" : "-",
                                   RF_ObjectiveText(i));
        }
        prose(OBJECTIVES, (irect_t){196, y, 248, 76}, objectives_text);
        y += 82;
    }
    int x = 220, w = 200, h = 28, gap = 34;
    menuitem_t *resume = button(RESUME, (irect_t){x, y, w, h}, "Resume", 0);
    button(GO_SAVE, (irect_t){x, y += gap, w, h}, "Save Game", SDLK_s)->enabled = !netgame;
    button(GO_LOAD, (irect_t){x, y += gap, w, h}, "Load Game", SDLK_l)->enabled = !netgame;
    button(GO_OPTIONS, (irect_t){x, y += gap, w, h}, "Options", SDLK_o);
    button(LEAVE, (irect_t){x, y += gap, w, h}, netgame ? "Leave Game" : "Abandon", 0);
    button(QUIT, (irect_t){x, y += gap, w, h}, "Quit to Desktop", 0);
    menu.itemOn = (int)(resume - items);
}

static void open_page(int next) {
    page = next;
    memset(items, 0, sizeof(items));
    menu = (menu_t){.items = items, .modal = true, .size = {640, 480}, .fit = true, .itemOn = -1,
                    .escape = escape, .refresh = refresh, .app = front_app};
    if (!inlevel && have_backdrop) {
        menu.background = &backdrop;
        menu.palette = backdrop.source_palette;
    }
    switch (page) {
    case MAIN: build_main(); break;
    case CAMPAIGN: read_mission(); build_campaign(); break;
    case SKIRMISH: read_maps(); build_skirmish(); break;
    case LOAD: build_slots(false); break;
    case SAVE: build_slots(true); break;
    case OPTIONS: build_options(); break;
    case PAUSE: build_pause(); break;
    }
    if (menu.itemOn < 0)
        for (int i = 0; i < menu.numitems; ++i)
            if (items[i].kind == MI_BUTTON && items[i].enabled) { menu.itemOn = i; break; }
    M_SetupNextMenu(&menu);
    refresh(&menu);
}

static void refresh(menu_t *m) {
    (void)m;
    if (page == CAMPAIGN) {
        menuitem_t *start = M_MenuFind(&menu, START);
        if (start) start->enabled = RF_MissionUnlocked(campaign_faction, campaign_mission);
    } else if (page == SKIRMISH) {
        menuitem_t *info = M_MenuFind(&menu, MAPINFO), *start = M_MenuFind(&menu, START);
        int you = 0, sides = 0, players = map_count ? maps[map_choice].players : 0;
        for (int i = 0; i < players; ++i) you += seats[i].who == WHO_YOU, sides += seats[i].who != WHO_CLOSED;
        if (info && map_count)
            snprintf(info->text, sizeof(info->text), "%d x %d, %d starts", maps[map_choice].width,
                     maps[map_choice].height, maps[map_choice].players);
        if (start) start->enabled = map_count && you == 1 && sides >= 2;
    } else if (page == LOAD || page == SAVE) {
        menuitem_t *l = M_MenuFind(&menu, SLOTS), *accept = M_MenuFind(&menu, ACCEPT);
        if (l && accept) accept->enabled = l->value >= 0 && l->value < SAVE_SLOTS && (page == SAVE || slot_used[l->value]);
    } else if (page == OPTIONS) {
        menuitem_t *note = M_MenuFind(&menu, NOTE);
        if (note) snprintf(note->text, sizeof(note->text), "Effects %d%%, music %d%%, speed %d%%",
                           gamesettings.sound * 10, gamesettings.music * 10, game_speed);
    }
}

static void escape(menu_t *m) {
    (void)m;
    if (inlevel) {
        if (page == PAUSE) M_ClearMenus();
        else open_page(PAUSE);
    } else if (page != MAIN) open_page(MAIN);
}

/* ── starting games ────────────────────────────────────────────────────── */

static void start_level(const char *path, const char *title, int back) {
    snprintf(launch, sizeof(launch), "%s", path);
    snprintf(launch_title, sizeof(launch_title), "%s", title ? title : "");
    return_page = back;
    g_loadfile[0] = '\0';
    menumap = launch;
    M_ClearMenus();
}

static void all_normal(void) {
    for (int owner = 0; owner < MAXPLAYERS; ++owner) RF_SetAIDifficulty(owner, RF_AI_NORMAL);
}

static void start_mission(void) {
    char path[256];
    if (!RF_MissionUnlocked(campaign_faction, campaign_mission) ||
        !RF_MissionPath(campaign_faction, campaign_mission, path, sizeof(path))) return;
    RF_SetStartPlan(NULL);
    all_normal();
    start_level(path, mission.title, CAMPAIGN);
}

/* You are owner 0 at your start; the computer players take owners 1, 2, ... */
static void start_skirmish(void) {
    if (!map_count) return;
    const rf_mapinfo_t *map = &maps[map_choice];
    rf_startplan_t plan = {.count = 0};
    for (int i = 0; i < RF_MAX_STARTS; ++i) plan.seat[i] = (rf_seat_t){.start = -1, .faction = -1};
    all_normal();
    int next = 1;
    for (int i = 0; i < map->players && i < RF_MAX_STARTS; ++i) {
        if (seats[i].who == WHO_CLOSED) continue;
        int owner = seats[i].who == WHO_YOU ? 0 : next++;
        if (owner >= RF_MAX_STARTS) continue;
        int faction = seats[i].faction == RANDOM_FACTION ? rand() % 3 : seats[i].faction;
        plan.seat[owner] = (rf_seat_t){.start = (int8_t)i, .faction = (int8_t)faction, .team = (uint8_t)seats[i].team};
        if (owner + 1 > plan.count) plan.count = owner + 1;
        if (owner)
            RF_SetAIDifficulty(owner, seats[i].who == WHO_EASY ? RF_AI_EASY : seats[i].who == WHO_HARD ? RF_AI_HARD
                                                                                                        : RF_AI_NORMAL);
    }
    RF_SetStartPlan(&plan);
    start_level(map->path, map->title, SKIRMISH);
}

/* The engine's lobby: every person a seat (owners 0..n-1, in the order they joined), each at the
 * start of the same number, as the faction the seat picked. Every peer builds the same plan from
 * the published seats, so the starts match before the first tic. */
static void net_commit(void) {
    rf_startplan_t plan = {.count = 0};
    for (int i = 0; i < RF_MAX_STARTS; ++i) plan.seat[i] = (rf_seat_t){.start = -1, .faction = -1};
    for (int i = 0; i < RF_MAX_STARTS && i < MAXPLAYERS; ++i) {
        int race = M_NetPlayerRace(i);
        if (race < 0) continue;
        const netseat_t *seat = M_NetSeat(i);
        plan.seat[i] = (rf_seat_t){.start = (int8_t)i, .faction = (int8_t)(race % 3),
                                   .team = (uint8_t)(seat ? seat->team : 0)};
        plan.count = i + 1;
    }
    all_normal();
    RF_SetStartPlan(&plan);
    return_page = MAIN;
}
static int net_map_count(void) { return map_count; }
static const char *net_map_path(int i) { return i >= 0 && i < map_count ? maps[i].path : ""; }
static const char *net_map_title(int i) { return i >= 0 && i < map_count ? maps[i].title : ""; }
static int net_map_players(int i) { return i >= 0 && i < map_count ? maps[i].players : 0; }
static const char *net_race_name(int i) { return i >= 0 && i < 3 ? FACTIONS[i] : ""; }
static int net_slot_race(int map_index, int player) {
    (void)map_index;
    return player >= 0 ? player % 3 : -1;
}
static void net_back(app_t *app) {
    (void)app;
    open_page(MAIN);
}
static void net_style_button(menuitem_t *item) {
    if (item->kind == MI_BUTTON) {
        item->fill = BUTTON, item->border = BUTTON_RIM, item->color = FOCUS;
        item->look[MS_FOCUS].ink = INK_HOT;
    }
    if (have_fonts) item->font = &body;
}
static void net_style_label(menuitem_t *item) {
    if (have_fonts) item->font = &body;
    item->ink = INK;
}
static void net_style_list(menuitem_t *l, menuitem_t *bar) {
    l->fill = PANEL, l->border = PANEL_RIM, l->color = ROW;
    if (have_fonts) l->font = &body, l->row_height = body.line_h + 3;
    if (bar) bar->color = PANEL_RIM;
}

static void open_multiplayer(void) {
    read_maps();
    netui_t ui = {
        .background = have_backdrop ? &backdrop : NULL,
        .palette = have_backdrop ? backdrop.source_palette : NULL,
        .font = have_fonts ? &body : NULL,
        .style_button = net_style_button, .style_label = net_style_label, .style_list = net_style_list,
        .back = net_back,
        .map_count = net_map_count, .map_path = net_map_path, .map_title = net_map_title,
        .map_players = net_map_players,
        .max_players = 4, .race_count = 3, .race_name = net_race_name, .slot_race = net_slot_race,
        .commit = net_commit,
    };
    M_NetOpen(front_app, &ui);
}

/* ── input ─────────────────────────────────────────────────────────────── */

static void cycle(int *value, int count, bool back) {
    *value = (*value + (back ? count - 1 : 1)) % count;
}

static void routine(menu_t *m, menuitem_t *item, menuaction_t action) {
    if (page == OPTIONS && (action == MA_CHANGE || action == MA_ACTIVATE) && item->kind == MI_SLIDER) {
        if (item->id == SOUND) gamesettings.sound = item->value, S_SetVolume(item->value * 10);
        if (item->id == MUSIC) gamesettings.music = item->value, S_SetMusicVolume(item->value * 10);
        if (item->id == SPEED) D_SetGameSpeed(item->value * 10);
        refresh(m);
        return;
    }
    if (action == MA_CHANGE) {
        if (item->id == MISSION && item->value >= 0) { campaign_mission = item->value; open_page(CAMPAIGN); }
        if (item->id == MAPS && item->value >= 0 && item->value != map_choice) {
            map_choice = item->value;
            open_page(SKIRMISH);
        }
        if (item->id == SLOTS) refresh(m);
        return;
    }
    bool back = action == MA_SECONDARY;
    if (action != MA_ACTIVATE && !back) return;
    switch (item->id) {
    case GO_CAMPAIGN:
        campaign_mission = RF_CampaignWon(campaign_faction);
        if (campaign_mission >= RF_CAMPAIGN_MISSIONS) campaign_mission = RF_CAMPAIGN_MISSIONS - 1;
        open_page(CAMPAIGN);
        break;
    case GO_SKIRMISH: open_page(SKIRMISH); break;
    case GO_MULTI: open_multiplayer(); break;
    case GO_LOAD: if (!netgame) open_page(LOAD); break;
    case GO_SAVE: if (!netgame) open_page(SAVE); break;
    case GO_OPTIONS: open_page(OPTIONS); break;
    case QUIT: M_MenuQuitGame(m, item, MA_ACTIVATE); break;
    case BACK:
        if (page == OPTIONS) D_SaveSettings(game_speed);
        escape(m);
        break;
    case RESUME: M_ClearMenus(); break;
    case LEAVE: menuleave = true; M_ClearMenus(); break;
    case FACTION:
        campaign_faction = item->value;
        campaign_mission = RF_CampaignWon(campaign_faction);
        if (campaign_mission >= RF_CAMPAIGN_MISSIONS) campaign_mission = RF_CAMPAIGN_MISSIONS - 1;
        open_page(CAMPAIGN);
        break;
    case MISSION: /* Enter on the list starts the mission. */
    case START:
        if (page == CAMPAIGN) start_mission();
        else if (page == SKIRMISH && item->id == START) start_skirmish();
        break;
    case RESET:
        RF_CampaignResetProgress();
        campaign_mission = 0;
        open_page(CAMPAIGN);
        break;
    case SEAT_WHO: {
        int i = item->value;
        cycle(&seats[i].who, WHO_COUNT, back);
        if (seats[i].who == WHO_YOU) /* one of you: the start you leave goes to the computer */
            for (int j = 0; j < RF_MAX_STARTS; ++j)
                if (j != i && seats[j].who == WHO_YOU) seats[j].who = WHO_NORMAL;
        open_page(SKIRMISH);
        break;
    }
    case SEAT_FACTION: cycle(&seats[item->value].faction, 4, back); open_page(SKIRMISH); break;
    case SEAT_TEAM: cycle(&seats[item->value].team, 5, back); open_page(SKIRMISH); break;
    case SLOTS: /* Enter on the list */
    case ACCEPT: {
        menuitem_t *l = M_MenuFind(m, SLOTS);
        if (!l || l->value < 0 || l->value >= SAVE_SLOTS) break;
        char path[1200];
        slot_path(l->value, path, sizeof(path));
        if (page == SAVE) {
            time_t now = time(NULL);
            char when[16] = "";
            struct tm *tm = localtime(&now);
            if (tm) strftime(when, sizeof(when), "%d %b %H:%M", tm);
            const char *what = RF_InMission(NULL, NULL) ? RF_MissionTitle() : launch_title[0] ? launch_title : "Skirmish";
            snprintf(g_savename, sizeof(g_savename), "%.18s, %s", what, when);
            snprintf(g_savefile, sizeof(g_savefile), "%s", path);
            M_ClearMenus();
        } else if (slot_used[l->value]) {
            saveinfo_t checked;
            if (!G_SaveInfo(path, &checked)) { M_StartMessage("The saved game is damaged or from another version"); break; }
            /* The save names the map as the level did, under the data root; the driver joins the
             * root again, so a path under it goes back relative (G_LoadGame compares the two). */
            size_t rl = strlen(root_dir);
            const char *map = checked.map;
            if (!strncmp(map, root_dir, rl) && map[rl] == '/') map += rl + 1;
            snprintf(launch, sizeof(launch), "%s", map);
            snprintf(g_loadfile, sizeof(g_loadfile), "%s", path);
            return_page = MAIN;
            if (inlevel) menuleave = false;
            menumap = launch;
            M_ClearMenus();
        }
        break;
    }
    default: break;
    }
}

/* ── the engine's entry points ─────────────────────────────────────────── */

bool G_InitMenus(app_t *app, const char *root) {
    front_app = app;
    snprintf(root_dir, sizeof(root_dir), "%s", root && *root ? root : g_game_default_root);
    if (!initialized) {
        char path[1200];
        snprintf(path, sizeof(path), "%s/assets/title.pcx", root_dir);
        have_backdrop = W_LoadIndexedSheet(path, &backdrop);
        if (!have_backdrop) fprintf(stderr, "rustfront: cannot load %s; the menus have no backdrop\n", path);
        have_fonts = load_face(root_dir, "body", &body) && load_face(root_dir, "head", &head);
        if (!have_fonts) fprintf(stderr, "rustfront: cannot load the menu fonts in %s/assets\n", root_dir);
        default_seats();
        srand((unsigned)time(NULL));
        initialized = true;
    }
    return true;
}

menu_t *G_ControlPanel(app_t *app, bool in_level) {
    front_app = app;
    if (!initialized) G_InitMenus(app, NULL);
    inlevel = in_level;
    if (in_level) {
        open_page(PAUSE);
        return &menu;
    }
    int back = return_page;
    return_page = MAIN;
    if (back == CAMPAIGN) {
        campaign_mission = RF_CampaignWon(campaign_faction);
        if (campaign_mission >= RF_CAMPAIGN_MISSIONS) campaign_mission = RF_CAMPAIGN_MISSIONS - 1;
    }
    open_page(back);
    return &menu;
}

void G_ShutdownMenus(void) {
    if (have_backdrop) R_FreeSprite(&backdrop);
    if (have_fonts) R_FreeSprite(&body.sprite), R_FreeSprite(&head.sprite);
    have_backdrop = have_fonts = initialized = false;
    memset(items, 0, sizeof(items));
    menu.numitems = 0;
}

/* The face of the game's own text: HUD messages and mission messages. */
bool HU_LoadFont(const char *root, bitmapfont_t *font) {
    return load_face(root && *root ? root : g_game_default_root, "body", font);
}
