#define _DEFAULT_SOURCE
#include "z_local.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ZED.EXE 0x51d1e/0x549c4 and 0x52411/0x52abf. Native coordinates,
 * sprite IDs, font advances and multiplayer map lists: Z_EXE_FINDINGS.md. */
enum { MAIN, REPLAY, LOAD, SCORES, MULTI, BROWSE, LOBBY };
enum { NEW = 1, RESTORE, REPLAY_LEVEL, MULTIPLAYER, HIGH_SCORE, QUIT,
       LIST, FILENAME, ACCEPT, CANCEL, LINK, MAP, PLAYERS, NAME, CONFIGURE,
       CONNECT, DISCONNECT, STATUS, SESSIONS };
static int page, players = 2, selectedmap;
static bool initialized, waiting;
static char launch[1024], playername[17] = "Player";
static spritesheet_t backgrounds[3], pictures;
static bitmapfont_t fonts[2];
static menuitem_t items[40];
static void escape(menu_t *menu);
static void refresh(menu_t *menu);
static void ticker(menu_t *menu);
static void routine(menu_t *menu, menuitem_t *item, menuaction_t action);
static menu_t menu = {.items = items, .modal = true, .size = {320,200}, .fit = true,
                      .escape = escape, .refresh = refresh, .ticker = ticker};
static const int images[] = {
    1129,1118,1119,1120,1121,1133,1134,1135,1136,2869,
    3591,3592,3040,3041,3042,3043,
    3595,3596,3597,3608,3760,3761,3768,3762,3763,3764,3765,3766,3767,
    6647,6648,6653,3598,3603,3604,3606,3607,6640,6641,6642,6643,6912,6913
};
/* 0x11cd35 points to three eight-byte lists, terminated with 0xff. */
static const uint8_t multiplayer_maps[3][6] = {
    {26,27,28,29,31,32}, {40,38,22,37,39,0xff}, {33,35,24,23,21,0xff}
};
typedef struct { char title[21], path[40]; int players; } mapentry_t;
static mapentry_t campaign[20], maps[16];
static int nummaps;
typedef struct { char path[1200]; saveinfo_t info; } saveentry_t;
static saveentry_t *saves;
static int numsaves;
/* Preserve the existing in-level control panel; retail pause/options are a
 * separate screen family from the front end implemented here. */
static menuitem_t pauseitems[] = {
    {.kind = MI_BUTTON, .visible = true, .enabled = true, .rect = {220,215,200,30},
     .fill = 0xff18242du, .color = 0xffdce6dcu, .align = MALIGN_CENTER, .routine = M_MenuBeginLevel},
    {.kind = MI_BUTTON, .visible = true, .enabled = true, .rect = {220,295,200,30},
     .fill = 0xff18242du, .color = 0xffdce6dcu, .text = "QUIT GAME", .align = MALIGN_CENTER,
     .routine = M_MenuQuitGame},
};
static menu_t pausemenu = {.items = pauseitems, .numitems = sizeof(pauseitems) / sizeof(*pauseitems)};

static int cell(int native) {
    for (unsigned i = 0; i < sizeof(images) / sizeof(*images); ++i)
        if (images[i] == native) return (int)i;
    return -1;
}

static void palette(spritesheet_t *sheet, const uint32_t *colors) {
    memcpy(sheet->palette, colors, sizeof(sheet->palette));
    memcpy(sheet->source_palette, colors, sizeof(sheet->source_palette));
}

static menuitem_t *add(menuitemkind_t kind, int id, irect_t rect) {
    menuitem_t *item = &items[menu.numitems++];
    *item = (menuitem_t){.kind = kind, .id = id, .rect = rect, .visible = true,
        .enabled = kind != MI_STATIC, .release = true, .link = -1,
        .font = &fonts[1], .routine = routine};
    for (int s = 0; s < MS_STATES; ++s)
        item->look[s] = (menulook_t){.cell = -1, .palette = -1,
                                   .font = s == MS_FOCUS || s == MS_PUSHED ? &fonts[0] : &fonts[1]};
    return item;
}

static menuitem_t *picture(int native, ivec2_t at) {
    int index = cell(native);
    irect_t rect = pictures.cells[index].rect;
    menuitem_t *item = add(MI_STATIC, 0, (irect_t){at.x,at.y,rect.w,rect.h});
    item->sheet = &pictures;
    for (int s = 0; s < MS_STATES; ++s) item->look[s].cell = index;
    return item;
}

/* A native overlay carries background well beyond its clickable label. */
static menuitem_t *button(int id, irect_t hit, int native, ivec2_t at, SDL_Keycode key) {
    menuitem_t *item;
    if (native >= 0) {
        item = picture(native, at);
        item->id = id; item->kind = MI_BUTTON; item->enabled = true;
        item->look[MS_NORMAL].cell = item->look[MS_DISABLED].cell = -1;
    } else item = add(MI_BUTTON, id, hit);
    item->hitbox = (irect_t){hit.x - item->rect.x, hit.y - item->rect.y, hit.w, hit.h};
    item->hotkey = key;
    return item;
}

/* Several native state pictures span two RSC images. The second image is
 * a decorative item following the same button's focus/press state. */
static void cap(menuitem_t *button, int focus, int pushed, ivec2_t at) {
    menuitem_t *item = picture(pushed, at);
    item->userdata = button;
    item->look[MS_FOCUS].cell = cell(focus);
    item->look[MS_PUSHED].cell = cell(pushed);
    item->look[MS_NORMAL].cell = -1;
}

static menuitem_t *text(int id, irect_t rect, const char *value) {
    menuitem_t *item = add(MI_STATIC, id, rect);
    snprintf(item->text, sizeof(item->text), "%s", value);
    return item;
}

static const char *row(const menuitem_t *item, int index) {
    (void)item;
    if (page == LOAD) return index >= 0 && index < numsaves ? saves[index].info.name : "";
    return index >= 0 && index < 20 ? campaign[index].title : "";
}

static const char *session_row(const menuitem_t *item, int index) {
    (void)item;
    int count;
    const netgame_t *games = I_NetGames(&count);
    return index >= 0 && index < count ? games[index].name : "";
}

static void read_saves(void) {
    free(saves); saves = NULL; numsaves = 0;
    const char *directory = D_UserDirectory();
    DIR *dir = opendir(directory);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".sav")) continue;
        saveentry_t save;
        M_PathJoin(save.path, sizeof(save.path), directory, entry->d_name);
        if (!G_SaveInfo(save.path, &save.info)) continue;
        saveentry_t *next = realloc(saves, (size_t)(numsaves + 1) * sizeof(*saves));
        if (!next) break;
        saves = next; saves[numsaves++] = save;
    }
    closedir(dir);
}

static void open_page(int next) {
    page = next;
    memset(items, 0, sizeof(items));
    menu.numitems = 0; menu.itemOn = -1;
    menu.held = menu.keyheld = menu.editing = menu.dropdown = NULL;
    menu.background = &backgrounds[page >= MULTI ? 1 : page == SCORES ? 2 : 0];
    menu.palette = menu.background->source_palette;
    palette(&pictures, menu.palette);
    for (int i = 0; i < 2; ++i) palette(&fonts[i].sprite, menu.palette);
    if (page == SCORES) {
        button(CANCEL, (irect_t){216,176,33,18}, -1, (ivec2_t){0}, SDLK_RETURN);
        M_SetupNextMenu(&menu); return;
    }
    if (page == MAIN || page == LOAD || page == REPLAY) {
        picture(1129, (ivec2_t){16,24});
        picture(1134, (ivec2_t){24,128}); picture(1135, (ivec2_t){144,128});
        button(NEW, (irect_t){35,58,66,12}, 1118, (ivec2_t){16,24}, SDLK_n);
        button(RESTORE, (irect_t){35,72,64,12}, 1119, (ivec2_t){16,32}, SDLK_l);
        button(REPLAY_LEVEL, (irect_t){34,86,72,14}, 1120, (ivec2_t){16,48}, SDLK_r);
        button(MULTIPLAYER, (irect_t){34,99,75,15}, 1121, (ivec2_t){16,64}, SDLK_m);
        button(HIGH_SCORE, (irect_t){34,113,58,15}, 1133, (ivec2_t){16,80}, SDLK_h);
        menuitem_t *quit = add(MI_BUTTON, QUIT, (irect_t){56,154,88,22});
        quit->sheet = &pictures; quit->hotkey = SDLK_q;
        quit->look[MS_FOCUS] = (menulook_t){.cell = cell(2869), .part = {32,18,88,22}, .palette = -1};
        quit->look[MS_PUSHED] = (menulook_t){.cell = cell(1136), .part = {32,26,88,22}, .palette = -1};
        menu.itemOn = (int)(M_MenuFind(&menu, NEW) - items);
        if (page == LOAD || page == REPLAY) {
            if (page == LOAD) read_saves();
            menuitem_t *list = add(MI_LIST, LIST, (irect_t){187,90,93,50});
            list->rows = page == LOAD ? numsaves : 20;
            list->value = list->rows ? 0 : -1;
            list->row = row; list->row_height = 10;
            list->prose = "No saved games";
            menuitem_t *bar = add(MI_SCROLLBAR, 0, (irect_t){283,90,12,50});
            bar->link = (int)(list - items); bar->color = 0xff68616au;
            text(FILENAME, (irect_t){236,68,58,10}, "");
            button(CANCEL, (irect_t){190,151,41,15}, -1, (ivec2_t){0}, SDLK_ESCAPE);
            button(ACCEPT, (irect_t){245,151,40,15}, -1, (ivec2_t){0}, SDLK_RETURN);
            menu.itemOn = (int)(list - items);
        }
    } else {
        picture(3591, (ivec2_t){24,24}); picture(3592, (ivec2_t){144,24});
        picture(3597, (ivec2_t){0,152});
        picture(3768, (ivec2_t){80,136});
        picture(6647, (ivec2_t){168,120}); picture(6648, (ivec2_t){288,120});
        picture(3762, (ivec2_t){168,144}); picture(3763, (ivec2_t){288,144});
        picture(3598, (ivec2_t){264,48}); picture(6653, (ivec2_t){197,59});
        text(LINK, (irect_t){200,63,78,10}, "Network");
        cap(button(MAP, (irect_t){36,73,71,12}, 3040, (ivec2_t){24,32}, SDLK_l),
            3041, 3041, (ivec2_t){144,32});
        cap(button(PLAYERS, (irect_t){36,88,105,15}, 3042, (ivec2_t){24,48}, SDLK_p),
            3043, 3043, (ivec2_t){144,48});
        button(MAP, (irect_t){281,79,8,12}, 3604, (ivec2_t){280,72}, 0)->value = -1;
        button(MAP, (irect_t){291,79,8,12}, 3603, (ivec2_t){288,72}, 0)->value = 1;
        button(PLAYERS, (irect_t){281,95,8,12}, 3607, (ivec2_t){280,88}, 0)->value = -1;
        button(PLAYERS, (irect_t){291,95,8,12}, 3606, (ivec2_t){288,88}, 0)->value = 1;
        menuitem_t *name = add(MI_TEXTFIELD, NAME, (irect_t){200,112,78,10});
        snprintf(name->text, sizeof(name->text), "%s", playername); name->maxchars = 16;
        text(MAP, (irect_t){200,79,78,10}, "");
        if (page == BROWSE) {
            M_MenuFind(&menu, MAP)->enabled = false;
            menuitem_t *list = add(MI_DROPDOWN, SESSIONS, (irect_t){200,79,78,10});
            list->row = session_row; list->row_height = 10; list->popup_rows = 6;
            list->value = -1; list->fill = 0xff000000u; list->color = 0xff393345u;
        }
        text(PLAYERS, (irect_t){200,97,78,10}, "");
        text(STATUS, (irect_t){25,137,98,16}, "");
        button(CANCEL, (irect_t){18,168,43,15}, 3595, (ivec2_t){0,152}, SDLK_ESCAPE)->look[MS_PUSHED].cell = cell(3596);
        menuitem_t *configure = add(MI_BUTTON, CONFIGURE, (irect_t){114,168,86,24});
        configure->sheet = &pictures; configure->hotkey = SDLK_j;
        configure->look[MS_FOCUS] = (menulook_t){.cell = cell(3608), .part = {34,24,86,24}, .palette = -1};
        configure->look[MS_PUSHED] = (menulook_t){.cell = cell(3760), .part = {34,32,86,24}, .palette = -1};
        configure->hitbox = (irect_t){0,0,54,15};
        cap(configure, -1, 3761, (ivec2_t){200,136});
        if (waiting) {
            picture(6912, (ivec2_t){176,144}); picture(6913, (ivec2_t){296,144});
            menuitem_t *start = button(CONNECT, (irect_t){223,168,52,15}, 6640, (ivec2_t){176,144}, SDLK_c);
            start->look[MS_PUSHED].cell = cell(6642);
            cap(start, 6641, 6643, (ivec2_t){296,144});
        } else {
            menuitem_t *connect = button(CONNECT, (irect_t){223,168,52,15}, 3764, (ivec2_t){184,144}, SDLK_c);
            connect->look[MS_PUSHED].cell = cell(3766);
            cap(connect, 3765, 3767, (ivec2_t){304,144});
        }
        button(DISCONNECT, (irect_t){205,144,70,16}, -1, (ivec2_t){0}, SDLK_d);
        menu.itemOn = (int)(M_MenuFind(&menu, CONNECT) - items);
    }
    M_SetupNextMenu(&menu);
    refresh(&menu);
}

static void refresh(menu_t *screen) {
    (void)screen;
    for (int i = 0; i < menu.numitems; ++i) if (items[i].userdata) {
        const menuitem_t *button = items[i].userdata;
        int state = !button->enabled ? MS_NORMAL : menu.keyheld == button ||
            (menu.held == button && menu.over) ? MS_PUSHED : M_MenuHover(&menu) == button ? MS_FOCUS : MS_NORMAL;
        items[i].look[MS_NORMAL].cell = state == MS_NORMAL ? -1 : items[i].look[state].cell;
    }
    if (page == LOAD || page == REPLAY) {
        menuitem_t *list = M_MenuFind(&menu, LIST), *name = M_MenuFind(&menu, FILENAME);
        bool selected = list->value >= 0 && list->value < list->rows;
        M_MenuFind(&menu, ACCEPT)->enabled = selected;
        snprintf(name->text, sizeof(name->text), "%.19s", selected ? row(list, list->value) : "");
    }
    if (page < MULTI) return;
    for (int i = 0; i < menu.numitems; ++i) {
        menuitem_t *item = &items[i];
        if (item->kind == MI_STATIC && item->id == MAP)
            snprintf(item->text, sizeof(item->text), "%.19s", page == BROWSE ? "" :
                     waiting && !M_NetHosting() ? I_NetMap() : maps[selectedmap].title);
        if (item->kind == MI_STATIC && item->id == PLAYERS)
            snprintf(item->text, sizeof(item->text), "%d", players);
        if (item->id == MAP || item->id == PLAYERS || item->id == NAME || item->id == CONFIGURE)
            item->enabled = item->kind != MI_STATIC && !waiting &&
                            (page != BROWSE || item->id == NAME || item->id == CONFIGURE);
    }
    M_MenuFind(&menu, CONNECT)->enabled = !waiting ||
        (M_NetHosting() && doomcom && I_NetPlayerCount() == doomcom->numplayers);
    M_MenuFind(&menu, DISCONNECT)->enabled = waiting;
    menuitem_t *status = M_MenuFind(&menu, STATUS);
    if (page == BROWSE) {
        int count; I_NetGames(&count);
        menuitem_t *list = M_MenuFind(&menu, SESSIONS);
        M_MenuSetRows(list, count);
        if (list->value < 0 && count) list->value = 0;
        snprintf(list->text, sizeof(list->text), "%s", session_row(list, list->value));
        M_MenuFind(&menu, CONNECT)->enabled = list->value >= 0 && list->value < count;
        snprintf(status->text, sizeof(status->text), "%s", count ? "Select a game" : "Searching LAN");
    } else if (waiting) snprintf(status->text, sizeof(status->text), "%d/%d players", I_NetPlayerCount(),
                                doomcom ? doomcom->numplayers : players);
    else snprintf(status->text, sizeof(status->text), "Ready");
}

static const char *chat_name(void) { return playername; }

static void escape(menu_t *screen) {
    (void)screen;
    if (waiting) { M_NetStop(); waiting = false; open_page(MULTI); }
    else if (page == BROWSE) { M_NetStop(); open_page(MULTI); }
    else if (page != MAIN) open_page(MAIN);
    else if (level.width) M_ClearMenus();
}

static void ticker(menu_t *screen) {
    M_MenuTicker(screen);
    if (page == BROWSE && neterror[0]) {
        char notice[128]; snprintf(notice, sizeof(notice), "%s", neterror);
        neterror[0] = '\0'; M_NetStop(); open_page(MULTI); M_StartMessage(notice); return;
    }
    if (!waiting) return;
    int result = M_NetPoll();
    if (result > 0) { waiting = false; M_ClearMenus(); }
    else if (result < 0) {
        char notice[128]; snprintf(notice, sizeof(notice), "%s", M_NetNotice());
        M_NetStop(); waiting = false; open_page(MULTI); M_StartMessage(notice);
    } else refresh(screen);
}

static void routine(menu_t *screen, menuitem_t *item, menuaction_t action) {
    if (action == MA_CHANGE && item->id == NAME) {
        snprintf(playername, sizeof(playername), "%.16s", item->text); return;
    }
    if (action != MA_ACTIVATE) return;
    switch (item->id) {
    case NEW:
        menumap = g_game_default_map; M_ClearMenus();
        break;
    case RESTORE: if (!netgame) open_page(LOAD); break;
    case REPLAY_LEVEL: if (!level.width) open_page(REPLAY); break;
    case MULTIPLAYER: if (!level.width) open_page(MULTI); break;
    case HIGH_SCORE: open_page(SCORES); break;
    case QUIT: M_MenuQuitGame(screen, item, action); break;
    case CANCEL: escape(screen); break;
    case LIST: if (page != LOAD && page != REPLAY) break; /* Enter on a list loads. */
        /* fall through */
    case ACCEPT: {
        menuitem_t *list = M_MenuFind(screen, LIST);
        if (!list || list->value < 0 || list->value >= list->rows) break;
        if (page == LOAD) {
            saveentry_t *save = &saves[list->value];
            saveinfo_t checked;
            if (!G_SaveInfo(save->path, &checked)) { M_StartMessage("Saved game is damaged or incompatible"); break; }
            char *absolute = realpath(checked.map, NULL);
            if (!absolute || strlen(absolute) >= sizeof(launch)) {
                free(absolute); M_StartMessage("Cannot find the saved game's map"); break;
            }
            snprintf(g_loadfile, sizeof(g_loadfile), "%s", save->path);
            snprintf(launch, sizeof(launch), "%s", absolute);
            free(absolute);
        } else snprintf(launch, sizeof(launch), "%s", campaign[list->value].path);
        menumap = launch; M_ClearMenus(); break;
    }
    case MAP:
        do { selectedmap = (selectedmap + nummaps + (item->value < 0 ? -1 : 1)) % nummaps; }
        while (maps[selectedmap].players != players);
        break;
    case PLAYERS:
        players = 2 + (players - 2 + (item->value < 0 ? 2 : 1)) % 3;
        for (selectedmap = 0; selectedmap < nummaps && maps[selectedmap].players != players; ++selectedmap) {}
        break;
    case CONFIGURE: {
        if (page == BROWSE) I_QueryNetGames(NULL);
        else {
            const netplay_t play = {.max_players = 4, .chat_name = chat_name};
            M_NetUse(&play);
            if (M_NetBrowse()) open_page(BROWSE);
            else M_StartMessage(M_NetNotice());
        }
        break;
    }
    case CONNECT:
        if (waiting) {
            if (!I_LaunchNetGame()) M_StartMessage("Waiting for every player to join");
        } else if (page == BROWSE) {
            menuitem_t *list = M_MenuFind(screen, SESSIONS);
            if (M_NetJoinListed(list->value)) { waiting = true; open_page(LOBBY); }
            else M_StartMessage(M_NetNotice());
        } else {
            const netplay_t play = {.max_players = 4, .chat_name = chat_name};
            M_NetUse(&play);
            if (M_NetHost(maps[selectedmap].title, maps[selectedmap].path, players)) {
                waiting = true; open_page(LOBBY);
            } else M_StartMessage(M_NetNotice());
        }
        break;
    case DISCONNECT: M_NetStop(); waiting = false; open_page(MULTI); break;
    default: break;
    }
}

bool G_InitMenus(app_t *app, const char *data_root) {
    G_ShutdownMenus();
    const char *root = data_root && *data_root ? data_root : g_game_default_root;
    menu.app = app;
    char path[1200]; M_PathJoin(path, sizeof(path), root, "WARDATA.PAC");
    const char *names[] = {"MANE.LBM", "MULTI.LBM", "HSCORE.LBM"};
    for (int i = 0; i < 3; ++i) {
        blob_t blob;
        if (!Z_ReadPac(path, names[i], &blob)) goto fail;
        bool ok = W_DecodeLBM(&blob, &backgrounds[i]);
        W_FreeFile(&blob);
        if (!ok || backgrounds[i].frame_size.w != 320 || backgrounds[i].frame_size.h != 200) goto fail;
    }
    if (!Z_LoadUISprites(root, images, sizeof(images) / sizeof(*images), &pictures)) goto fail;
    /* 0x57002: bright 830..913, dim +84; widths from 0x11cc84. */
    static const uint8_t widths[] = {
        4,3,4,6,4,6,6,3,3,3,7,6,3,5,4,6,4,4,4,4,5,4,4,4,4,4,3,4,4,4,4,6,
        5,5,6,5,6,5,5,5,5,4,6,5,5,6,5,5,5,6,5,5,4,5,5,6,5,6,5,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,2,4,5,4,6,4,4,4,5,4,4,4,4,4,6,4,4,5};
    for (int f = 0; f < 2; ++f) {
        bitmapfont_t *font = &fonts[f];
        int indices[85]; for (int i = 0; i < 84; ++i) indices[i] = 830 + f * 84 + i;
        indices[84] = -1; /* 0x57098 advances spaces without drawing a sprite. */
        if (!Z_LoadUISprites(root, indices, 85, &font->sprite)) goto fail;
        for (int ch = 0; ch < 256; ++ch) font->glyph_index[ch] = -1;
        for (int ch = 32; ch <= 122; ++ch) {
            font->glyph_width[ch] = widths[ch - 32];
            if (ch >= 33 && ch <= 90) font->glyph_index[ch] = ch - 33;
            if (ch >= 97) font->glyph_index[ch] = 58 + ch - 97;
        }
        font->glyph_index[' '] = 84; font->glyph_width[' '] = 8;
        font->glyph_size = (isize2_t){8,8}; font->line_h = 10;
    }
    M_PathJoin(path, sizeof(path), root, "LEVELS.DAT");
    blob_t levels;
    if (!W_ReadFile(path, &levels)) goto fail;
    bool ok = levels.size >= 51 * 240;
    for (int i = 0; ok && i < 20; ++i) {
        snprintf(campaign[i].title, sizeof(campaign[i].title), "%.20s", (char *)levels.bytes + (i + 1) * 240);
        snprintf(campaign[i].path, sizeof(campaign[i].path), "LEVEL%02d/LEVEL%02d.MAP", i + 1, i + 1);
    }
    nummaps = 0;
    for (int p = 0; ok && p < 3; ++p) for (int m = 0; m < 6 && multiplayer_maps[p][m] != 0xff; ++m) {
        int levelnum = multiplayer_maps[p][m];
        mapentry_t *entry = &maps[nummaps++];
        entry->players = p + 2;
        snprintf(entry->title, sizeof(entry->title), "%.20s", (char *)levels.bytes + levelnum * 240);
        snprintf(entry->path, sizeof(entry->path), "LEVEL%02d/LEVEL%02d.MAP", levelnum, levelnum);
    }
    W_FreeFile(&levels);
    if (!ok) goto fail;
    players = 2; selectedmap = 0;
    initialized = true;
    return true;
fail:
    G_ShutdownMenus();
    return false;
}

menu_t *G_ControlPanel(app_t *app, bool inlevel) {
    if (!initialized) return NULL;
    menu.app = app;
    if (inlevel) return M_SimpleControlPanel(&pausemenu);
    open_page(MAIN);
    return &menu;
}

void G_ShutdownMenus(void) {
    if (waiting || initialized) M_NetStop();
    waiting = initialized = false;
    for (int i = 0; i < 3; ++i) R_FreeSprite(&backgrounds[i]);
    R_FreeSprite(&pictures);
    for (int i = 0; i < 2; ++i) HU_FreeFont(&fonts[i]);
    free(saves); saves = NULL; numsaves = 0;
    memset(items, 0, sizeof(items)); menu.numitems = 0;
}
