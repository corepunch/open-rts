#define _DEFAULT_SOURCE
#include "m_menu.h"
#include "../../../hud/m_menu.h"
#include "game.h"
#include "w_spr.h"
#include "d_net.h"
#include "dc_skirmish.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

/* DC.EXE's screen scripts own layout; the M_* lifecycle is separate from SB_*.
 * Native control IDs and dispatch: docs/DC_EXE_FINDINGS.md, Main-menu screens. */
bool menuactive;
bool menuerror;
const char *menumap;
static char root[1024], leader[128], mapname[1024];
static int race, scroll;
static int bright_pushed, bright_highlight;
static menu_t dc_menu = { .grab = -1 };
static menuitem_t dc_items[300];
#define itemOn dc_menu.itemOn
#define pressed dc_menu.grab
static bool initialized, training;
static uint64_t menutime;
static const char *notice;
static char mission_title[128], mission_region[128];
static char *prose;
static enum { MAIN, SETUP, STORY, BRIEFING, SKIRMISH, QUIT, NETWORK, SESSION_NAME, BROWSE, CONNECT } page;
static bool lan, waiting;
static char session_name[32] = "Dark Colony", server_address[128] = "127.0.0.1";
static char network_notice[128], selected_server[64];
static int server_scroll;
static isize2_t screen;
static bitmapfont_t fonts[3];
static spritesheet_t background;
static spritesheet_t pictures;
static spritecache_t images;

typedef struct {
    dc_fin_t fin;
} menuanimation_t;
static menuanimation_t animations[16];
static int numanimations;

/* The native screen object has 300 controls (0x34 bytes each at +0x88).
 * Only the presentation fields needed by these screens are decoded. */
typedef struct {
    enum { NONE, PUSH, CHECK, LABEL, TEXT, GADGET, PICTURE, LIST, SCROLL } kind;
    irect_t rect;
    char text[128], animation[32];
    int message, font, remap, mode, frame, delay;
    int maxchars;
    int normal, pushed;
    bool centered, visible, checked, writable;
    const dc_fin_t *fin;
    const dc_fin_label_t *sequence;
} menucontrol_t;
static menucontrol_t controls[300];
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
static int nummaps, selectedmap = -1, mapscroll;

static void refresh_skirmish(void);
static bool load_skirmish_maps(void);

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
    for (int i = 0; i < numanimations; ++i) DC_FreeFIN(&animations[i].fin);
    numanimations = 0;
    free(prose);
    prose = NULL;
    free(maps);
    maps = NULL;
    nummaps = 0;
    memset(controls, 0, sizeof(controls));
    memset(messages, 0, sizeof(messages));
    numentrances = entrance = 0;
    screen = (isize2_t){0};
    bright_pushed = bright_highlight = 0;
    pressed = -1;
}

void M_Shutdown(void) {
    if (waiting || page == BROWSE) I_CancelNetGame();
    waiting = false;
    free_screen();
    initialized = menuactive = false;
    menumap = NULL;
    SDL_StopTextInput();
}

static bool load_animations(const char *name) {
    char path[1024], entry[128];
    M_PathJoin(path, sizeof(path), root, name);
    FILE *file = fopen(path, "r");
    if (!file) return false;
    bool ok = true;
    while (fscanf(file, "%127s", entry) == 1) {
        if (numanimations == 16) { ok = false; break; }
        dc_fin_t *fin = &animations[numanimations++].fin;
        M_PathJoin(path, sizeof(path), root, M_va("ANIMATE/%s", M_Upper(entry)));
        if (!DC_LoadFIN(path, fin)) { ok = false; break; }
        for (int i = 0; i < fin->header->dependency_count; ++i) {
            char key[32];
            snprintf(key, sizeof(key), "SPRITES/%.8s.SPR", fin->dependencies[i].name);
            M_Upper(key);
            if (R_CacheLookup(&images, key)) continue;
            if (images.count == MAX_DECORATION_SPRITES) { ok = false; break; }
            cachedsprite_t *image = &images.entries[images.count];
            M_PathJoin(path, sizeof(path), root, key);
            if (!DC_LoadSpriteImage(path, &image->sprite)) { ok = false; break; }
            snprintf(image->name, sizeof(image->name), "%s", key);
            ++images.count;
        }
        if (!ok) break;
    }
    if (ferror(file)) ok = false;
    fclose(file);
    return ok;
}

static void animate(int id, int mode) {
    menucontrol_t *control = &controls[id];
    if (!control->sequence) return;
    control->mode = mode;
    control->frame = control->sequence->start;
    control->delay = 0;
}

static bool read_text(const char *name) {
    char path[1024];
    M_PathJoin(path, sizeof(path), root, name);
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

static int visible_map_rows(void) {
    return controls[27].rect.h / fonts[0].glyph_size.h;
}

static void scroll_maps(int amount) {
    int count = 0;
    while (filtered_map(count) >= 0) ++count;
    mapscroll += amount;
    if (mapscroll > count - visible_map_rows()) mapscroll = count - visible_map_rows();
    if (mapscroll < 0) mapscroll = 0;
}

static void drag_map_scroll(ivec2_t point) {
    int count = 0;
    while (filtered_map(count) >= 0) ++count;
    irect_t bar = controls[30].rect;
    /* 0x42805f..0x4280d6 centers the visible range on the pointer. */
    mapscroll = (point.y - bar.y) * count / bar.h - visible_map_rows() / 2;
    scroll_maps(0);
}

static void gadget_pose(int id, int pose) {
    menucontrol_t *c = &controls[id];
    if (c->sequence && pose >= 0 && c->sequence->start + pose <= c->sequence->end) {
        c->frame = c->sequence->start + pose;
        c->mode = 2;
    }
}

static void refresh_skirmish(void) {
    for (int i = 0; i < 8; ++i) {
        const dc_skirmish_player_t *p = &skirmish.players[i];
        snprintf(controls[i].text, sizeof(controls[i].text), "%s", p->type == DC_PLAYER_HUMAN ? p->name : "");
        strcpy(controls[72 + i].text, messages[52 + p->type]);
        strcpy(controls[80 + i].text, messages[50 + p->race]);
        gadget_pose(8 + i, p->race);
        gadget_pose(88 + i, p->type);
        gadget_pose(96 + i, p->color * 2);
        gadget_pose(142 + i, p->team * 2);
    }
    for (int i = 105; i <= 108; ++i) controls[i].checked = i - 105 == skirmish.storage;
    for (int i = 110; i <= 113; ++i) controls[i].checked = i - 110 == skirmish.artifacts;
    controls[115].checked = !skirmish.erupting;
    controls[116].checked = skirmish.erupting;
    controls[118].checked = !skirmish.renewable;
    controls[119].checked = skirmish.renewable;
    snprintf(controls[121].text, sizeof(controls[121].text), "%d%%", skirmish.quantity * 25);
    snprintf(controls[125].text, sizeof(controls[125].text), "%d%%", skirmish.flow * 25);
    strcpy(controls[129].text, messages[(skirmish.players[0].race ? 40 : 30) + skirmish.rank]);
    if (selectedmap >= 0 && maps[selectedmap].players < active_players()) selectedmap = -1;
    snprintf(controls[26].text, sizeof(controls[26].text), "%s", selectedmap < 0 ? "" : maps[selectedmap].title);
    scroll_maps(0);
    if (lan) {
        for (int i = 0; i < 8; ++i) {
            if (i) snprintf(controls[i].text, sizeof(controls[i].text), "%s",
                            i < active_players() ? "LAN Player" : "");
            controls[16 + i].visible = false;
            controls[180 + i].visible = false;
            controls[8 + i].visible = controls[80 + i].visible = false;
            controls[96 + i].visible = controls[142 + i].visible = false;
        }
        for (int i = 32; i <= 47; ++i) controls[i].visible = false;
        for (int i = 104; i <= 131; ++i) controls[i].visible = false;
        for (int i = 150; i <= 165; ++i) controls[i].visible = false;
        controls[137].visible = controls[139].visible = controls[140].visible = controls[141].visible = false;
        snprintf(controls[133].text, sizeof(controls[133].text), "%s", waiting ? "WAITING" : "CREATE");
        snprintf(controls[24].text, sizeof(controls[24].text), "%s", network_notice[0] ? network_notice :
                 "Select map and 2-4 LAN player slots.\nMap supplies factions and game settings.\nStarts when all players connect.");
        controls[25].visible = false;
    }
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
        mapscroll = 0;
    } else if (id == 32 || id == 40) {
        int oldcolor = skirmish.players[0].color;
        int newcolor = (oldcolor + (id == 32 ? 1 : 7)) % 8;
        for (int i = 1; i < 8; ++i)
            if (skirmish.players[i].type != DC_PLAYER_NONE && skirmish.players[i].color == newcolor)
                skirmish.players[i].color = oldcolor;
        skirmish.players[0].color = newcolor;
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
    else if (id == 28 || id == 29) scroll_maps(id == 28 ? -1 : 1);
    else if ((id == 133 || id == 16) && selectedmap >= 0) {
        skirmish.seed = (uint8_t)SDL_GetTicks();
        snprintf(mapname, sizeof(mapname), "%s", maps[selectedmap].path);
        DC_RequestSkirmish(mapname, &skirmish);
        menumap = mapname;
        menuactive = false;
        SDL_StopTextInput();
    }
    refresh_skirmish();
}

/* Screens start their decorative gadgets only after the banim entrance:
 * 0x404b8c..0x404b9c (INTROE DCSS logo), 0x401f26..0x401f70 (NEWGAMEE),
 * 0x403340..0x403386 (SHUMANE). */
static void start_page_animations(void) {
    if (page == MAIN) animate(14, 1);
    if (page == SETUP) {
        animate(race ? 26 : 23, 0);
        for (int i = 13; i <= 16; ++i) animate(i, 0);
        for (int i = 29; i <= 35; ++i) animate(i, 0);
    }
    if (page == BRIEFING) {
        for (int i = 15; i <= 24; ++i) animate(i, 0);
        animate(39, 0);
        animate(race ? 35 : 36, 0);
    }
}

static bool load_screen(int next) {
    static const char *const scripts[] = {"INTROE", "NEWGAMEE", "STORYE", "SHUMANE", "MULTIE", "LQCE", "NETOPTE", "IPXNAMEE", "DPLAYSE", "GETSVRE"};
    static const char *const lists[] = {"INTRO.DAT", "CHOO.DAT", "LOADG.DAT", "SHUMAN.DAT", "TCPWAIT.DAT", NULL, "NET.DAT", "SERVER.DAT", "LOADG.DAT", "SERVER.DAT"};
    free_screen();
    page = next;
    itemOn = page == STORY ? 5 : page == BRIEFING ? 2 : 0;
    scroll = 0;
    notice = NULL;
    char path[1024], line[512], name[128], palette_path[1024] = "";
    if (lists[page] && !load_animations(M_va("INTRFACE/%s", lists[page]))) return false;
    if (page == QUIT) {
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
    bool ok = true;
    while (fgets(line, sizeof(line), file)) {
        char kind[32];
        int id, desc;
        irect_t rect;
        if (sscanf(line, "size %d %d %d %d", &rect.x, &rect.y, &rect.w, &rect.h) == 4) {
            screen = (isize2_t){640, 480};
            continue;
        }
        if (sscanf(line, "size %d %d", &screen.w, &screen.h) == 2) continue;
        if (sscanf(line, "bright_pushed %d", &bright_pushed) == 1 ||
            sscanf(line, "bright_highlight %d", &bright_highlight) == 1) continue;
        if (sscanf(line, "background %127s", name) == 1) {
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
            if (id < 0 || id >= 300) { ok = false; break; }
            strcpy(messages[id], name);
        } else if (sscanf(line, "%31s %d %d %d %d %d %d", kind, &id, &desc,
                           &rect.x, &rect.y, &rect.w, &rect.h) == 7 &&
                   (!strcmp(kind, "pushb") || !strcmp(kind, "checkb") ||
                    !strcmp(kind, "label") || !strcmp(kind, "in_text") || !strcmp(kind, "gadget") ||
                    !strcmp(kind, "picture") || !strcmp(kind, "list") || !strcmp(kind, "scroll"))) {
            if (id < 0 || id >= 300 || rect.w <= 0 || rect.h <= 0) { ok = false; break; }
            menucontrol_t *control = &controls[id];
            *control = (menucontrol_t){ .rect = rect, .visible = true, .remap = 7,
                                       .normal = -1, .pushed = -1 };
            control->kind = !strcmp(kind, "pushb") ? PUSH : !strcmp(kind, "checkb") ? CHECK :
                !strcmp(kind, "label") ? LABEL : !strcmp(kind, "in_text") ? TEXT :
                !strcmp(kind, "picture") ? PICTURE : !strcmp(kind, "list") ? LIST :
                !strcmp(kind, "scroll") ? SCROLL : GADGET;
            if (control->kind == PUSH || control->kind == CHECK || control->kind == PICTURE)
                sscanf(line, "%*s %*d %*d %*d %*d %*d %*d %d %d", &control->normal, &control->pushed);
            control->writable = strstr(line, "read_write") != NULL;
            control->centered = strstr(line, "align centre") || strstr(line, "align  centre");
            char *label = strstr(line, "label centre ");
            if (label) {
                sscanf(label, "label centre %d %d", &control->message, &control->font);
                control->centered = true;
            } else if ((label = strstr(line, " label "))) sscanf(label, " label %d", &control->message);
            if ((label = strstr(line, "remap "))) sscanf(label, "remap %d", &control->remap);
            if ((label = strstr(line, " font "))) sscanf(label, " font %d", &control->font);
            if (control->kind == TEXT) {
                control->maxchars = rect.w;
                sscanf(line, "%*s %*d %*d %*d %*d %*d %*d %d", &control->font);
                if ((label = strstr(line, " init "))) sscanf(label, " init %d", &control->message);
            }
            if (control->message < 0 || control->message >= 300 || control->font < 0 || control->font >= 3) { ok = false; break; }
            if (control->kind == GADGET) {
                sscanf(line, "%*s %*d %*d %*d %*d %*d %*d %31s", control->animation);
                for (int i = 0; i < numanimations && !control->sequence; ++i) {
                    control->fin = &animations[i].fin;
                    control->sequence = DC_FINLabel(control->fin, control->animation);
                }
                if (!control->sequence) { ok = false; break; }
                animate(id, strstr(line, "anim_oneoff") ? 1 : strstr(line, "anim_loop") ? 0 : 2);
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
                if (value < 0 || value >= 300) { ok = false; break; }
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
    for (int i = 0; i < 300; ++i) {
        menucontrol_t *control = &controls[i];
        if (control->message) strcpy(control->text, messages[control->message]);
        if (control->kind == TEXT) {
            const bitmapfont_t *font = &fonts[control->font];
            control->rect.w *= font->glyph_size.w + 1;
            control->rect.h *= font->line_h;
        }
    }
    if (page == SETUP) {
        controls[training ? 3 : 2].visible = false;
        controls[19].visible = controls[20].visible = false;
        for (int i = 21; i <= 26; ++i) controls[i].visible = false;
        controls[race ? 26 : 23].visible = true;
        snprintf(controls[5].text, sizeof(controls[5].text), "%s", leader);
        SDL_StartTextInput();
    } else SDL_StopTextInput();
    if (page == STORY && !read_text(race ? "INTRFACE/ASTORY.TXT" : "INTRFACE/HSTORY.TXT")) ok = false;
    if (page == BRIEFING) {
        controls[10].visible = false;
        controls[race ? 36 : 35].visible = false;
        snprintf(controls[5].text, sizeof(controls[5].text), "%s", leader);
        snprintf(controls[6].text, sizeof(controls[6].text), "%s", mission_title);
        snprintf(controls[7].text, sizeof(controls[7].text), "%s", mission_region);
        if (!read_text(M_va("%.*s.TXT", (int)strlen(mapname) - 4, mapname))) ok = false;
    }
    if (page == SKIRMISH) {
        selectedmap = -1;
        mapscroll = 0;
        if (!load_skirmish_maps()) ok = false;
        for (int i = 166; i <= 173; ++i) controls[i].visible = i == 166;
        for (int i = 180; i <= 187; ++i) controls[i].visible = i == 180;
        for (int i = 17; i <= 23; ++i) controls[i].visible = false;
        refresh_skirmish();
        SDL_StartTextInput();
    }
    if (page == NETWORK) {
        controls[0].checked = true;
        for (int i = 1; i <= 3; ++i) {
            controls[i].visible = false;
            controls[7 + i].visible = false;
        }
    }
    if (page == SESSION_NAME || page == CONNECT) {
        itemOn = page == SESSION_NAME ? 1 : 3;
        controls[itemOn].writable = true;
        snprintf(controls[itemOn].text, sizeof(controls[itemOn].text), "%s",
                 page == SESSION_NAME ? session_name : server_address);
        SDL_StartTextInput();
    }
    if (page == BROWSE) {
        itemOn = 0;
        server_scroll = 0;
        selected_server[0] = '\0';
        strcpy(controls[6].text, "Select LAN Session");
        strcpy(controls[5].text, "JOIN");
        /* Use the native button row's unused interval for direct connection. */
        controls[18] = controls[4];
        controls[18].rect.x = controls[17].rect.x + controls[17].rect.w;
        strcpy(controls[18].text, "ADDRESS");
    }
    entrance = 0;
    if (!numentrances) start_page_animations();
    menutime = SDL_GetTicks64();
    return ok && screen.w > 0 && screen.h > 0 &&
        (page == QUIT || background.numlumps) && fonts[0].sprite.numlumps;
}

bool M_Init(app_t *app, const char *data_root) {
    (void)app;
    menuerror = false;
    if (strlen(data_root) >= sizeof(root)) return false;
    strcpy(root, data_root);
    initialized = load_screen(MAIN);
    if (!initialized) M_Shutdown();
    return initialized;
}

void M_StartControlPanel(app_t *app) {
    if (!initialized || menuactive) return;
    if (page != MAIN && !load_screen(MAIN)) {
        fprintf(stderr, "Could not load Dark Colony main menu\n");
        menuerror = true;
        app->running = false;
        return;
    }
    menuactive = true;
    itemOn = 0;
    notice = NULL;
    app->dragging_select = false;
    app->selection_rect = (irect_t){0};
}

void DC_OpenQuitDialog(app_t *app) {
    if (!initialized || menuactive) return;
    if (!load_screen(QUIT)) {
        fprintf(stderr, "Could not load Dark Colony quit dialog\n");
        menuerror = true;
        return;
    }
    itemOn = 57;
    menuactive = true;
    app->dragging_select = false;
    app->selection_rect = (irect_t){0};
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
    snprintf(network_notice, sizeof(network_notice), "%.127s", neterror);
    I_CancelNetGame();
    waiting = false;
    notice = network_notice;
    if (page == CONNECT) snprintf(controls[3].text, sizeof(controls[3].text), "%s", network_notice);
}

static bool join_session(const char *address) {
    char copy[128];
    snprintf(copy, sizeof(copy), "%s", address);
    network_notice[0] = '\0';
    if (!I_JoinNetGame("dark-colony", copy)) { network_failure(); return true; }
    waiting = true;
    return load_screen(CONNECT);
}

static void activate(app_t *app, int id, bool inlevel) {
    bool ok = true;
    notice = NULL;
    if (page == QUIT) {
        if (id == 56) app->running = false;
        if (id == 56 || id == 57) menuactive = false;
    } else if (page == MAIN) {
        if (id == 12) app->running = false;
        else if ((id == 0 || id == 1) && !netgame) {
            training = id == 1;
            race = 0;
            ok = load_screen(SETUP);
        } else if (id == 3 && !netgame) {
            lan = true;
            network_notice[0] = '\0';
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
        } else notice = inlevel ? "Escape resumes the current game" : "This menu is not implemented yet";
    } else if (page == SETUP) {
        if (id == 0 || id == 1) {
            race = id;
            controls[23].visible = race == 0;
            controls[26].visible = race == 1;
            animate(race ? 26 : 23, 0);
        } else if (id == 4) ok = load_screen(MAIN);
        else if (id == 2 || id == 3) {
            if (!leader[0]) { itemOn = 5; notice = "Type a name for your leader"; }
            else ok = first_mission() && load_screen(training ? BRIEFING : STORY);
        }
    } else if (page == STORY) {
        if (id == 4) ok = load_screen(SETUP);
        else if (id == 5) ok = load_screen(BRIEFING);
        else if (id == 2) { if (scroll > 0) --scroll; }
        else if (id == 3) ++scroll;
    } else if (page == NETWORK) {
        if (id == 6) ok = load_screen(MAIN);
        else if (id == 4) ok = load_screen(SESSION_NAME);
        else if (id == 5) {
            network_notice[0] = '\0';
            ok = load_screen(BROWSE);
            if (ok && !I_OpenNetBrowser("dark-colony")) network_failure();
        }
    } else if (page == SESSION_NAME) {
        if (id == 0) {
            if (!session_name[0]) notice = "Enter a session name";
            else {
                skirmish = (dc_skirmish_t){0};
                for (int i = 0; i < 8; ++i)
                    skirmish.players[i] = (dc_skirmish_player_t){
                        .type = i < 2 ? DC_PLAYER_HUMAN : DC_PLAYER_NONE};
                strcpy(skirmish.players[0].name, "Host");
                network_notice[0] = '\0';
                ok = load_screen(SKIRMISH);
            }
        } else if (id == 1) itemOn = 1;
    } else if (page == BROWSE) {
        if (id == 4) { I_CancelNetGame(); ok = load_screen(NETWORK); }
        else if (id == 17) {
            network_notice[0] = '\0';
            if (!I_OpenNetBrowser("dark-colony")) network_failure();
            selected_server[0] = '\0';
            server_scroll = 0;
        } else if (id == 18) { I_CancelNetGame(); ok = load_screen(CONNECT); }
        else if (id == 5 && selected_server[0]) ok = join_session(selected_server);
        else if (id == 2 && server_scroll > 0) --server_scroll;
        else if (id == 3) ++server_scroll;
    } else if (page == CONNECT) {
        if (id == 1) { I_CancelNetGame(); waiting = false; ok = load_screen(NETWORK); }
        else if (id == 0 && !waiting) ok = join_session(server_address);
    } else if (page == SKIRMISH) {
        if (id == 132) {
            if (lan) I_CancelNetGame();
            waiting = false;
            ok = load_screen(lan ? NETWORK : MAIN);
        } else if (lan && !waiting) {
            if (id == 90 || id == 91) {
                int count = active_players();
                count = count > id - 88 ? id - 88 : id - 87;
                for (int i = 1; i < 8; ++i)
                    skirmish.players[i].type = i < count ? DC_PLAYER_HUMAN : DC_PLAYER_NONE;
            } else if (id == 133 && selectedmap >= 0) {
                if (I_HostNetGame("dark-colony", session_name, maps[selectedmap].path, active_players())) waiting = true;
                else network_failure();
            } else if (id == 28 || id == 29) scroll_maps(id == 28 ? -1 : 1);
            refresh_skirmish();
        } else if (!lan) activate_skirmish(id);
    } else if (page == BRIEFING) {
        if (id == 0) ok = load_screen(training ? SETUP : STORY);
        else if (id == 2) { menumap = mapname; menuactive = false; }
        else if (id == 3) ++scroll;
        else if (id == 4) { if (scroll > 0) --scroll; }
        else if (id == 1) notice = "Encyclopedia is not implemented yet";
    }
    if (!ok) {
        fprintf(stderr, "Could not load Dark Colony menu screen %d\n", page);
        menuerror = true;
        app->running = false;
    }
}

static bool selectable(int id) {
    const menucontrol_t *control = &controls[id];
    if (page == CONNECT && waiting) return id == 1;
    if (page == CONNECT && id == 3) return !waiting;
    if (page == SESSION_NAME && id == 1) return true;
    if (page == BROWSE) {
        if (id == 5) return selected_server[0] != '\0';
        if (id == 0 || id == 1) return true;
    }
    if (page == SKIRMISH) {
        if (lan) {
            if (waiting) return id == 132;
            if (id == 90 || id == 91) return true;
            if (id == 132) return true;
            if (id == 133) return selectedmap >= 0;
            return id == 27 || id == 28 || id == 29 || id == 30;
        }
        if (id == 88 || (id > 32 && id < 40) || (id > 40 && id < 48)) return false;
        if (id >= 9 && id < 16 && skirmish.players[id - 8].type == DC_PLAYER_NONE) return false;
        if (id >= 150 && id <= 165 && skirmish.players[(id - 150) % 8].type == DC_PLAYER_NONE) return false;
        if ((id == 16 || id == 133) && selectedmap < 0) return false;
        if (id == 0 || control->kind == LIST || control->kind == SCROLL || control->writable)
            return control->visible;
    }
    return control->visible && (control->kind == PUSH || control->kind == CHECK ||
                                (page == SETUP && id == 5));
}

static void drag_server_scroll(ivec2_t point) {
    int count;
    I_NetGames(&count);
    irect_t bar = controls[1].rect;
    server_scroll = (point.y - bar.y) * count / bar.h -
                    controls[0].rect.h / fonts[0].glyph_size.h / 2;
    if (server_scroll < 0) server_scroll = 0;
}

/* The native screens hard-wire their lists, scroll bars and editable fields
 * by control id. Every other selectable control acts as a button. */
static menuitemkind_t item_kind(int id) {
    if (page == BROWSE && id <= 1) return id ? MI_SCROLLBAR : MI_LIST;
    if (page == SKIRMISH && (id == 27 || id == 30)) return id == 27 ? MI_LIST : MI_SCROLLBAR;
    if ((page == SETUP && id == 5) || (page == SESSION_NAME && id == 1) ||
        (page == CONNECT && id == 3) || (page == SKIRMISH && id == 0))
        return MI_TEXTFIELD;
    switch (controls[id].kind) {
    case CHECK: return MI_CHECK;
    case PICTURE: return MI_PICTURE;
    case GADGET: return MI_CUSTOM;
    case LABEL: case TEXT: case NONE: return MI_LABEL;
    case PUSH: case LIST: case SCROLL: return MI_BUTTON;
    }
    return MI_LABEL;
}

static void menu_escape(menu_t *menu) {
    app_t *app = menu->owner;
    if (page == QUIT) menuactive = false;
    else if (page == SESSION_NAME) {
        if (!load_screen(NETWORK)) { menuerror = true; app->running = false; }
    } else if (page != MAIN) activate(app, page == SKIRMISH ? 132 : page == NETWORK ? 6 :
        page == CONNECT ? 1 : page == BROWSE || page == SETUP || page == STORY ? 4 : 0, menu->inlevel);
    else if (menu->inlevel) menuactive = false;
}

static void menu_wheel(menu_t *menu, int delta) {
    (void)menu;
    if (page == BROWSE) {
        server_scroll -= delta;
        if (server_scroll < 0) server_scroll = 0;
    } else if (page == SKIRMISH && !waiting) scroll_maps(-delta);
    else if (page == STORY || page == BRIEFING) {
        scroll -= delta;
        if (scroll < 0) scroll = 0;
    }
}

static void copy_field(char *dst, size_t dst_size, const char *src, int id) {
    snprintf(dst, dst_size, "%s", src ? src : "");
    snprintf(controls[id].text, sizeof(controls[id].text), "%s", dst);
}

/* Mouse sets the control's pressed flag before MA_ACTIVATE. Enter does not. */
#undef pressed
static void menu_routine(menu_t *menu, menuitem_t *item, menuaction_t action) {
    app_t *app = menu->owner;
    int id = item->userid;
    if (action == MA_CHANGE && item->kind == MI_TEXTFIELD) {
        if (page == SESSION_NAME && id == 1) copy_field(session_name, sizeof(session_name), item->text, id);
        else if (page == CONNECT && id == 3 && !waiting)
            copy_field(server_address, sizeof(server_address), item->text, id);
        else if (page == SKIRMISH && id == 0 && !lan) {
            snprintf(skirmish.players[0].name, sizeof(skirmish.players[0].name), "%s", item->text);
            refresh_skirmish();
        } else if (page == SETUP && id == 5) copy_field(leader, sizeof(leader), item->text, id);
        else return;
        if (!(page == SKIRMISH && id == 0)) notice = NULL;
        return;
    }
    if (action == MA_CHANGE && item->kind == MI_SCROLLBAR) {
        if (page == BROWSE && id == 1) drag_server_scroll(menu->cursor);
        else if (page == SKIRMISH && id == 30) drag_map_scroll(menu->cursor);
        return;
    }
    if (action == MA_ROW && page == BROWSE && id == 0) {
        int count, row = item->value;
        const netgame_t *games = I_NetGames(&count);
        if (item->step != 0) {
            row = -1;
            for (int i = 0; i < count; ++i)
                if (!strcmp(games[i].address, selected_server)) row = i;
            row += item->step;
            if (row < 0) row = 0;
        }
        if (row >= 0 && row < count) strcpy(selected_server, games[row].address);
        if (item->step != 0) {
            int rows = controls[0].rect.h / fonts[0].glyph_size.h;
            if (row < server_scroll) server_scroll = row;
            if (row >= server_scroll + rows) server_scroll = row - rows + 1;
        }
        return;
    }
    if (action == MA_ROW && page == SKIRMISH && id == 27) {
        if (item->step != 0) {
            int row = 0;
            if (selectedmap >= 0) {
                while (filtered_map(row) >= 0 && filtered_map(row) != selectedmap) ++row;
                row += item->step;
            }
            if (row < 0) row = 0;
            if (filtered_map(row) >= 0) {
                selectedmap = filtered_map(row);
                if (row < mapscroll) mapscroll = row;
                if (row >= mapscroll + visible_map_rows()) mapscroll = row - visible_map_rows() + 1;
                refresh_skirmish();
            }
        } else {
            selectedmap = filtered_map(item->value);
            refresh_skirmish();
        }
        return;
    }
    if (action != MA_ACTIVATE) return;
    /* Enter on a field or the session list confirms the screen. A mouse click
     * acts on the control it hit. */
    if (!item->pressed) {
        if (page == SETUP && id == 5) id = training ? 2 : 3;
        else if (page == SESSION_NAME && id == 1) id = 0;
        else if (page == CONNECT && id == 3) id = 0;
        else if (page == BROWSE && id == 0) id = 5;
    }
    activate(app, id, menu->inlevel);
}
#define pressed dc_menu.grab

static void bind_menu(app_t *app, bool inlevel) {
    int row_h = fonts[0].glyph_size.h > 0 ? fonts[0].glyph_size.h : 1;
    for (int i = 0; i < 300; ++i) {
        menucontrol_t *c = &controls[i];
        menuitem_t *item = &dc_items[i];
        bool on = selectable(i);
        *item = (menuitem_t){
            .kind = item_kind(i),
            .rect = c->rect,
            .visible = c->visible || on,
            .enabled = on,
            .maxchars = c->maxchars,
            .routine = menu_routine,
            .userid = i,
            .row_height = row_h,
            .first_row = i == 0 ? server_scroll : mapscroll,
        };
        M_MenuSetText(item, c->text);
    }
    dc_menu.items = dc_items;
    dc_menu.numitems = 300;
    dc_menu.space = screen;
    dc_menu.owner = app;
    dc_menu.inlevel = inlevel;
    dc_menu.escape = menu_escape;
    dc_menu.wheel = menu_wheel;
    dc_menu.ticker = NULL;
}

bool M_Responder(app_t *app, const SDL_Event *event, bool inlevel) {
    if (!initialized) return false;
    if (event->type == SDL_QUIT) { app->running = false; return true; }
    if (event->type == SDL_WINDOWEVENT) return false;
    if (!menuactive) {
        if (event->type != SDL_KEYDOWN || event->key.keysym.sym != SDLK_ESCAPE) return false;
        if (!event->key.repeat) M_StartControlPanel(app);
        return true;
    }
    bind_menu(app, inlevel);
    M_MenuOpen(&dc_menu);
    return M_MenuResponder(event);
}

static void step_entrances(void) {
    while (entrance < numentrances) {
        menuentrance_t *e = &entrances[entrance];
        if (e->started + 1 < e->count) {
            const menucontrol_t *g = &controls[e->gadgets[e->started]];
            /* 0x425257..0x425294: the next gadget starts when the running one
             * reaches its third frame, so the entrances overlap. */
            if (!g->visible || !g->sequence || g->frame - g->sequence->start == 2)
                animate(e->gadgets[++e->started], 1);
        }
        if (e->finished < e->count) {
            menucontrol_t *g = &controls[e->gadgets[e->finished]];
            /* 0x4252a5..0x4252be: a stopped gadget is hidden and the push
             * button beneath it is redrawn. */
            if (!g->sequence || g->mode == 2) {
                g->visible = false;
                ++e->finished;
            }
        }
        if (e->finished < e->count) return;
        if (++entrance == numentrances) start_page_animations();
    }
}

void M_Ticker(void) {
    if (!menuactive) return;
    if (waiting) {
        int status = I_PollNetGame(mapname, sizeof(mapname));
        if (status < 0) {
            network_failure();
            if (page == SKIRMISH) refresh_skirmish();
            else snprintf(controls[3].text, sizeof(controls[3].text), "%s", network_notice);
        } else if (status > 0) {
            waiting = false;
            menumap = mapname;
            menuactive = false;
            SDL_StopTextInput();
            return;
        } else if (page == SKIRMISH) {
            snprintf(network_notice, sizeof(network_notice), "Waiting for LAN players: %d/%d\nEscape cancels the session.",
                     I_NetPlayerCount(), doomcom->numplayers);
            refresh_skirmish();
        } else {
            strcpy(controls[3].text, "Connecting... Escape cancels");
        }
    }
    if (page == BROWSE) {
        int count;
        const netgame_t *games = I_NetGames(&count);
        bool found = false;
        for (int i = 0; i < count; ++i) found |= !strcmp(games[i].address, selected_server);
        if (!found) selected_server[0] = '\0';
        int rows = controls[0].rect.h / fonts[0].glyph_size.h;
        if (server_scroll > count - rows) server_scroll = count - rows;
        if (server_scroll < 0) server_scroll = 0;
        if (neterror[0]) network_failure();
    }
    uint64_t now = SDL_GetTicks64();
    if (now - menutime <= 16) return; /* DC.EXE 0x421ebd: menu cadence. */
    menutime = now;
    for (int i = 0; i < 300; ++i) {
        menucontrol_t *c = &controls[i];
        if (!c->visible || !c->sequence || c->mode == 2) continue;
        if (!c->delay) {
            if (++c->frame > c->sequence->end) {
                /* Gadget ticker 0x422828 holds the last one-off pose;
                 * the world animation ticker 0x423dd0 resets to zero. */
                if (c->mode == 1) {
                    c->frame = c->sequence->end;
                    c->mode = 2;
                    continue;
                }
                c->frame = c->sequence->start;
            }
            int raw = c->fin->frames[c->frame].ticks;
            c->delay = ((raw ? raw : 15) + 3) * 15 / 100;
        }
        if (c->delay) --c->delay;
    }
    step_entrances();
}

static void draw_mapped(ivec2_t at, const bitmapfont_t *font, const char *text, int palette_id) {
    HU_DrawText(at, font, text, R_PaletteMap(&font->sprite, palette_id), 1);
}

static void draw_gadget(const menucontrol_t *c) {
    const dc_fin_frame_t *frame = &c->fin->frames[c->frame];
    const dc_fin_command_t *parts = c->fin->frame_commands[c->frame];
    for (int i = 0; i < frame->part_count; ++i) {
        const dc_fin_command_t *part = &parts[i];
        char key[32];
        snprintf(key, sizeof(key), "SPRITES/%.8s.SPR", part->sprite);
        const spritesheet_t *sprite = R_CacheLookup(&images, M_Upper(key));
        if (!sprite || part->cell < 0 || part->cell >= sprite->numlumps) continue;
        const spritecell_t *cell = &sprite->cells[part->cell];
        /* 0x4224d7..0x4224ef passes only the dependency and cell to the UI
         * blitter. World FIN offsets/flags do not position menu gadgets. */
        irect_t dst = {c->rect.x + cell->displacement.x,
                        c->rect.y + cell->displacement.y, cell->rect.w, cell->rect.h};
        R_DrawSprite(sprite, part->cell, -1, &cell->rect, &dst, 0, 16);
    }
}

static int control_intensity(const menucontrol_t *c) {
    bool pushed = c->checked || (pressed >= 0 && c == &controls[pressed]);
    int frame = pushed ? c->pushed : c->normal;
    int intensity = 16;
    if (frame < 0) intensity = -frame;
    if (pushed) intensity += bright_pushed;
    else if (c == &controls[itemOn]) intensity += bright_highlight;
    return intensity > 31 ? 31 : intensity;
}

static void draw_picture(const menucontrol_t *c) {
    bool pushed = c->checked || (pressed >= 0 && c == &controls[pressed]);
    int frame = pushed ? c->pushed : c->normal;
    if (frame < 0) frame = pushed ? c->normal : c->pushed;
    if (frame < 0 || frame >= pictures.numlumps) return;
    const spritecell_t *cell = &pictures.cells[frame];
    irect_t dst = {c->rect.x + cell->displacement.x, c->rect.y + cell->displacement.y,
                   cell->rect.w, cell->rect.h};
    R_DrawSprite(&pictures, frame, control_intensity(c) * 8 + c->remap, &cell->rect, &dst, 0, 16);
}

static void draw_scroll_chrome(irect_t bar, irect_t thumb, bool show) {
    V_FillRect(bar, V_NearestIndex(0xff000000u));
    if (!show) return;
    uint8_t red = V_NearestIndex(0xffff0000u); /* Native default colour 1. */
    V_DrawRectOutline(bar, red);
    V_FillRect(thumb, red);
}

static void draw_map_list(void) {
    irect_t clip = controls[27].rect;
    V_SetClip(clip);
    for (int row = 0; row < visible_map_rows(); ++row) {
        int index = filtered_map(mapscroll + row);
        if (index < 0) break;
        int y = clip.y + row * fonts[0].glyph_size.h;
        if (index == selectedmap)
            V_FillRect((irect_t){clip.x, y, clip.w, fonts[0].glyph_size.h},
                       V_NearestIndex(0xff00ffffu)); /* MULTIE selbg cyan. */
        draw_mapped((ivec2_t){clip.x, y}, &fonts[0], maps[index].label,
                    (index == selectedmap ? 0 : 16 * 8) + 7);
    }
    V_SetClip((irect_t){0});
    /* 0x427f74..0x427fa1: scroll start/end scaled by total list length. */
    int count = 0;
    while (filtered_map(count) >= 0) ++count;
    irect_t bar = controls[30].rect;
    int rows = visible_map_rows();
    int end = mapscroll + rows < count ? mapscroll + rows : count;
    irect_t thumb = {bar.x, bar.y + (count ? bar.h * mapscroll / count : 0),
                     bar.w, count ? bar.h * end / count - bar.h * mapscroll / count : 0};
    draw_scroll_chrome(bar, thumb, count > 0);
}

static void draw_sessions(void) {
    int count;
    const netgame_t *games = I_NetGames(&count);
    irect_t clip = controls[0].rect;
    V_SetClip(clip);
    if (!count)
        HU_DrawTextWrapped(clip, &fonts[0],
            network_notice[0] ? network_notice : "Searching for LAN games...\nUse ADDRESS to connect directly.",
            NULL, 1);
    for (int i = server_scroll; i < count; ++i) {
        int y = clip.y + (i - server_scroll) * fonts[0].glyph_size.h;
        if (y >= clip.y + clip.h) break;
        bool selected = !strcmp(games[i].address, selected_server);
        if (selected)
            V_FillRect((irect_t){clip.x, y, clip.w, fonts[0].glyph_size.h},
                       V_NearestIndex(0xff00ff00u)); /* DPLAYSE colour sel. */
        char label[128];
        snprintf(label, sizeof(label), "%.24s  %d/%d", games[i].name[0] ? games[i].name : games[i].map,
                 games[i].players, games[i].capacity);
        draw_mapped((ivec2_t){clip.x, y}, &fonts[0], label, (selected ? 0 : 16 * 8) + 4);
    }
    V_SetClip((irect_t){0});
    irect_t bar = controls[1].rect;
    int rows = clip.h / fonts[0].glyph_size.h;
    int end = server_scroll + rows < count ? server_scroll + rows : count;
    irect_t thumb = {bar.x, bar.y + (count ? bar.h * server_scroll / count : 0),
                     bar.w, count ? bar.h * (end - server_scroll) / count : 0};
    draw_scroll_chrome(bar, thumb, count > 0);
}

static void draw_text(int i) {
    const menucontrol_t *c = &controls[i];
    if (!c->visible || !c->text[0]) return;
    const bitmapfont_t *font = &fonts[c->font];
    ivec2_t at = {c->rect.x, c->rect.y};
    if (c->centered) at = ivec2_add(at, (ivec2_t){(c->rect.w - HU_TextWidth(font, c->text, 1)) / 2, (c->rect.h - font->glyph_size.h) / 2});
    else if (c->kind == LABEL) at = ivec2_add(at, (ivec2_t){font->glyph_size.w, (c->rect.h - font->glyph_size.h) / 2});
    if (c->centered && c->kind != TEXT)
        at = ivec2_add(at, (ivec2_t){(font->glyph_size.w + 1) / 2, 0});
    int intensity = c->kind == PUSH || c->kind == CHECK ? control_intensity(c) : 16;
    if (page == CONNECT && i == 3 && notice == network_notice) {
        HU_DrawTextWrapped((irect_t){at.x, at.y, c->rect.w, c->rect.h}, font, c->text, NULL, 1);
        return;
    }
    draw_mapped(at, font, c->text, intensity * 8 + c->remap);
}

void M_Drawer(const app_t *app) {
    if (!menuactive) return;
    I_SetPalette(background.source_palette);
    irect_t dst = {0, 0, screen.w, screen.h};
    if (background.numlumps)
        R_DrawSprite(&background, 0, -1, NULL, &dst, V_OPAQUE, 16);
    for (int i = 0; i < 300; ++i) {
        const menucontrol_t *c = &controls[i];
        if (c->visible && c->sequence) draw_gadget(c);
        if (c->visible && (c->kind == PICTURE || c->kind == PUSH || c->kind == CHECK))
            draw_picture(c);
        /* Native controls draw in id order with their own label, so a later
         * entrance gadget covers both the button image and its text. */
        if (c->kind == PUSH || c->kind == CHECK) draw_text(i);
    }
    if (page == SKIRMISH) draw_map_list();
    if (page == BROWSE) draw_sessions();
    for (int i = 0; i < 300; ++i)
        if (controls[i].kind != PUSH && controls[i].kind != CHECK) draw_text(i);
    if (prose) {
        /* Text viewport arguments at 0x4023f8/0x403030, separate from widgets.
         * The wrapper ignores scale; the box origin is already scrolled. */
        irect_t box = page == STORY ? (irect_t){10, 13, 579, 420} : (irect_t){310, 212, 294, 225};
        box.y -= scroll * fonts[0].line_h;
        HU_DrawTextWrapped(box, &fonts[0], prose, NULL, 1);
    }
    if (app && app->window)
        SDL_SetWindowTitle(app->window, notice ? notice : "Dark Colony");
}
