#define _DEFAULT_SOURCE
#include "engine.h"
#include "dark-colony.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

/* DC.EXE's screen scripts own layout; the engine runs the screens and the HUD.
 * Native control IDs and dispatch: docs/DC_EXE_FINDINGS.md, Main-menu screens. */
static char root[1024], leader[128], mapname[1024];
static int race;
static int bright_pushed, bright_highlight;
/* The native screen object has 300 controls (0x34 bytes each at +0x88); an
 * item's index is its native control ID. Prose and the PIC globe are extra
 * items outside the native screen controls. */
enum { NUMCONTROLS = 300, PROSE = NUMCONTROLS, GLOBE, NUMITEMS };
static menuitem_t items[NUMITEMS];
static bool initialized, training;
static void menu_escape(menu_t *screen);
static int gadget_tics(const menuitem_t *item);
static void refresh(menu_t *screen);
static void menu_ticker(menu_t *screen);
static menu_t menu = {.items = items, .numitems = NUMITEMS, .modal = true, .escape = menu_escape,
                      .refresh = refresh, .ticker = menu_ticker, .frametics = gadget_tics};
static uint64_t menutime, globetime;
static const char *notice;
static char mission_title[128], mission_region[128];
static char *prose;
static enum { MAIN, SETUP, STORY, BRIEFING, SKIRMISH, QUIT, NETWORK, BROWSE, CONNECT,
              OPTIONS, OBJECTIVES, SAVE, LOAD } page;
static gamesettings_t editing;
static int editing_speed;
typedef struct { char path[1200]; dc_saveinfo_t info; } saveentry_t;
static saveentry_t *saves;
static int numsaves;
/* client: a joiner in the host's lobby. DC.EXE's join path 0x405670 enters
 * the same MULTI lobby 0x40fb20 as the host (both call 0x401210 with a
 * lobby argument); host-only controls assert player_number == 0 (0x410a53). */
static bool lan, waiting, client;
/* Ready checks 16..23 (DC.EXE message 'h', 0x41e3bc): control 133 toggles
 * the local player's own. A ready slot is locked (0x410a32). The match
 * starts once every human is ready; the engine applies that rule. */
static bool lobby_ready[8];
static char lobby_log[NETCHAT_LENGTH * 33 + 256];
static char server_address[128] = "127.0.0.1";
static char network_notice[128], selected_server[64];
static bitmapfont_t fonts[3];
static spritesheet_t background;
static spritesheet_t pictures;
static spritecache_t images;

/* A gadget is an item that plays one FIN sequence: userdata is its FIN and
 * the engine steps anim through the sequence's frames. */
static dc_fin_t animations[16];
static int numanimations;
/* A banim control (native type 12, button.c create_banim 0x4250bc) lists the
 * entrance gadgets and the push buttons they cover. 0x425214 plays the gadgets
 * one by one when the screen opens and hides each finished gadget so the
 * button beneath shows; hovering a button only brightens it (0x424638). */
typedef struct {
    int gadgets[300];
    int count, started, finished;
} menuentrance_t;
static menuentrance_t entrances[4];
static int numentrances, entrance;
static char messages[300][128];
static dc_skirmish_t skirmish;
typedef struct {
    char path[128], title[128], label[256];
    int players;
} skirmishmap_t;
static skirmishmap_t *maps;
static int nummaps, selectedmap = -1;

static void refresh_skirmish(void);
static bool load_skirmish_maps(void);
static void network_failure(void);

static bool screen_palette(spritesheet_t *sprite, const blob_t *rmp) {
    if (!sprite->numlumps) return true;
    memcpy(sprite->palette, background.source_palette, sizeof(sprite->palette));
    sprite->palette[0] = 0;
    memcpy(sprite->source_palette, sprite->palette, sizeof(sprite->palette));
    free(sprite->palette_maps);
    sprite->palette_map_count = 0;
    sprite->palette_maps = calloc(256, sizeof(*sprite->palette_maps));
    if (!sprite->palette_maps) return false;
    sprite->palette_map_count = 256;
    for (int i = 0; i < 256; ++i) {
        sprite->palette_maps[i].id = i;
        memcpy(sprite->palette_maps[i].indices, rmp->bytes + i * 256, 256);
    }
    return true;
}

static void free_screen(void) {
    for (int i = 0; i < 3; ++i) HU_FreeFont(&fonts[i]);
    R_FreeSprite(&background);
    R_FreeSprite(&pictures);
    R_FreeSpriteCache(&images);
    for (int i = 0; i < numanimations; ++i) DC_FreeFIN(&animations[i]);
    numanimations = 0;
    free(prose);
    prose = NULL;
    free(maps);
    maps = NULL;
    nummaps = 0;
    free(saves);
    saves = NULL;
    numsaves = 0;
    memset(items, 0, sizeof(items));
    memset(messages, 0, sizeof(messages));
    numentrances = entrance = 0;
    bright_pushed = bright_highlight = 0;
    menu.held = NULL;
}

void G_ShutdownMenus(void) {
    if (waiting || page == BROWSE) M_NetStop();
    waiting = false;
    free_screen();
    initialized = false;
}

static const spritesheet_t *load_image(const char *name) {
    const spritesheet_t *cached = R_CacheLookup(&images, name);
    if (cached) return cached;
    if (images.count == MAX_DECORATION_SPRITES) return NULL;
    cachedsprite_t *image = &images.entries[images.count];
    char path[1024];
    M_PathJoin(path, sizeof(path), root, name);
    if (!DC_LoadSpriteImage(path, &image->sprite)) return NULL;
    snprintf(image->name, sizeof(image->name), "%s", name);
    ++images.count;
    return &image->sprite;
}

static bool load_animations(const char *name) {
    char path[1024], entry[128];
    M_PathJoin(path, sizeof(path), root, name);
    FILE *file = fopen(path, "r");
    if (!file) return false;
    bool ok = true;
    while (fscanf(file, "%127s", entry) == 1) {
        if (numanimations == 16) { ok = false; break; }
        dc_fin_t *fin = &animations[numanimations++];
        M_PathJoin(path, sizeof(path), root, M_va("ANIMATE/%s", M_Upper(entry)));
        if (!DC_LoadFIN(path, fin)) { ok = false; break; }
        for (int i = 0; i < fin->header->dependency_count; ++i) {
            char key[32];
            snprintf(key, sizeof(key), "SPRITES/%.8s.SPR", fin->dependencies[i].name);
            M_Upper(key);
            if (!load_image(key)) { ok = false; break; }
        }
        if (!ok) break;
    }
    if (ferror(file)) ok = false;
    fclose(file);
    return ok;
}

static void animate(int id, menuanimmode_t mode) {
    if (items[id].userdata) M_MenuAnimate(&items[id], mode);
}

/* 0x4230ac: a FIN frame lasts this many menu ticks; a zero count means 15. */
static int gadget_tics(const menuitem_t *item) {
    const dc_fin_t *fin = item->userdata;
    int raw = fin->frames[item->anim.frame].ticks;
    return ((raw ? raw : 15) + 3) * 15 / 100;
}

static bool read_text(const char *name) {
    char path[1024];
    if (name[0] == '/') snprintf(path, sizeof(path), "%s", name);
    else M_PathJoin(path, sizeof(path), root, name);
    blob_t file;
    if (!W_ReadFile(path, &file)) return false;
    prose = malloc(file.size + 1);
    if (!prose) { W_FreeFile(&file); return false; }
    /* ~digit is a native text colour command, not displayed text. */
    size_t n = 0;
    for (size_t i = 0; i < file.size; ++i) {
        if (file.bytes[i] == '~' && i + 1 < file.size && isdigit(file.bytes[i + 1])) { ++i; continue; }
        if (file.bytes[i] != '\r') prose[n++] = (char)file.bytes[i];
    }
    prose[n] = '\0';
    W_FreeFile(&file);
    return true;
}

static int compare_maps(const void *a, const void *b) {
    const skirmishmap_t *left = a, *right = b;
    int order = strcmp(left->label, right->label);
    return order ? order : strcmp(left->path, right->path);
}

static bool load_skirmish_maps(void) {
    char directory[1024];
    M_PathJoin(directory, sizeof(directory), root, "SCENARIO/MPLAYER");
    DIR *dir = opendir(directory);
    if (!dir) return false;
    struct dirent *entry;
    bool ok = true;
    while ((entry = readdir(dir))) {
        const char *extension = strrchr(entry->d_name, '.');
        if (!extension || strcasecmp(extension, ".SCN")) continue;
        char path[1024], terrain[128], line[256];
        M_PathJoin(path, sizeof(path), directory, entry->d_name);
        FILE *file = fopen(path, "r");
        if (!file) { ok = false; break; }
        skirmishmap_t map = {0};
        int mode;
        bool valid = fgets(terrain, sizeof(terrain), file) &&
            fgets(line, sizeof(line), file) && fgets(map.title, sizeof(map.title), file) &&
            fgets(line, sizeof(line), file) && sscanf(line, "%d %d", &mode, &map.players) == 2;
        fclose(file);
        if (!valid || map.players < 1 || map.players > 8) continue;
        terrain[strcspn(terrain, ".\r\n")] = '\0';
        map.title[strcspn(map.title, "\r\n")] = '\0';
        const char *description = !strcasecmp(terrain, "desert") ? "Desert Map " :
            !strcasecmp(terrain, "jungle") ? "Jungle Map " :
            !strcasecmp(terrain, "atlantis") ? "Underground" : terrain;
        snprintf(map.label, sizeof(map.label), "%-43.43s (%d Player %s)", map.title, map.players, description);
        snprintf(map.path, sizeof(map.path), "SCENARIO/MPLAYER/%.*s.MAP",
                 (int)(extension - entry->d_name), entry->d_name);
        M_PathJoin(path, sizeof(path), root, map.path);
        file = fopen(path, "rb");
        /* The level loader also resolves logical .MAP names to retail .MTG. */
        if (!file) {
            strcpy(strrchr(path, '.'), ".MTG");
            file = fopen(path, "rb");
        }
        if (!file) continue;
        fclose(file);
        skirmishmap_t *grown = realloc(maps, (size_t)(nummaps + 1) * sizeof(*maps));
        if (!grown) { ok = false; break; }
        maps = grown;
        maps[nummaps++] = map;
    }
    closedir(dir);
    if (nummaps) qsort(maps, nummaps, sizeof(*maps), compare_maps);
    return ok && nummaps > 0;
}

static int active_players(void) {
    int count = 0;
    for (int i = 0; i < 8; ++i) count += skirmish.players[i].type != DC_PLAYER_NONE;
    return count;
}

static int filtered_map(int row) {
    int count = active_players();
    for (int i = 0; i < nummaps; ++i)
        if (maps[i].players >= count && row-- == 0) return i;
    return -1;
}

static void gadget_pose(int id, int pose) {
    menuanim_t *anim = &items[id].anim;
    if (items[id].userdata && pose >= 0 && anim->first + pose <= anim->last) {
        anim->frame = anim->first + pose;
        anim->mode = MANIM_STOPPED;
    }
}

/* The engine owns the lobby record. These copy Dark Colony's screen into it
 * and back: a seat is race, type, colour and team; the eight option bytes are
 * the skirmish rules. */
static bool dc_net_bad;

static const char *dc_chat_name(void) {
    return client ? M_va("Player %d", doomcom->consoleplayer + 1) : "Host";
}

static void push_lobby(void) {
    for (int i = 0; i < 8; ++i) {
        const dc_skirmish_player_t *p = &skirmish.players[i];
        netseat_t seat = {.race = (uint8_t)p->race, .type = (uint8_t)p->type,
                          .color = (uint8_t)p->color, .team = (uint8_t)p->team, .ready = lobby_ready[i]};
        M_NetSetSeat(i, &seat);
    }
    uint8_t opt[8] = {(uint8_t)skirmish.storage, (uint8_t)skirmish.artifacts, (uint8_t)skirmish.erupting,
                      (uint8_t)skirmish.renewable, (uint8_t)skirmish.flow, (uint8_t)skirmish.quantity,
                      (uint8_t)skirmish.rank, skirmish.seed};
    M_NetSetOptions(opt, sizeof(opt));
}

static bool pull_lobby(void) {
    uint8_t opt[16];
    if (M_NetOptions(opt, sizeof(opt)) != sizeof(opt)) return true;
    if (opt[4] < 1 || opt[4] > 20 || opt[5] < 1 || opt[5] > 20 || opt[6] > 3) return false;
    dc_skirmish_t shared = {.storage = opt[0], .artifacts = opt[1], .erupting = opt[2], .renewable = opt[3],
                            .flow = opt[4], .quantity = opt[5], .rank = opt[6], .seed = opt[7]};
    for (int i = 0; i < 8; ++i) {
        const netseat_t *seat = M_NetSeat(i);
        if (!seat || seat->race > 1 || seat->type > DC_PLAYER_NONE || seat->color > 7 || seat->team > 7) return false;
        shared.players[i] = (dc_skirmish_player_t){.race = seat->race, .type = seat->type,
                                                   .color = seat->color, .team = seat->team};
        snprintf(shared.players[i].name, sizeof(shared.players[i].name), "%s", i ? "LAN Player" : "Host");
        lobby_ready[i] = seat->ready;
    }
    skirmish = shared;
    const char *map = I_NetMap();
    if (!map || !map[0]) map = M_NetLaunchMap();
    selectedmap = -1;
    for (int i = 0; map && i < nummaps; ++i)
        if (!strcmp(maps[i].path, map)) selectedmap = i;
    return true;
}

static void dc_net_commit(void) {
    dc_net_bad = !pull_lobby() || M_NetOptions(&(uint8_t){0}, 1) == 0;
    if (!dc_net_bad) DC_RequestSkirmish(M_NetLaunchMap(), &skirmish);
}

static void use_net(void) {
    netplay_t net = {.max_players = 4, .race_count = 2, .joiner_fields = NET_FIELD_RACE,
                     .commit = dc_net_commit, .chat_name = dc_chat_name};
    M_NetUse(&net);
    dc_net_bad = false;
}

static int consoleplayer_slot(void) { return client ? doomcom->consoleplayer : 0; }

static void refresh_skirmish(void) {
    for (int i = 0; i < 8; ++i) {
        const dc_skirmish_player_t *p = &skirmish.players[i];
        snprintf(items[i].text, sizeof(items[i].text), "%s", p->type == DC_PLAYER_HUMAN ? p->name : "");
        strcpy(items[72 + i].text, messages[52 + p->type]);
        strcpy(items[80 + i].text, messages[50 + p->race]);
        gadget_pose(8 + i, p->race);
        gadget_pose(88 + i, p->type);
        gadget_pose(96 + i, p->color * 2);
        gadget_pose(142 + i, p->team * 2);
    }
    snprintf(items[121].text, sizeof(items[121].text), "%d%%", skirmish.quantity * 25);
    snprintf(items[125].text, sizeof(items[125].text), "%d%%", skirmish.flow * 25);
    strcpy(items[129].text, messages[(skirmish.players[consoleplayer_slot()].race ? 40 : 30) + skirmish.rank]);
    if (selectedmap >= 0 && maps[selectedmap].players < active_players()) selectedmap = -1;
    snprintf(items[26].text, sizeof(items[26].text), "%s", selectedmap < 0 ? "" : maps[selectedmap].title);
    if (lan) {
        int me = consoleplayer_slot();
        for (int i = 0; i < 8; ++i) {
            if (i) snprintf(items[i].text, sizeof(items[i].text), "%s",
                            i >= active_players() ? "" : client && i == consoleplayer_slot() ? "You" :
                            waiting && i >= I_NetPlayerCount() ? "Open" : "LAN Player");
            items[16 + i].visible = waiting && i < active_players();
            items[16 + i].value = lobby_ready[i];
            items[180 + i].visible = false;
            items[8 + i].visible = items[80 + i].visible = i < active_players();
            items[96 + i].visible = items[142 + i].visible = i < active_players();
            items[32 + i].visible = items[40 + i].visible = i < active_players();
            items[150 + i].visible = items[158 + i].visible = i < active_players();
        }
        items[137].visible = items[139].visible = items[140].visible = items[141].visible = false;
        snprintf(items[133].text, sizeof(items[133].text), "%s",
                 !waiting ? "CREATE" : lobby_ready[me] ? "UNREADY" : "READY");
        /* Chat window 24 shows the log above the lobby status; 25 is the
         * input line (DC.EXE sends "<name>: <text>" as message 'e'). */
        const char *status = network_notice[0] ? network_notice : client ?
            "Choose your race, then READY.\nThe game starts when everyone is ready." :
            "Select map and 2-4 LAN player slots.\nPlayers choose their own race.\nThe game starts when everyone is ready.";
        const char *log = waiting ? M_NetLog() : "";
        size_t used = (size_t)snprintf(lobby_log, sizeof(lobby_log), "%s", log);
        snprintf(lobby_log + used, sizeof(lobby_log) - used, "%s", status);
        menuitem_t *chat = &items[24];
        chat->text[0] = '\0';
        chat->prose = lobby_log;
        int lines = V_TextWrappedHeight(chat->rect.w, chat->font, lobby_log) / chat->font->line_h;
        int shown = chat->rect.h / chat->font->line_h;
        chat->first_row = lines > shown ? lines - shown : 0; /* Newest at the bottom. */
        items[25].visible = waiting;
    }
}

static void send_chat(void) {
    if (!items[25].text[0]) return;
    if (M_NetChat(items[25].text)) items[25].text[0] = '\0';
    else notice = "Chat is busy; try again";
}

static void activate_skirmish(int id) {
    if (id >= 8 && id < 16) skirmish.players[id - 8].race ^= 1;
    else if (id >= 89 && id < 96) {
        dc_skirmish_player_t *p = &skirmish.players[id - 88];
        p->type = p->type == DC_PLAYER_NONE ? DC_PLAYER_AI :
            p->type == DC_PLAYER_AI ? DC_PLAYER_AI_PLUS : DC_PLAYER_NONE;
        /* The native host reserves human colours, then assigns AI colours
         * in slot order to the first free colour (0x4103fe..0x41050c). */
        bool used[8] = {false};
        used[skirmish.players[0].color] = true;
        for (int i = 1; i < 8; ++i) {
            dc_skirmish_player_t *ai = &skirmish.players[i];
            ai->color = 0;
            if (ai->type == DC_PLAYER_NONE) continue;
            for (int j = 0; j < 8; ++j) {
                int color = (i + j) % 8;
                if (!used[color]) { ai->color = color; used[color] = true; break; }
            }
        }
        items[27].first_row = 0;
    } else if ((id >= 32 && id < 48) && (id % 8 == 0 || lan)) {
        int slot = id & 7, oldcolor = skirmish.players[slot].color;
        int newcolor = (oldcolor + (id < 40 ? 1 : 7)) % 8;
        for (int i = 0; i < 8; ++i)
            if (i != slot && skirmish.players[i].type != DC_PLAYER_NONE && skirmish.players[i].color == newcolor)
                skirmish.players[i].color = oldcolor;
        skirmish.players[slot].color = newcolor;
    } else if (id >= 150 && id <= 165) {
        int player = id < 158 ? id - 150 : id - 158;
        skirmish.players[player].team = (skirmish.players[player].team + (id < 158 ? 1 : 7)) % 8;
    } else if (id >= 105 && id <= 108) skirmish.storage = id - 105;
    else if (id >= 110 && id <= 113) skirmish.artifacts = id - 110;
    else if (id == 115 || id == 116) skirmish.erupting = id - 115;
    else if (id == 118 || id == 119) skirmish.renewable = id - 118;
    else if (id >= 122 && id <= 127 && id != 124 && id != 125) {
        int *value = id < 124 ? &skirmish.quantity : &skirmish.flow;
        *value += id & 1 ? 1 : -1;
        if (*value < 1) *value = 1;
        if (*value > 20) *value = 20;
    } else if (id == 130 && skirmish.rank > 0) --skirmish.rank;
    else if (id == 131 && skirmish.rank < 3) ++skirmish.rank;
    else if ((id == 133 || id == 16) && selectedmap >= 0) {
        skirmish.seed = (uint8_t)SDL_GetTicks();
        snprintf(mapname, sizeof(mapname), "%s", maps[selectedmap].path);
        DC_RequestSkirmish(mapname, &skirmish);
        menumap = mapname;
        M_ClearMenus();
        SDL_StopTextInput();
    }
    refresh_skirmish();
}

/* Screens start their decorative gadgets only after the banim entrance:
 * 0x404b8c..0x404b9c (INTROE DCSS logo), 0x401f26..0x401f70 (NEWGAMEE),
 * 0x403340..0x403386 (SHUMANE). */
static void start_page_animations(void) {
    if (page == MAIN) animate(14, MANIM_ONCE);
    if (page == NETWORK) {
        /* NETOPTE constructor 0x405809..0x405828: loop gadgets 14..18. */
        for (int i = 14; i <= 18; ++i) animate(i, MANIM_LOOP);
        items[GLOBE].visible = true;
        globetime = SDL_GetTicks64();
    }
    if (page == SETUP) {
        animate(race ? 26 : 23, MANIM_LOOP);
        for (int i = 13; i <= 16; ++i) animate(i, MANIM_LOOP);
        for (int i = 29; i <= 35; ++i) animate(i, MANIM_LOOP);
    }
    if (page == BRIEFING) {
        for (int i = 15; i <= 24; ++i) animate(i, MANIM_LOOP);
        animate(39, MANIM_LOOP);
        animate(race ? 35 : 36, MANIM_LOOP);
    }
}

void DC_ControlLooks(menuitem_t *item, int normal, int pushed, int remap,
                     int pushed_light, int highlight) {
    for (int state = 0; state < MS_STATES; ++state) {
        bool down = state == MS_PUSHED;
        int frame = down ? pushed : normal;
        int light = (frame < 0 ? -frame : 16) +
            (down ? pushed_light : state == MS_FOCUS ? highlight : 0);
        item->look[state].cell = frame < 0 ? (down ? normal : pushed) : frame;
        item->look[state].palette = (light > 31 ? 31 : light) * 8 + remap;
    }
}

static void draw_gadget(const menu_t *screen, const menuitem_t *item, irect_t rect) {
    (void)screen;
    const dc_fin_t *fin = item->userdata;
    const dc_fin_frame_t *frame = &fin->frames[item->anim.frame];
    const dc_fin_command_t *parts = fin->frame_commands[item->anim.frame];
    for (int i = 0; i < frame->part_count; ++i) {
        const dc_fin_command_t *part = &parts[i];
        char key[32];
        snprintf(key, sizeof(key), "SPRITES/%.8s.SPR", part->sprite);
        const spritesheet_t *sprite = R_CacheLookup(&images, M_Upper(key));
        if (!sprite || part->cell < 0 || part->cell >= sprite->numlumps) continue;
        const spritecell_t *cell = &sprite->cells[part->cell];
        /* 0x4224d7..0x4224ef passes only the dependency and cell to the UI
         * blitter. World FIN offsets/flags do not position menu gadgets. */
        irect_t dst = {rect.x + cell->displacement.x,
                        rect.y + cell->displacement.y, cell->rect.w, cell->rect.h};
        R_DrawSprite(sprite, part->cell, -1, &cell->rect, &dst, 0, 16);
    }
}

static void draw_globe(const menu_t *screen, const menuitem_t *item, irect_t rect) {
    (void)screen;
    const spritecell_t *cell = &item->sheet->cells[item->anim.frame];
    irect_t dst = {rect.x + cell->displacement.x,
                  rect.y + cell->displacement.y, cell->rect.w, cell->rect.h};
    R_DrawSprite(item->sheet, item->anim.frame, -1, &cell->rect, &dst, 0, 16);
}

static void menu_routine(menu_t *screen, menuitem_t *item, menuaction_t action);
static const char *map_row(const menuitem_t *item, int row);
static const char *session_row(const menuitem_t *item, int row);

static bool popup(void) {
    return page == QUIT || page == OPTIONS || page == OBJECTIVES || page == SAVE;
}

static const char *save_row(const menuitem_t *item, int row) {
    (void)item;
    return row >= 0 && row < numsaves ? saves[row].info.name : "";
}

static int compare_saves(const void *a, const void *b) {
    return strcmp(((const saveentry_t *)a)->info.name, ((const saveentry_t *)b)->info.name);
}

static bool load_saves(void) {
    DIR *dir = opendir(D_UserDirectory());
    if (!dir) return false;
    struct dirent *entry;
    bool ok = true;
    while ((entry = readdir(dir))) {
        size_t n = strlen(entry->d_name);
        if (n < 5 || strcmp(entry->d_name + n - 4, ".sav")) continue;
        saveentry_t save;
        M_PathJoin(save.path, sizeof(save.path), D_UserDirectory(), entry->d_name);
        if (!DC_SaveInfo(save.path, &save.info)) continue;
        saveentry_t *added = realloc(saves, (size_t)(numsaves + 1) * sizeof(*saves));
        if (!added) { ok = false; break; }
        saves = added;
        saves[numsaves++] = save;
    }
    closedir(dir);
    if (numsaves) qsort(saves, numsaves, sizeof(*saves), compare_saves);
    return ok;
}

static void draw_objectives(const menu_t *screen, const menuitem_t *item, irect_t rect) {
    (void)screen;
    V_DrawTextWrapped(rect, item->font, prose,
                     R_PaletteMap(&item->font->sprite, item->look[MS_NORMAL].palette),
                     item->first_row * item->row_height);
}

static void option_values(void) {
    snprintf(items[46].text, sizeof(items[46].text), "%d%%", editing_speed);
    snprintf(items[47].text, sizeof(items[47].text), "%d", editing.sound);
    snprintf(items[69].text, sizeof(items[69].text), "%d", editing.music);
    strcpy(items[48].text, messages[10 + editing.detail]);
}

static bool load_screen(int next) {
    static const char *const scripts[] = {"INTROE", "NEWGAMEE", "STORYE", "SHUMANE", "MULTIE", "LQCE", "NETOPTE", "DPLAYSE", "GETSVRE", "LOPTE", "LOBJE", "LSGE", "LOADGE"};
    static const char *const lists[] = {"INTRO.DAT", "CHOO.DAT", "LOADG.DAT", "SHUMAN.DAT", "TCPWAIT.DAT", NULL, "NET.DAT", "LOADG.DAT", "SERVER.DAT", NULL, NULL, NULL, "LOADG.DAT"};
    free_screen();
    page = next;
    menu.itemOn = page == STORY ? 5 : page == BRIEFING ? 2 : 0;
    notice = NULL;
    char path[1024], line[512], name[128], palette_path[1024] = "";
    if (lists[page] && !load_animations(M_va("INTRFACE/%s", lists[page]))) return false;
    if (page == NETWORK) {
        const spritesheet_t *globe = load_image("INTRFACE/BLEW.SPR");
        if (!globe || globe->numlumps < 2) return false;
        /* PIC window 0x40578b..0x4057a6, outside NETOPTE's controls. */
        items[GLOBE] = (menuitem_t){.sheet = globe, .rect = {336, 24, 0, 0},
            .anim = {.frame = 1}, .ownerdraw = draw_globe};
        for (int i = 0; i < MS_STATES; ++i) items[GLOBE].look[i].cell = -1;
    }
    if (popup()) {
        spritesheet_t palette = {0};
        M_PathJoin(path, sizeof(path), root, "PALETTE.GIF");
        if (!W_LoadGIFTexture(path, &palette)) return false;
        memcpy(background.source_palette, palette.source_palette, sizeof(background.source_palette));
        R_FreeSprite(&palette);
        M_PathJoin(palette_path, sizeof(palette_path), root, "PALETTE.RMP");
    }
    M_PathJoin(path, sizeof(path), root, M_va("INTRFACE/%s", scripts[page]));
    FILE *file = fopen(path, "r");
    if (!file) return false;
    /* Script fields that need the fonts, messages and brightness lines, which
     * may follow the control that uses them. */
    enum { PUSH = 1, CHECK, LABEL, TEXT, GADGET, PICTURE, LIST, SCROLL };
    typedef struct { int kind, message, font, remap, normal, pushed; } scriptcontrol_t;
    scriptcontrol_t controls[NUMCONTROLS] = {0};
    struct { char name[16]; uint32_t argb; } colours[8];
    int numcolours = 0;
    isize2_t screen = {0};
    bool ok = true;
    while (fgets(line, sizeof(line), file)) {
        char kind[32];
        int id, desc, red, green, blue;
        irect_t rect;
        if (sscanf(line, "size %d %d %d %d", &rect.x, &rect.y, &rect.w, &rect.h) == 4) {
            screen = (isize2_t){640, 480};
            continue;
        }
        if (sscanf(line, "size %d %d", &screen.w, &screen.h) == 2) continue;
        if (sscanf(line, "bright_pushed %d", &bright_pushed) == 1 ||
            sscanf(line, "bright_highlight %d", &bright_highlight) == 1) continue;
        if (sscanf(line, "colour %15s %d %d %d", name, &red, &green, &blue) == 4) {
            if (numcolours == 8) { ok = false; break; }
            snprintf(colours[numcolours].name, sizeof(colours[numcolours].name), "%s", name);
            colours[numcolours++].argb = 0xff000000u | (uint32_t)red << 16 |
                (uint32_t)green << 8 | (uint32_t)blue;
        } else if (sscanf(line, "background %127s", name) == 1) {
            M_PathJoin(path, sizeof(path), root, M_va("%s.GIF", M_Upper(name)));
            if (!W_LoadGIFTexture(path, &background)) ok = false;
            M_PathJoin(palette_path, sizeof(palette_path), root, M_va("%s.RMP", M_Upper(name)));
        } else if (sscanf(line, "pictures %127s", name) == 1) {
            M_PathJoin(path, sizeof(path), root, M_va("%s.SPR", M_Upper(name)));
            if (!DC_LoadSpriteImage(path, &pictures)) ok = false;
        } else if (sscanf(line, "font %d %127s", &id, name) == 2) {
            if (id < 0 || id >= 3 || !DC_LoadFont(root,
                    M_va("%s.SPR", M_Upper(name)), &fonts[id])) ok = false;
        } else if (sscanf(line, "textmsg %d %127[^\r\n]", &id, name) == 2) {
            if (id < 0 || id >= NUMCONTROLS) { ok = false; break; }
            strcpy(messages[id], name);
        } else if (sscanf(line, "%31s %d %d %d %d %d %d", kind, &id, &desc,
                           &rect.x, &rect.y, &rect.w, &rect.h) == 7 &&
                   (!strcmp(kind, "pushb") || !strcmp(kind, "checkb") ||
                    !strcmp(kind, "label") || !strcmp(kind, "in_text") || !strcmp(kind, "gadget") ||
                    !strcmp(kind, "picture") || !strcmp(kind, "list") || !strcmp(kind, "scroll"))) {
            if (id < 0 || id >= NUMCONTROLS || rect.w <= 0 || rect.h <= 0) { ok = false; break; }
            menuitem_t *item = &items[id];
            *item = (menuitem_t){.rect = rect, .visible = true, .link = -1,
                                 .routine = menu_routine};
            controls[id] = (scriptcontrol_t){.remap = 7, .normal = -1, .pushed = -1};
            int script = controls[id].kind = !strcmp(kind, "pushb") ? PUSH :
                !strcmp(kind, "checkb") ? CHECK : !strcmp(kind, "label") ? LABEL :
                !strcmp(kind, "in_text") ? TEXT : !strcmp(kind, "picture") ? PICTURE :
                !strcmp(kind, "list") ? LIST : !strcmp(kind, "scroll") ? SCROLL : GADGET;
            item->kind = script == PUSH ? MI_BUTTON : script == CHECK ? MI_CHECK :
                script == LIST ? MI_LIST : script == SCROLL ? MI_SCROLLBAR : MI_STATIC;
            if (script == PUSH || script == CHECK || script == PICTURE)
                sscanf(line, "%*s %*d %*d %*d %*d %*d %*d %d %d", &controls[id].normal, &controls[id].pushed);
            if (strstr(line, "align centre") || strstr(line, "align  centre")) item->align = MALIGN_CENTER;
            char *label = strstr(line, "label centre ");
            if (label) {
                sscanf(label, "label centre %d %d", &controls[id].message, &controls[id].font);
                item->align = MALIGN_CENTER;
            } else if ((label = strstr(line, " label "))) sscanf(label, " label %d", &controls[id].message);
            if ((label = strstr(line, "remap "))) sscanf(label, "remap %d", &controls[id].remap);
            if ((label = strstr(line, " font "))) sscanf(label, " font %d", &controls[id].font);
            if (script == TEXT) {
                item->maxchars = rect.w;
                sscanf(line, "%*s %*d %*d %*d %*d %*d %*d %d", &controls[id].font);
                if ((label = strstr(line, " init "))) sscanf(label, " init %d", &controls[id].message);
            }
            if (controls[id].message < 0 || controls[id].message >= NUMCONTROLS ||
                controls[id].font < 0 || controls[id].font >= 3) { ok = false; break; }
            if (script == GADGET) {
                char animation[32] = "";
                sscanf(line, "%*s %*d %*d %*d %*d %*d %*d %31s", animation);
                const dc_fin_label_t *sequence = NULL;
                for (int i = 0; i < numanimations && !sequence; ++i) {
                    item->userdata = &animations[i];
                    sequence = DC_FINLabel(&animations[i], animation);
                }
                if (!sequence) { ok = false; break; }
                item->anim.first = sequence->start;
                item->anim.last = sequence->end;
                animate(id, strstr(line, "anim_oneoff") ? MANIM_ONCE :
                            strstr(line, "anim_loop") ? MANIM_LOOP : MANIM_STOPPED);
                if (strstr(line, "read_write")) item->kind = MI_BUTTON;
                item->ownerdraw = draw_gadget;
            }
            /* list N ... selbg <colour>; scroll N ... list <id>;
             * pushb N ... list <id> <rows>. */
            if (script == LIST && (label = strstr(line, " selbg ")) &&
                sscanf(label, " selbg %15s", name) == 1)
                for (int i = 0; i < numcolours; ++i)
                    if (!strcmp(colours[i].name, name)) item->color = colours[i].argb;
            if ((script == SCROLL || script == PUSH) && (label = strstr(line, " list ")) &&
                sscanf(label, " list %d %d", &item->link, &item->step) >= 1) {
                if (item->link < 0 || item->link >= NUMCONTROLS) { ok = false; break; }
                if (script == SCROLL) {
                    item->fill = 0xff000000u;
                    item->color = 0xffff0000u; /* Native default colour 1. */
                }
            }
        } else if (!strncmp(line, "banim", 5)) {
            /* banim id desc ngadgets nbuttons gadget... button...: the gadgets
             * play in list order. Buttons are always drawn beneath their
             * gadget here, so only the gadget list is retained. */
            int values[300], count = 0;
            char *cursor = line + 5, *end;
            while (count < 300) {
                long value = strtol(cursor, &end, 10);
                if (end == cursor) break;
                if (value < 0 || value >= NUMCONTROLS) { ok = false; break; }
                values[count++] = (int)value;
                cursor = end;
            }
            if (count < 4 || values[2] <= 0 || values[3] < 0 || count != 4 + values[2] + values[3] ||
                numentrances == 4) { ok = false; break; }
            menuentrance_t *added = &entrances[numentrances++];
            added->count = values[2];
            added->started = added->finished = 0;
            memcpy(added->gadgets, values + 4, (size_t)values[2] * sizeof(int));
        }
    }
    if (ferror(file)) ok = false;
    fclose(file);
    blob_t rmp;
    if (!W_ReadFile(palette_path, &rmp)) return false;
    if (rmp.size < 0x10000) { W_FreeFile(&rmp); return false; }
    ok = screen_palette(&pictures, &rmp) && ok;
    for (int i = 0; i < 3; ++i) ok = screen_palette(&fonts[i].sprite, &rmp) && ok;
    for (int i = 0; i < images.count; ++i) ok = screen_palette(&images.entries[i].sprite, &rmp) && ok;
    W_FreeFile(&rmp);
    for (int i = 0; i < NUMCONTROLS; ++i) {
        menuitem_t *item = &items[i];
        int script = controls[i].kind;
        const bitmapfont_t *font = item->font = &fonts[controls[i].font];
        if (controls[i].message) strcpy(item->text, messages[controls[i].message]);
        if (script == TEXT) {
            item->rect.w *= font->glyph_size.w + 1;
            item->rect.h *= font->line_h;
        }
        /* Centred text sits half a glyph right, except in text fields; a
         * left-aligned label is inset one glyph and centred vertically. */
        if (item->align) item->inset.x = script == TEXT ? 0 : (font->glyph_size.w + 1) / 2;
        else if (script == LABEL)
            item->inset = (ivec2_t){font->glyph_size.w, (item->rect.h - font->glyph_size.h) / 2};
        /* Only buttons and pictures have frames; other text is at full light. */
        item->sheet = &pictures;
        if (script == PUSH || script == CHECK || script == PICTURE)
            DC_ControlLooks(item, controls[i].normal, controls[i].pushed, controls[i].remap,
                            bright_pushed, bright_highlight);
        else
            for (int state = 0; state < MS_STATES; ++state)
                item->look[state] = (menulook_t){.cell = -1, .palette = 16 * 8 + controls[i].remap};
        if (script == LIST) {
            item->row_height = font->glyph_size.h;
            item->look[MS_PUSHED].palette = controls[i].remap;
        }
    }
    menu.background = &background;
    menu.palette = background.source_palette;
    if (page == SETUP) {
        items[5].kind = MI_TEXTFIELD;
        items[training ? 3 : 2].visible = false;
        items[19].visible = items[20].visible = false;
        for (int i = 21; i <= 26; ++i) items[i].visible = false;
        items[race ? 26 : 23].visible = true;
        snprintf(items[5].text, sizeof(items[5].text), "%s", leader);
        items[0].group = items[1].group = 1;
        items[race].value = 1;
    }
    /* Text viewport arguments at 0x4023f8/0x403030, separate from widgets.
     * Its two arrow buttons scroll it a line at a time. */
    if (page == STORY || page == BRIEFING) {
        items[PROSE] = (menuitem_t){.visible = true, .font = &fonts[0],
            .rect = page == STORY ? (irect_t){10, 13, 579, 420} : (irect_t){310, 212, 294, 225}};
        menuitem_t *up = &items[page == STORY ? 2 : 4], *down = &items[3];
        up->link = down->link = PROSE;
        up->step = -1;
        down->step = 1;
    }
    if (page == STORY && !read_text(race ? "INTRFACE/ASTORY.TXT" : "INTRFACE/HSTORY.TXT")) ok = false;
    if (page == BRIEFING) {
        items[10].visible = false;
        items[race ? 36 : 35].visible = false;
        snprintf(items[5].text, sizeof(items[5].text), "%s", leader);
        snprintf(items[6].text, sizeof(items[6].text), "%s", mission_title);
        snprintf(items[7].text, sizeof(items[7].text), "%s", mission_region);
        if (!read_text(M_va("%.*s.TXT", (int)strlen(mapname) - 4, mapname))) ok = false;
    }
    items[PROSE].prose = prose;
    if (page == SKIRMISH) {
        selectedmap = -1;
        if (!load_skirmish_maps()) ok = false;
        items[0].kind = MI_TEXTFIELD;
        items[27].row = map_row;
        /* The four option rows are each one choice. 133 is a native check
         * box that this port uses as the start button. */
        for (int i = 105; i <= 119; ++i) items[i].group = i <= 108 ? 1 : i <= 113 ? 2 : i <= 116 ? 3 : 4;
        items[105 + skirmish.storage].value = items[110 + skirmish.artifacts].value = 1;
        items[115 + skirmish.erupting].value = items[118 + skirmish.renewable].value = 1;
        items[133].kind = MI_BUTTON;
        items[25].kind = MI_TEXTFIELD;
        items[25].text[0] = '\0';
        for (int i = 166; i <= 173; ++i) items[i].visible = i == 166;
        for (int i = 180; i <= 187; ++i) items[i].visible = i == 180;
        for (int i = 17; i <= 23; ++i) items[i].visible = false;
        refresh_skirmish();
    }
    if (page == NETWORK) {
        items[0].value = items[0].group = 1;
        for (int i = 1; i <= 3; ++i) {
            items[i].visible = false;
            items[7 + i].visible = false;
        }
    }
    if (page == CONNECT) {
        menu.itemOn = 3;
        items[menu.itemOn].kind = MI_TEXTFIELD;
        snprintf(items[menu.itemOn].text, sizeof(items[menu.itemOn].text), "%s",
                 server_address);
    }
    if (page == BROWSE) {
        items[0].row = session_row;
        selected_server[0] = '\0';
        strcpy(items[6].text, "Select LAN Session");
        strcpy(items[5].text, "JOIN");
        /* Use the native button row's unused interval for direct connection. */
        items[18] = items[4];
        items[18].rect.x = items[17].rect.x + items[17].rect.w;
        strcpy(items[18].text, "ADDRESS");
    }
    if (page == OPTIONS) {
        editing = gamesettings;
        editing_speed = game_speed;
        option_values();
    }
    if (page == OBJECTIVES) {
        char briefing[1024];
        snprintf(briefing, sizeof(briefing), "%s", level.map_path);
        char *dot = strrchr(briefing, '.');
        if (dot) strcpy(dot, ".TXT");
        const char *relative = briefing;
        size_t prefix = strlen(root);
        if (!strncmp(briefing, root, prefix) && briefing[prefix] == '/') relative += prefix + 1;
        if (!dot || !read_text(relative)) {
            const char *goal = DC_LevelSkirmish(&level) ?
                "Destroy the opposing colonies and protect your colony." : "No mission briefing is available.";
            prose = malloc(strlen(goal) + 1);
            if (prose) strcpy(prose, goal);
            else ok = false;
        }
        menuitem_t *list = &items[50];
        list->row_height = fonts[0].line_h;
        list->ownerdraw = draw_objectives;
        M_MenuSetRows(list, V_TextWrappedHeight(list->rect.w, list->font, prose) / list->row_height);
        list->value = -1;
    }
    if (page == SAVE || page == LOAD) {
        if (!load_saves()) notice = "Cannot read the save directory";
        menuitem_t *list = &items[page == SAVE ? 50 : 0];
        list->row = save_row;
        list->value = -1;
        M_MenuSetRows(list, numsaves);
        list->prose = "No saved games";
        if (page == SAVE) { items[54].kind = MI_TEXTFIELD; menu.itemOn = 54; }
        else for (int i = 17; i <= 23; ++i) items[i].visible = false;
    }
    entrance = 0;
    if (!numentrances) start_page_animations();
    menutime = SDL_GetTicks64();
    /* Screen constructors play the open sound before the entrances, and
     * 0x425214 plays sound 186 as each entrance gadget starts. */
    S_StartUISound(UI_SOUND_SCREEN);
    if (numentrances) S_StartUISound(UI_SOUND_GADGET);
    return ok && screen.w > 0 && screen.h > 0 &&
        (popup() || background.numlumps) && fonts[0].sprite.numlumps;
}

bool G_InitMenus(app_t *app, const char *data_root) {
    menu.app = app;
    if (strlen(data_root) >= sizeof(root)) return false;
    strcpy(root, data_root);
    initialized = load_screen(MAIN);
    if (!initialized) G_ShutdownMenus();
    return initialized;
}

menu_t *G_ControlPanel(app_t *app, bool in_level) {
    (void)in_level;
    if (!initialized) return NULL;
    if (page != MAIN && !load_screen(MAIN)) {
        fprintf(stderr, "Could not load Dark Colony main menu\n");
        menuerror = true;
        app->running = false;
        return NULL;
    }
    menu.itemOn = 0;
    notice = NULL;
    return &menu;
}

void DC_OpenQuitDialog(app_t *app) {
    if (!initialized || menuactive) return;
    if (!load_screen(QUIT)) {
        fprintf(stderr, "Could not load Dark Colony quit dialog\n");
        menuerror = true;
        return;
    }
    menu.itemOn = 57;
    menu.app = app;
    M_SetupNextMenu(&menu);
    app->dragging_select = false;
    app->selection_rect = (irect_t){0};
}

static void open_popup(app_t *app, int next, int focus) {
    if (!initialized || menuactive) return;
    if (!load_screen(next)) { menuerror = true; app->running = false; return; }
    menuinlevel = true;
    menu.app = app;
    M_SetupNextMenu(&menu);
    menu.itemOn = focus;
    app->dragging_select = false;
    app->selection_rect = (irect_t){0};
}

void DC_OpenOptionsDialog(app_t *app) { open_popup(app, OPTIONS, 56); }
void DC_OpenObjectives(app_t *app) { open_popup(app, OBJECTIVES, 56); }
void DC_OpenSave(app_t *app) {
    if (!netgame) open_popup(app, SAVE, 54);
    else M_StartMessage("Saving is available in single-player games");
}

static bool first_mission(void) {
    char path[1024], line[256];
    M_PathJoin(path, sizeof(path), root, M_va("GAMESTAT/%sSCENE.TXT", race ? (training ? "GT" : "G") : (training ? "HT" : "H")));
    FILE *file = fopen(path, "r");
    if (!file) return false;
    /* Eight faction-name lines, count, then scenario/title/region/map prefix. */
    bool ok = true;
    for (int i = 0; i < 10; ++i) if (!fgets(line, sizeof(line), file)) ok = false;
    if (!fgets(mission_title, sizeof(mission_title), file) ||
        !fgets(mission_region, sizeof(mission_region), file) || !fgets(line, sizeof(line), file)) ok = false;
    fclose(file);
    if (!ok) return false;
    mission_title[strcspn(mission_title, "\r\n")] = '\0';
    mission_region[strcspn(mission_region, "\r\n")] = '\0';
    line[strcspn(line, "\r\n")] = '\0';
    snprintf(mapname, sizeof(mapname), "%s.MAP", M_Upper(line));
    return true;
}

static void network_failure(void) {
    const char *why = M_NetNotice();
    if (why && why[0]) snprintf(network_notice, sizeof(network_notice), "%.127s", why);
    else if (neterror[0]) snprintf(network_notice, sizeof(network_notice), "%.127s", neterror);
    else if (!network_notice[0]) snprintf(network_notice, sizeof(network_notice), "The connection failed");
    M_NetStop();
    waiting = false;
    notice = network_notice;
    if (page == CONNECT) snprintf(items[3].text, sizeof(items[3].text), "%s", network_notice);
}

static bool join_session(const char *address) {
    network_notice[0] = '\0';
    memset(lobby_ready, 0, sizeof(lobby_ready));
    if (!M_NetJoinAddress(address)) { network_failure(); return true; }
    waiting = true;
    client = false;
    return load_screen(CONNECT);
}

static void activate(app_t *app, int id) {
    bool ok = true;
    notice = NULL;
    if (page == QUIT) {
        if (id == 56) app->running = false;
        if (id == 56 || id == 57) M_ClearMenus();
    } else if (page == OPTIONS) {
        int *value = NULL, step = 1, maximum = 10, minimum = 0;
        if (id == 40 || id == 41) { value = &editing_speed; step = 10; minimum = 10; maximum = 200; }
        else if (id == 42 || id == 43) value = &editing.sound;
        else if (id == 67 || id == 68) value = &editing.music;
        else if (id == 44 || id == 45) { value = &editing.detail; maximum = 2; }
        if (value) {
            if ((id == 40 || id == 41) && netgame && consoleplayer) return;
            *value += (id == 40 || id == 42 || id == 44 || id == 67) ? -step : step;
            if (*value < minimum) *value = minimum;
            if (*value > maximum) *value = maximum;
            S_SetVolume(editing.sound * 10);
            option_values();
        } else if (id == 55) {
            S_SetVolume(gamesettings.sound * 10);
            M_ClearMenus();
        } else if (id == 56) {
            if ((!netgame || consoleplayer == 0) &&
                !G_QueueTiccmd(&(ticcmd_t){.order = TC_SPEED, .product = editing_speed})) {
                notice = "Command queue is full; try again";
                return;
            }
            gamesettings = editing;
            S_SetVolume(editing.sound * 10);
            if (!D_SaveSettings(editing_speed)) { notice = "Cannot save settings"; M_StartMessage(notice); return; }
            M_ClearMenus();
        }
    } else if (page == OBJECTIVES) {
        if (id == 56) M_ClearMenus();
    } else if (page == SAVE) {
        if (id == 55) M_ClearMenus();
        else if (id == 56) {
            const char *name = items[54].text;
            if (!*name) { notice = "Enter a save name"; M_StartMessage(notice); return; }
            for (const char *p = name; *p; ++p)
                if (!isalnum((unsigned char)*p) && *p != ' ' && *p != '_' && *p != '-') {
                    notice = "Use letters, numbers, spaces, hyphens or underscores";
                    M_StartMessage(notice);
                    return;
                }
            snprintf(g_savename, sizeof(g_savename), "%.32s", name);
            M_PathJoin(g_savefile, sizeof(g_savefile), D_UserDirectory(), M_va("%s.sav", g_savename));
            M_ClearMenus();
        }
    } else if (page == LOAD) {
        if (id == 4) ok = load_screen(MAIN);
        else if (id == 5 && items[0].value >= 0 && items[0].value < numsaves) {
            saveentry_t *save = &saves[items[0].value];
            dc_saveinfo_t checked;
            if (!DC_SaveInfo(save->path, &checked)) {
                notice = "The saved game is damaged or incompatible"; M_StartMessage(notice); return;
            }
            snprintf(g_loadfile, sizeof(g_loadfile), "%s", save->path);
            snprintf(mapname, sizeof(mapname), "%s", checked.map);
            if (checked.skirmish) DC_RequestSkirmish(mapname, &checked.setup);
            menumap = mapname;
            M_ClearMenus();
        }
    } else if (page == MAIN) {
        if (id == 12) app->running = false;
        else if (id == 2 && !netgame) ok = load_screen(LOAD);
        else if ((id == 0 || id == 1) && !netgame) {
            training = id == 1;
            race = 0;
            ok = load_screen(SETUP);
        } else if (id == 3 && !netgame) {
            lan = true;
            network_notice[0] = '\0';
            use_net();
            ok = load_screen(NETWORK);
        } else if (id == 4 && !netgame) {
            lan = false;
            skirmish = (dc_skirmish_t){.erupting = 1, .quantity = 4, .flow = 4};
            for (int i = 0; i < 8; ++i)
                skirmish.players[i] = (dc_skirmish_player_t){.race = i & 1,
                    .type = i ? (i == 1 ? DC_PLAYER_AI : DC_PLAYER_NONE) : DC_PLAYER_HUMAN,
                    .color = i == 1 ? 1 : 0, .team = i};
            strcpy(skirmish.players[0].name, "Player0"); /* 0x410091: Player%d. */
            ok = load_screen(SKIRMISH);
        } else notice = menuinlevel ? "Escape resumes the current game" : "This menu is not implemented yet";
    } else if (page == SETUP) {
        if (id == 0 || id == 1) {
            race = id;
            items[23].visible = race == 0;
            items[26].visible = race == 1;
            animate(race ? 26 : 23, MANIM_LOOP);
        } else if (id == 4) ok = load_screen(MAIN);
        else if (id == 2 || id == 3) {
            if (!leader[0]) { menu.itemOn = 5; notice = "Type a name for your leader"; }
            else ok = first_mission() && load_screen(training ? BRIEFING : STORY);
        }
    } else if (page == STORY) {
        if (id == 4) ok = load_screen(SETUP);
        else if (id == 5) ok = load_screen(BRIEFING);
    } else if (page == NETWORK) {
        if (id == 6) ok = load_screen(MAIN);
        else if (id == 4) {
            skirmish = (dc_skirmish_t){.erupting = 1, .quantity = 4, .flow = 4};
            for (int i = 0; i < 8; ++i)
                skirmish.players[i] = (dc_skirmish_player_t){.race = i & 1, .color = i, .team = i,
                    .type = i < 2 ? DC_PLAYER_HUMAN : DC_PLAYER_NONE};
            strcpy(skirmish.players[0].name, "Host");
            network_notice[0] = '\0';
            ok = load_screen(SKIRMISH);
        } else if (id == 5) {
            network_notice[0] = '\0';
            ok = load_screen(BROWSE);
            if (ok && !M_NetBrowse()) network_failure();
        }
    } else if (page == BROWSE) {
        if (id == 4) { M_NetStop(); ok = load_screen(NETWORK); }
        else if (id == 17) {
            network_notice[0] = '\0';
            if (!M_NetBrowse()) network_failure();
            selected_server[0] = '\0';
            items[0].first_row = 0;
        } else if (id == 18) { M_NetStop(); ok = load_screen(CONNECT); }
        else if (id == 5 && selected_server[0]) ok = join_session(selected_server);
    } else if (page == CONNECT) {
        if (id == 1) { M_NetStop(); waiting = false; ok = load_screen(NETWORK); }
        else if (id == 0 && !waiting) ok = join_session(server_address);
    } else if (page == SKIRMISH) {
        if (id == 132) {
            if (lan) M_NetStop();
            waiting = client = false;
            ok = load_screen(lan ? NETWORK : MAIN);
        } else if (lan && waiting && id == 25) {
            send_chat();
            refresh_skirmish();
        } else if (lan && waiting) {
            int me = consoleplayer_slot();
            if (id == 8 + me && !lobby_ready[me]) M_NetCycleRace(me);
            else if (id == 133) M_NetToggleReady();
            if (!pull_lobby()) {
                snprintf(network_notice, sizeof(network_notice), "Host sent an invalid game setup");
                network_failure();
            }
            refresh_skirmish();
        } else if (lan && !waiting) {
            if (id == 90 || id == 91) {
                int count = active_players();
                count = count > id - 88 ? id - 88 : id - 87;
                for (int i = 1; i < 8; ++i)
                    skirmish.players[i].type = i < count ? DC_PLAYER_HUMAN : DC_PLAYER_NONE;
            } else if ((id >= 8 && id < 16 && skirmish.players[id - 8].type != DC_PLAYER_NONE) ||
                       (id >= 32 && id < 48 && skirmish.players[id & 7].type != DC_PLAYER_NONE) ||
                       (id >= 150 && id <= 165 && skirmish.players[(id - 150) % 8].type != DC_PLAYER_NONE) ||
                       (id >= 105 && id <= 131)) {
                activate_skirmish(id);
            } else if (id == 133 && selectedmap >= 0) {
                skirmish.seed = (uint8_t)SDL_GetTicks();
                memset(lobby_ready, 0, sizeof(lobby_ready));
                push_lobby();
                if (M_NetHost(maps[selectedmap].title, maps[selectedmap].path, active_players())) waiting = true;
                else network_failure();
            }
            refresh_skirmish();
        } else if (!lan) activate_skirmish(id);
    } else if (page == BRIEFING) {
        if (id == 0) ok = load_screen(training ? SETUP : STORY);
        else if (id == 2) { menumap = mapname; M_ClearMenus(); }
        else if (id == 1) notice = "Encyclopedia is not implemented yet";
    }
    if (!ok) {
        fprintf(stderr, "Could not load Dark Colony menu screen %d\n", page);
        menuerror = true;
        app->running = false;
    }
}

static bool selectable(int id) {
    const menuitem_t *item = &items[id];
    if (page == OPTIONS && netgame && consoleplayer && (id == 40 || id == 41)) return false;
    if (page == LOAD && id == 5) return items[0].value >= 0 && items[0].value < numsaves;
    if ((page == OBJECTIVES || page == SAVE || page == LOAD) &&
        (item->kind == MI_LIST || item->kind == MI_SCROLLBAR)) return item->visible;
    if (page == CONNECT && waiting) return id == 1;
    if (page == CONNECT && id == 3) return !waiting;
    if (page == BROWSE) {
        if (id == 5) return selected_server[0] != '\0';
        if (id == 0 || id == 1) return true;
    }
    if (page == SKIRMISH) {
        if (lan && waiting) {
            int me = consoleplayer_slot();
            return id == 132 || id == 133 || id == 25 || (id == 8 + me && !lobby_ready[me]);
        }
        if (lan) {
            if (id == 90 || id == 91) return true;
            if (id >= 8 && id < 16) return skirmish.players[id - 8].type != DC_PLAYER_NONE;
            if (id >= 32 && id < 48) return skirmish.players[id & 7].type != DC_PLAYER_NONE;
            if (id >= 150 && id <= 165) return skirmish.players[(id - 150) % 8].type != DC_PLAYER_NONE;
            if (id >= 105 && id <= 131 && id != 124 && id != 125 && id != 128 && id != 129) return items[id].visible;
            if (id == 132) return true;
            if (id == 133) return selectedmap >= 0;
            return id == 27 || id == 28 || id == 29 || id == 30;
        }
        if (id == 88 || (id > 32 && id < 40) || (id > 40 && id < 48)) return false;
        if (id >= 9 && id < 16 && skirmish.players[id - 8].type == DC_PLAYER_NONE) return false;
        if (id >= 150 && id <= 165 && skirmish.players[(id - 150) % 8].type == DC_PLAYER_NONE) return false;
        if ((id == 16 || id == 133) && selectedmap < 0) return false;
        if (item->kind == MI_LIST || item->kind == MI_SCROLLBAR) return item->visible;
    }
    return item->visible && (item->kind == MI_BUTTON || item->kind == MI_CHECK ||
                             item->kind == MI_TEXTFIELD);
}

static const char *map_row(const menuitem_t *item, int row) {
    (void)item;
    return maps[filtered_map(row)].label;
}

static const char *session_row(const menuitem_t *item, int row) {
    (void)item;
    static char label[128];
    int count;
    const netgame_t *games = I_NetGames(&count);
    snprintf(label, sizeof(label), "%.24s  %d/%d", games[row].name[0] ? games[row].name : games[row].map,
             games[row].players, games[row].capacity);
    return label;
}

/* Bring the items in line with the setup, the network and the catalog. */
static void refresh(menu_t *screen) {
    (void)screen;
    for (int i = 0; i < NUMCONTROLS; ++i) items[i].enabled = selectable(i);
    if (page == SKIRMISH) {
        menuitem_t *list = &items[27];
        int rows = 0;
        list->value = -1;
        for (; filtered_map(rows) >= 0; ++rows)
            if (filtered_map(rows) == selectedmap) list->value = rows;
        M_MenuSetRows(list, rows);
    }
    if (page == BROWSE) {
        menuitem_t *list = &items[0];
        int rows;
        const netgame_t *games = I_NetGames(&rows);
        list->value = -1;
        for (int i = 0; i < rows; ++i)
            if (!strcmp(games[i].address, selected_server)) list->value = i;
        M_MenuSetRows(list, rows);
        list->prose = network_notice[0] ? network_notice :
            "Searching for LAN games...\nUse ADDRESS to connect directly.";
    }
    if (page == CONNECT) items[3].prose = notice == network_notice ? network_notice : NULL;
    if (page == OPTIONS) option_values();
}

static void menu_escape(menu_t *screen) {
    app_t *app = screen->app;
    if (popup()) {
        if (page == OPTIONS) S_SetVolume(gamesettings.sound * 10);
        M_ClearMenus();
    }
    else if (page == LOAD) { if (!load_screen(MAIN)) { menuerror = true; app->running = false; } }
    else if (page != MAIN) activate(app, page == SKIRMISH ? 132 : page == NETWORK ? 6 :
        page == CONNECT ? 1 : page == BROWSE || page == SETUP || page == STORY ? 4 : 0);
    else if (menuinlevel) M_ClearMenus();
}

static void menu_routine(menu_t *screen, menuitem_t *item, menuaction_t action) {
    app_t *app = screen->app;
    int id = (int)(item - items);
    if (action == MA_CHANGE && page == SAVE && id == 50) {
        snprintf(items[54].text, sizeof(items[54].text), "%s", save_row(item, item->value));
        return;
    }
    if (action == MA_CHANGE && item->kind == MI_TEXTFIELD) {
        if (page == CONNECT && id == 3 && !waiting)
            snprintf(server_address, sizeof(server_address), "%s", item->text);
        else if (page == SKIRMISH && id == 0 && !lan) {
            snprintf(skirmish.players[0].name, sizeof(skirmish.players[0].name), "%s", item->text);
            refresh_skirmish();
        } else if (page == SETUP && id == 5) snprintf(leader, sizeof(leader), "%s", item->text);
        else return;
        if (!(page == SKIRMISH && id == 0)) notice = NULL;
        return;
    }
    if (action == MA_CHANGE && page == BROWSE && id == 0) {
        int count;
        const netgame_t *games = I_NetGames(&count);
        if (item->value < count) strcpy(selected_server, games[item->value].address);
        return;
    }
    if (action == MA_CHANGE && page == SKIRMISH && id == 27) {
        selectedmap = filtered_map(item->value);
        refresh_skirmish();
        return;
    }
    if (action != MA_ACTIVATE) return;
    /* Enter on a field or the session list confirms the screen. A mouse click
     * acts on the control it hit. */
    if (page == SKIRMISH && id == 25 && screen->held == item) return; /* A click only focuses. */
    if (screen->held != item) {
        if (page == SETUP && id == 5) id = training ? 2 : 3;
        else if (page == CONNECT && id == 3) id = 0;
        else if ((page == BROWSE || page == LOAD) && id == 0) id = 5;
        else if (page == SAVE && (id == 54 || id == 50)) id = 56;
    }
    activate(app, id);
}

static void step_entrances(void) {
    while (entrance < numentrances) {
        menuentrance_t *e = &entrances[entrance];
        if (e->started + 1 < e->count) {
            const menuitem_t *g = &items[e->gadgets[e->started]];
            /* 0x425257..0x425294: the next gadget starts when the running one
             * reaches its third frame, so the entrances overlap. */
            if (!g->visible || !g->userdata || g->anim.frame - g->anim.first == 2) {
                animate(e->gadgets[++e->started], MANIM_ONCE);
                S_StartUISound(UI_SOUND_GADGET);
            }
        }
        if (e->finished < e->count) {
            menuitem_t *g = &items[e->gadgets[e->finished]];
            /* 0x4252a5..0x4252be: hide completed gadgets. An omitted
             * transport's hidden gadget cannot tick or block the entrance. */
            if (!g->visible || g->anim.mode == MANIM_STOPPED) {
                g->visible = false;
                ++e->finished;
            }
        }
        if (e->finished < e->count) return;
        if (++entrance == numentrances) start_page_animations();
    }
}

static void menu_ticker(menu_t *screen) {
    S_StartUISound(UI_SOUND_SCREEN);
    if (screen->app && screen->app->window)
        SDL_SetWindowTitle(screen->app->window, notice ? notice : "Dark Colony");
    if (waiting) {
        int status = M_NetPoll();
        if (status < 0) {
            bool joiner = client;
            network_failure();
            if (joiner) {
                /* The joiner returns to the address screen with the reason. */
                client = false;
                if (!load_screen(CONNECT)) { menuerror = true; return; }
                notice = network_notice;
                snprintf(items[3].text, sizeof(items[3].text), "%s", network_notice);
            } else if (page == SKIRMISH) refresh_skirmish();
            else if (page == CONNECT) snprintf(items[3].text, sizeof(items[3].text), "%s", network_notice);
        } else if (status > 0) {
            waiting = client = false;
            if (dc_net_bad) {
                menumap = NULL;
                snprintf(network_notice, sizeof(network_notice), "Host sent an invalid game setup");
                notice = network_notice;
                M_NetStop();
                if (page == SKIRMISH) refresh_skirmish();
            } else {
                M_ClearMenus();
                return;
            }
        } else if (!client && page == CONNECT && M_NetInLobby()) {
            uint8_t opt[16];
            if (M_NetOptions(opt, sizeof(opt)) != sizeof(opt)) {
                /* A command-line host has no lobby setup; its joiners just wait. */
                strcpy(items[3].text, "Joined; waiting for the host");
            } else {
                /* Joined: enter the host's lobby (0x405670 -> 0x401210 -> 0x40fb20). */
                lan = client = true;
                if (!load_screen(SKIRMISH) || !pull_lobby()) {
                    snprintf(network_notice, sizeof(network_notice), "Host sent an invalid game setup");
                    network_failure();
                    client = false;
                    if (!load_screen(CONNECT)) menuerror = true;
                    notice = network_notice;
                    snprintf(items[3].text, sizeof(items[3].text), "%s", network_notice);
                    return;
                }
                refresh_skirmish();
            }
        } else if (page == SKIRMISH && (M_NetHosting() || M_NetInLobby())) {
            if (!pull_lobby()) {
                bool joiner = client;
                snprintf(network_notice, sizeof(network_notice), "Host sent an invalid game setup");
                network_failure();
                if (joiner) {
                    client = false;
                    if (!load_screen(CONNECT)) menuerror = true;
                    notice = network_notice;
                    snprintf(items[3].text, sizeof(items[3].text), "%s", network_notice);
                }
                return;
            }
            if (M_NetHosting() && doomcom) {
                if (I_NetPlayerCount() == doomcom->numplayers)
                    snprintf(network_notice, sizeof(network_notice),
                             "All %d players joined.\nThe game starts when everyone is ready.", doomcom->numplayers);
                else
                    snprintf(network_notice, sizeof(network_notice),
                             "Waiting for LAN players: %d/%d\nEscape cancels the session.",
                             I_NetPlayerCount(), doomcom->numplayers);
            }
            refresh_skirmish();
        } else if (page == CONNECT) {
            strcpy(items[3].text, "Connecting... Escape cancels");
        }
    }
    if (page == BROWSE) {
        int count;
        const netgame_t *games = I_NetGames(&count);
        bool found = false;
        for (int i = 0; i < count; ++i) found |= !strcmp(games[i].address, selected_server);
        if (!found) selected_server[0] = '\0';
        if (neterror[0]) network_failure();
    }
    uint64_t now = SDL_GetTicks64();
    if (page == NETWORK && items[GLOBE].visible && now - globetime > 50) {
        /* PIC mode 1, 0x426497..0x426608: skip elapsed 50 ms frames,
         * wrap to cell 1 at the SPR count; cell 0 is not in this loop. */
        menuitem_t *globe = &items[GLOBE];
        uint64_t frame = globe->anim.frame + (now - globetime) / 50;
        globe->anim.frame = frame < (uint64_t)globe->sheet->numlumps ? (int)frame : 1;
        globetime = now;
    }
    if (now - menutime <= 16) return; /* DC.EXE 0x421ebd: menu cadence. */
    menutime = now;
    /* Gadget ticker 0x422828 holds the last one-off pose, as the engine's
     * MANIM_ONCE does; the world animation ticker 0x423dd0 resets to zero. */
    M_MenuTicker(&menu);
    step_entrances();
}
