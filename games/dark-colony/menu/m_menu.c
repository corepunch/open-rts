#define _DEFAULT_SOURCE
#include "engine.h"
#include "dark-colony.h"

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
static int race;
static int bright_pushed, bright_highlight;
/* The native screen object has 300 controls (0x34 bytes each at +0x88); an
 * item's index is its native control ID. The story text is one more item. */
enum { NUMCONTROLS = 300, PROSE = NUMCONTROLS, NUMITEMS };
static menuitem_t items[NUMITEMS];
static bool initialized, training, inlevel;
static void menu_escape(menu_t *screen);
static int gadget_tics(const menuitem_t *item);
static menu_t menu = {.items = items, .numitems = NUMITEMS, .modal = true, .escape = menu_escape,
                      .frametics = gadget_tics};
static uint64_t menutime;
static const char *notice;
static char mission_title[128], mission_region[128];
static char *prose;
static enum { MAIN, SETUP, STORY, BRIEFING, SKIRMISH, QUIT, NETWORK, SESSION_NAME, BROWSE, CONNECT } page;
static bool lan, waiting;
static char session_name[32] = "Dark Colony", server_address[128] = "127.0.0.1";
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
    memset(items, 0, sizeof(items));
    memset(messages, 0, sizeof(messages));
    numentrances = entrance = 0;
    bright_pushed = bright_highlight = 0;
    menu.held = NULL;
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
        dc_fin_t *fin = &animations[numanimations++];
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

static void gadget_pose(int id, int pose) {
    menuanim_t *anim = &items[id].anim;
    if (items[id].userdata && pose >= 0 && anim->first + pose <= anim->last) {
        anim->frame = anim->first + pose;
        anim->mode = MANIM_STOPPED;
    }
}

/* LAN lobby settings travel to joiners as 40 explicit bytes. */
enum { LAN_SETUP_SIZE = 40 };
static size_t pack_setup(uint8_t out[LAN_SETUP_SIZE]) {
    for (int i = 0; i < 8; ++i) {
        const dc_skirmish_player_t *p = &skirmish.players[i];
        out[i * 4] = (uint8_t)p->race; out[i * 4 + 1] = (uint8_t)p->type;
        out[i * 4 + 2] = (uint8_t)p->color; out[i * 4 + 3] = (uint8_t)p->team;
    }
    uint8_t *v = out + 32;
    v[0] = (uint8_t)skirmish.storage; v[1] = (uint8_t)skirmish.artifacts;
    v[2] = (uint8_t)skirmish.erupting; v[3] = (uint8_t)skirmish.renewable;
    v[4] = (uint8_t)skirmish.flow; v[5] = (uint8_t)skirmish.quantity;
    v[6] = (uint8_t)skirmish.rank; v[7] = skirmish.seed;
    return LAN_SETUP_SIZE;
}

static bool unpack_setup(const uint8_t *in, size_t size, dc_skirmish_t *out) {
    if (size != LAN_SETUP_SIZE) return false;
    *out = (dc_skirmish_t){0};
    for (int i = 0; i < 8; ++i) {
        if (in[i * 4] > 1 || in[i * 4 + 1] > DC_PLAYER_NONE || in[i * 4 + 2] > 7 || in[i * 4 + 3] > 7) return false;
        out->players[i] = (dc_skirmish_player_t){.race = in[i * 4], .type = in[i * 4 + 1],
                                                 .color = in[i * 4 + 2], .team = in[i * 4 + 3]};
        snprintf(out->players[i].name, sizeof(out->players[i].name), "%s", i ? "LAN Player" : "Host");
    }
    const uint8_t *v = in + 32;
    out->storage = v[0]; out->artifacts = v[1]; out->erupting = v[2]; out->renewable = v[3];
    out->flow = v[4]; out->quantity = v[5]; out->rank = v[6]; out->seed = v[7];
    return out->flow >= 1 && out->flow <= 20 && out->quantity >= 1 && out->quantity <= 20 && out->rank <= 3;
}

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
    strcpy(items[129].text, messages[(skirmish.players[0].race ? 40 : 30) + skirmish.rank]);
    if (selectedmap >= 0 && maps[selectedmap].players < active_players()) selectedmap = -1;
    snprintf(items[26].text, sizeof(items[26].text), "%s", selectedmap < 0 ? "" : maps[selectedmap].title);
    if (lan) {
        for (int i = 0; i < 8; ++i) {
            if (i) snprintf(items[i].text, sizeof(items[i].text), "%s",
                            i < active_players() ? "LAN Player" : "");
            items[16 + i].visible = false;
            items[180 + i].visible = false;
            items[8 + i].visible = items[80 + i].visible = i < active_players();
            items[96 + i].visible = items[142 + i].visible = i < active_players();
            items[32 + i].visible = items[40 + i].visible = i < active_players();
            items[150 + i].visible = items[158 + i].visible = i < active_players();
        }
        items[137].visible = items[139].visible = items[140].visible = items[141].visible = false;
        snprintf(items[133].text, sizeof(items[133].text), "%s", waiting ? "WAITING" : "CREATE");
        snprintf(items[24].text, sizeof(items[24].text), "%s", network_notice[0] ? network_notice :
                 "Select map and 2-4 LAN player slots.\nMap supplies factions and game settings.\nStarts when all players connect.");
        items[25].visible = false;
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
        menuactive = false;
        SDL_StopTextInput();
    }
    refresh_skirmish();
}

/* Screens start their decorative gadgets only after the banim entrance:
 * 0x404b8c..0x404b9c (INTROE DCSS logo), 0x401f26..0x401f70 (NEWGAMEE),
 * 0x403340..0x403386 (SHUMANE). */
static void start_page_animations(void) {
    if (page == MAIN) animate(14, MANIM_ONCE);
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

static void draw_gadget(const menu_t *screen, const menuitem_t *item) {
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
        irect_t dst = {item->rect.x + cell->displacement.x,
                        item->rect.y + cell->displacement.y, cell->rect.w, cell->rect.h};
        R_DrawSprite(sprite, part->cell, -1, &cell->rect, &dst, 0, 16);
    }
}

static void menu_routine(menu_t *screen, menuitem_t *item, menuaction_t action);
static const char *map_row(const menuitem_t *item, int row);
static const char *session_row(const menuitem_t *item, int row);

static bool load_screen(int next) {
    static const char *const scripts[] = {"INTROE", "NEWGAMEE", "STORYE", "SHUMANE", "MULTIE", "LQCE", "NETOPTE", "IPXNAMEE", "DPLAYSE", "GETSVRE"};
    static const char *const lists[] = {"INTRO.DAT", "CHOO.DAT", "LOADG.DAT", "SHUMAN.DAT", "TCPWAIT.DAT", NULL, "NET.DAT", "SERVER.DAT", "LOADG.DAT", "SERVER.DAT"};
    free_screen();
    page = next;
    menu.itemOn = page == STORY ? 5 : page == BRIEFING ? 2 : 0;
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
            item->centered = strstr(line, "align centre") || strstr(line, "align  centre");
            char *label = strstr(line, "label centre ");
            if (label) {
                sscanf(label, "label centre %d %d", &controls[id].message, &controls[id].font);
                item->centered = true;
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
        if (item->centered) item->inset.x = script == TEXT ? 0 : (font->glyph_size.w + 1) / 2;
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
    if (page == SESSION_NAME || page == CONNECT) {
        menu.itemOn = page == SESSION_NAME ? 1 : 3;
        items[menu.itemOn].kind = MI_TEXTFIELD;
        snprintf(items[menu.itemOn].text, sizeof(items[menu.itemOn].text), "%s",
                 page == SESSION_NAME ? session_name : server_address);
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
    menu.itemOn = 0;
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
    menu.itemOn = 57;
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
    if (page == CONNECT) snprintf(items[3].text, sizeof(items[3].text), "%s", network_notice);
}

static bool join_session(const char *address) {
    char copy[128];
    snprintf(copy, sizeof(copy), "%s", address);
    network_notice[0] = '\0';
    if (!I_JoinNetGame("dark-colony", copy)) { network_failure(); return true; }
    waiting = true;
    return load_screen(CONNECT);
}

static void activate(app_t *app, int id) {
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
                skirmish = (dc_skirmish_t){.erupting = 1, .quantity = 4, .flow = 4};
                for (int i = 0; i < 8; ++i)
                    skirmish.players[i] = (dc_skirmish_player_t){.race = i & 1, .color = i, .team = i,
                        .type = i < 2 ? DC_PLAYER_HUMAN : DC_PLAYER_NONE};
                strcpy(skirmish.players[0].name, "Host");
                network_notice[0] = '\0';
                ok = load_screen(SKIRMISH);
            }
        } else if (id == 1) menu.itemOn = 1;
    } else if (page == BROWSE) {
        if (id == 4) { I_CancelNetGame(); ok = load_screen(NETWORK); }
        else if (id == 17) {
            network_notice[0] = '\0';
            if (!I_OpenNetBrowser("dark-colony")) network_failure();
            selected_server[0] = '\0';
            items[0].first_row = 0;
        } else if (id == 18) { I_CancelNetGame(); ok = load_screen(CONNECT); }
        else if (id == 5 && selected_server[0]) ok = join_session(selected_server);
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
            } else if ((id >= 8 && id < 16 && skirmish.players[id - 8].type != DC_PLAYER_NONE) ||
                       (id >= 32 && id < 48 && skirmish.players[id & 7].type != DC_PLAYER_NONE) ||
                       (id >= 150 && id <= 165 && skirmish.players[(id - 150) % 8].type != DC_PLAYER_NONE) ||
                       (id >= 105 && id <= 131)) {
                activate_skirmish(id);
            } else if (id == 133 && selectedmap >= 0) {
                uint8_t setup[LAN_SETUP_SIZE];
                skirmish.seed = (uint8_t)SDL_GetTicks();
                if (I_HostNetGame("dark-colony", session_name, maps[selectedmap].path, active_players()) &&
                    I_SetNetSetup(setup, pack_setup(setup))) waiting = true;
                else network_failure();
            }
            refresh_skirmish();
        } else if (!lan) activate_skirmish(id);
    } else if (page == BRIEFING) {
        if (id == 0) ok = load_screen(training ? SETUP : STORY);
        else if (id == 2) { menumap = mapname; menuactive = false; }
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
static void refresh(void) {
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
}

static void menu_escape(menu_t *screen) {
    app_t *app = screen->owner;
    if (page == QUIT) menuactive = false;
    else if (page == SESSION_NAME) {
        if (!load_screen(NETWORK)) { menuerror = true; app->running = false; }
    } else if (page != MAIN) activate(app, page == SKIRMISH ? 132 : page == NETWORK ? 6 :
        page == CONNECT ? 1 : page == BROWSE || page == SETUP || page == STORY ? 4 : 0);
    else if (inlevel) menuactive = false;
}

static void menu_routine(menu_t *screen, menuitem_t *item, menuaction_t action) {
    app_t *app = screen->owner;
    int id = (int)(item - items);
    if (action == MA_CHANGE && item->kind == MI_TEXTFIELD) {
        if (page == SESSION_NAME && id == 1) snprintf(session_name, sizeof(session_name), "%s", item->text);
        else if (page == CONNECT && id == 3 && !waiting)
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
    if (screen->held != item) {
        if (page == SETUP && id == 5) id = training ? 2 : 3;
        else if (page == SESSION_NAME && id == 1) id = 0;
        else if (page == CONNECT && id == 3) id = 0;
        else if (page == BROWSE && id == 0) id = 5;
    }
    activate(app, id);
}

bool M_Responder(app_t *app, const SDL_Event *event, bool in_level) {
    if (!initialized) return false;
    if (event->type == SDL_QUIT) { app->running = false; return true; }
    if (event->type == SDL_WINDOWEVENT) return false;
    if (!menuactive) {
        if (event->type != SDL_KEYDOWN || event->key.keysym.sym != SDLK_ESCAPE) return false;
        if (!event->key.repeat) M_StartControlPanel(app);
        return true;
    }
    inlevel = in_level;
    menu.owner = app;
    refresh();
    return M_MenuResponder(&menu, app, event);
}

static void step_entrances(void) {
    while (entrance < numentrances) {
        menuentrance_t *e = &entrances[entrance];
        if (e->started + 1 < e->count) {
            const menuitem_t *g = &items[e->gadgets[e->started]];
            /* 0x425257..0x425294: the next gadget starts when the running one
             * reaches its third frame, so the entrances overlap. */
            if (!g->visible || !g->userdata || g->anim.frame - g->anim.first == 2)
                animate(e->gadgets[++e->started], MANIM_ONCE);
        }
        if (e->finished < e->count) {
            menuitem_t *g = &items[e->gadgets[e->finished]];
            /* 0x4252a5..0x4252be: a stopped gadget is hidden and the push
             * button beneath it is redrawn. */
            if (g->anim.mode == MANIM_STOPPED) {
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
            else snprintf(items[3].text, sizeof(items[3].text), "%s", network_notice);
        } else if (status > 0) {
            waiting = false;
            {
                uint8_t setup[LAN_SETUP_SIZE + 24];
                dc_skirmish_t shared;
                size_t size = page == SKIRMISH ? 0 : I_NetSetup(setup, sizeof(setup));
                if (page == SKIRMISH) shared = skirmish; /* Host: lobby as created. */
                else if (size && !unpack_setup(setup, size, &shared)) {
                    snprintf(neterror, sizeof(neterror), "Host sent an invalid game setup");
                    network_failure();
                    return;
                }
                if (page == SKIRMISH || size) DC_RequestSkirmish(mapname, &shared);
            }
            menumap = mapname;
            menuactive = false;
            SDL_StopTextInput();
            return;
        } else if (page == SKIRMISH) {
            snprintf(network_notice, sizeof(network_notice), "Waiting for LAN players: %d/%d\nEscape cancels the session.",
                     I_NetPlayerCount(), doomcom->numplayers);
            refresh_skirmish();
        } else {
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
    if (now - menutime <= 16) return; /* DC.EXE 0x421ebd: menu cadence. */
    menutime = now;
    /* Gadget ticker 0x422828 holds the last one-off pose, as the engine's
     * MANIM_ONCE does; the world animation ticker 0x423dd0 resets to zero. */
    M_MenuTicker(&menu);
    step_entrances();
}

void M_Drawer(const app_t *app) {
    if (!menuactive) return;
    refresh();
    M_MenuDrawer(&menu);
    if (app && app->window)
        SDL_SetWindowTitle(app->window, notice ? notice : "Dark Colony");
}
