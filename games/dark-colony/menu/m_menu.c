#define _DEFAULT_SOURCE
#include "m_menu.h"
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
static int race, itemOn, scroll;
static int bright_pushed, bright_highlight;
static int pressed = -1;
static bool initialized, training;
static uint64_t menutime;
static const char *notice;
static char mission_title[128], mission_region[128];
static char *prose;
static enum { MAIN, SETUP, STORY, BRIEFING, SKIRMISH, QUIT } page;
static isize2_t screen;
static bitmapfont_t fonts[2];
static spritesheet_t background;
static spritesheet_t pictures;
static SDL_Renderer *menu_renderer;
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
    int message, font, remap, gadget, mode, frame, delay;
    int maxchars;
    int normal, pushed;
    bool centered, visible, checked, writable;
    const dc_fin_t *fin;
    const dc_fin_label_t *sequence;
} menucontrol_t;
static menucontrol_t controls[300];
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
    for (int i = 0; i < 2; ++i) HU_FreeFont(&fonts[i]);
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
    for (int i = 0; i < 300; ++i) controls[i].gadget = -1;
    memset(messages, 0, sizeof(messages));
    screen = (isize2_t){0};
    bright_pushed = bright_highlight = 0;
    pressed = -1;
}

void M_Shutdown(void) {
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

static bool load_screen(int next) {
    static const char *const scripts[] = {"INTROE", "NEWGAMEE", "STORYE", "SHUMANE", "MULTIE", "LQCE"};
    static const char *const lists[] = {"INTRO.DAT", "CHOO.DAT", "LOADG.DAT", "SHUMAN.DAT", "TCPWAIT.DAT"};
    free_screen();
    page = next;
    itemOn = page == STORY ? 5 : page == BRIEFING ? 2 : 0;
    scroll = 0;
    notice = NULL;
    char path[1024], line[512], name[128], palette_path[1024] = "";
    if (page < QUIT && !load_animations(M_va("INTRFACE/%s", lists[page]))) return false;
    if (page == QUIT) {
        spritesheet_t palette = {0};
        M_PathJoin(path, sizeof(path), root, "PALETTE.GIF");
        if (!W_LoadGIFTexture(menu_renderer, path, &palette)) return false;
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
            if (!W_LoadGIFTexture(menu_renderer, path, &background)) ok = false;
            M_PathJoin(palette_path, sizeof(palette_path), root, M_va("%s.RMP", M_Upper(name)));
        } else if (sscanf(line, "pictures %127s", name) == 1) {
            M_PathJoin(path, sizeof(path), root, M_va("%s.SPR", M_Upper(name)));
            if (!DC_LoadSpriteImage(path, &pictures)) ok = false;
        } else if (sscanf(line, "font %d %127s", &id, name) == 2) {
            if (id < 0 || id >= 2 || !DC_LoadFont(root,
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
            *control = (menucontrol_t){ .rect = rect, .gadget = -1, .visible = true, .remap = 7,
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
            }
            if (control->message < 0 || control->message >= 300 || control->font < 0 || control->font >= 2) { ok = false; break; }
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
            int values[300], count = 0;
            char *cursor = line + 5, *end;
            while (count < 300) {
                long value = strtol(cursor, &end, 10);
                if (end == cursor) break;
                if (value < 0 || value >= 300) { ok = false; break; }
                values[count++] = (int)value;
                cursor = end;
            }
            if (count < 4 || values[2] <= 0 || values[2] > values[3] || count != 4 + values[2] + values[3]) { ok = false; break; }
            for (int j = 0; j < values[2]; ++j) controls[values[4 + j]].visible = j == 0;
            for (int i = 0; i < values[3]; ++i) {
                menucontrol_t *button = &controls[values[4 + values[2] + i]];
                for (int j = 0; j < values[2]; ++j) {
                    int gadget = values[4 + j];
                    if (button->rect.x == controls[gadget].rect.x && button->rect.y == controls[gadget].rect.y)
                        button->gadget = gadget;
                }
            }
        }
    }
    if (ferror(file)) ok = false;
    fclose(file);
    blob_t rmp;
    if (!W_ReadFile(palette_path, &rmp)) return false;
    if (rmp.size < 0x10000) { W_FreeFile(&rmp); return false; }
    ok = screen_palette(&pictures, &rmp) && ok;
    for (int i = 0; i < 2; ++i) ok = screen_palette(&fonts[i].sprite, &rmp) && ok;
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
    if (page == MAIN) animate(14, 1); /* 0x404b8c..0x404b9c. */
    if (page == SETUP) {
        controls[training ? 3 : 2].visible = false;
        controls[19].visible = controls[20].visible = false;
        for (int i = 21; i <= 26; ++i) controls[i].visible = false;
        controls[race ? 26 : 23].visible = true;
        animate(race ? 26 : 23, 0);
        for (int i = 13; i <= 16; ++i) animate(i, 0);
        for (int i = 29; i <= 35; ++i) animate(i, 0);
        snprintf(controls[5].text, sizeof(controls[5].text), "%s", leader);
        SDL_StartTextInput();
    } else SDL_StopTextInput();
    if (page == STORY && !read_text(race ? "INTRFACE/ASTORY.TXT" : "INTRFACE/HSTORY.TXT")) ok = false;
    if (page == BRIEFING) {
        controls[10].visible = false;
        controls[race ? 36 : 35].visible = false;
        for (int i = 15; i <= 24; ++i) animate(i, 0);
        animate(39, 0);
        animate(race ? 35 : 36, 0);
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
    menutime = SDL_GetTicks64();
    return ok && screen.w > 0 && screen.h > 0 &&
        (page == QUIT || background.numlumps) && fonts[0].sprite.numlumps;
}

bool M_Init(app_t *app, const char *data_root) {
    menu_renderer = app->renderer;
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
        } else if (id == 4 && !netgame) {
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
    } else if (page == SKIRMISH) {
        if (id == 132) ok = load_screen(MAIN);
        else activate_skirmish(id);
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
    if (page == SKIRMISH) {
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

bool M_Responder(app_t *app, const SDL_Event *event, bool inlevel) {
    if (!initialized) return false;
    if (event->type == SDL_QUIT) { app->running = false; return true; }
    if (event->type == SDL_WINDOWEVENT) return false;
    if (!menuactive) {
        if (event->type != SDL_KEYDOWN || event->key.keysym.sym != SDLK_ESCAPE) return false;
        if (!event->key.repeat) M_StartControlPanel(app);
        return true;
    }
    int olditem = itemOn;
    if (event->type == SDL_KEYDOWN) {
        switch (event->key.keysym.sym) {
        case SDLK_ESCAPE:
            if (event->key.repeat) break;
            if (page == QUIT) menuactive = false;
            else if (page != MAIN) activate(app, page == SKIRMISH ? 132 : page == SETUP || page == STORY ? 4 : 0, inlevel);
            else if (inlevel) menuactive = false;
            break;
        case SDLK_UP: case SDLK_DOWN:
            if (page == SKIRMISH && itemOn == 27) {
                int row = 0;
                if (selectedmap >= 0) {
                    while (filtered_map(row) >= 0 && filtered_map(row) != selectedmap) ++row;
                    row += event->key.keysym.sym == SDLK_UP ? -1 : 1;
                }
                if (row < 0) row = 0;
                if (filtered_map(row) >= 0) {
                    selectedmap = filtered_map(row);
                    if (row < mapscroll) mapscroll = row;
                    if (row >= mapscroll + visible_map_rows()) mapscroll = row - visible_map_rows() + 1;
                    refresh_skirmish();
                }
                break;
            }
            /* fall through */
        case SDLK_TAB:
            do { itemOn = (itemOn + (event->key.keysym.sym == SDLK_UP ? 299 : 1)) % 300; } while (!selectable(itemOn));
            break;
        case SDLK_RETURN: case SDLK_KP_ENTER:
            if (!event->key.repeat) activate(app, page == SETUP && itemOn == 5 ? (training ? 2 : 3) : itemOn, inlevel);
            break;
        case SDLK_BACKSPACE:
            if (page == SKIRMISH && itemOn == 0 && skirmish.players[0].name[0]) {
                char *name = skirmish.players[0].name;
                name[strlen(name) - 1] = '\0';
                refresh_skirmish();
            }
            if (page == SETUP && itemOn == 5 && leader[0]) {
                leader[strlen(leader) - 1] = '\0';
                strcpy(controls[5].text, leader);
            }
            break;
        default: break;
        }
    } else if (event->type == SDL_TEXTINPUT && page == SKIRMISH && itemOn == 0) {
        char *name = skirmish.players[0].name;
        size_t length = strlen(name);
        for (const unsigned char *p = (const unsigned char *)event->text.text; *p; ++p)
            if (*p >= 32 && *p < 127 && length < (size_t)controls[0].maxchars)
                name[length++] = (char)*p;
        name[length] = '\0';
        refresh_skirmish();
    } else if (event->type == SDL_TEXTINPUT && page == SETUP && itemOn == 5) {
        size_t length = strlen(leader);
        for (const unsigned char *p = (const unsigned char *)event->text.text; *p; ++p) {
            if (*p < 32 || *p >= 127 || length >= sizeof(leader) - 1 ||
                length >= (size_t)controls[5].maxchars) continue;
            leader[length++] = (char)*p;
        }
        leader[length] = '\0';
        strcpy(controls[5].text, leader);
        notice = NULL;
    } else if (event->type == SDL_MOUSEMOTION || (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)) {
        ivec2_t point;
        int x = event->type == SDL_MOUSEMOTION ? event->motion.x : event->button.x;
        int y = event->type == SDL_MOUSEMOTION ? event->motion.y : event->button.y;
        R_WindowToRenderPt(app, x, y, &point.x, &point.y);
        point = (ivec2_t){ point.x * screen.w / app->win.w, point.y * screen.h / app->win.h };
        if (page == SKIRMISH && pressed == 30 && event->type == SDL_MOUSEMOTION) {
            drag_map_scroll(point);
            return true;
        }
        for (int i = 0; i < 300; ++i) {
            irect_t rect = controls[i].rect;
            if (!selectable(i) || !irect_contains(rect, point)) continue;
            itemOn = i;
            if (event->type == SDL_MOUSEBUTTONDOWN) {
                pressed = i;
                if (page == SKIRMISH && i == 27) {
                    selectedmap = filtered_map(mapscroll + (point.y - rect.y) / fonts[0].glyph_size.h);
                    refresh_skirmish();
                } else if (page == SKIRMISH && i == 30) {
                    drag_map_scroll(point);
                } else activate(app, i, inlevel);
            }
            break;
        }
    } else if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        pressed = -1;
    } else if (event->type == SDL_MOUSEWHEEL && page == SKIRMISH) {
        scroll_maps(-event->wheel.y);
    } else if (event->type == SDL_MOUSEWHEEL && (page == STORY || page == BRIEFING)) {
        scroll -= event->wheel.y;
        if (scroll < 0) scroll = 0;
    }
    if (itemOn != olditem) {
        for (int i = 0; i < 300; ++i)
            if (controls[i].gadget >= 0) controls[controls[i].gadget].visible = i == itemOn;
        if (controls[itemOn].gadget >= 0) animate(controls[itemOn].gadget, 1);
    }
    return true;
}

void M_Ticker(void) {
    if (!menuactive) return;
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
}

static void draw_gadget(SDL_Renderer *renderer, const menucontrol_t *c) {
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
        R_DrawSprite(renderer, sprite, part->cell, -1, &cell->rect, &dst,
                      SDL_FLIP_NONE,
                      (SDL_Color){255, 255, 255, 255}, SDL_BLENDMODE_BLEND);
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

static void draw_picture(SDL_Renderer *renderer, const menucontrol_t *c) {
    bool pushed = c->checked || (pressed >= 0 && c == &controls[pressed]);
    int frame = pushed ? c->pushed : c->normal;
    if (frame < 0) frame = pushed ? c->normal : c->pushed;
    if (frame < 0 || frame >= pictures.numlumps) return;
    const spritecell_t *cell = &pictures.cells[frame];
    irect_t dst = {c->rect.x + cell->displacement.x, c->rect.y + cell->displacement.y,
                   cell->rect.w, cell->rect.h};
    R_DrawSprite(renderer, &pictures, frame, control_intensity(c) * 8 + c->remap, &cell->rect, &dst, SDL_FLIP_NONE,
                 (SDL_Color){255,255,255,255}, SDL_BLENDMODE_BLEND);
}

static void draw_map_list(SDL_Renderer *renderer) {
    irect_t clip = controls[27].rect;
    SDL_RenderSetClipRect(renderer, &clip);
    for (int row = 0; row < visible_map_rows(); ++row) {
        int index = filtered_map(mapscroll + row);
        if (index < 0) break;
        int y = clip.y + row * fonts[0].glyph_size.h;
        if (index == selectedmap) {
            SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255); /* MULTIE selbg cyan. */
            SDL_RenderFillRect(renderer, &(irect_t){clip.x, y, clip.w, fonts[0].glyph_size.h});
        }
        HU_DrawTextRemapped(renderer, &fonts[0], clip.x, y, maps[index].label,
                           (SDL_Color){255,255,255,255}, 1, (index == selectedmap ? 0 : 16 * 8) + 7);
    }
    SDL_RenderSetClipRect(renderer, NULL);
    irect_t bar = controls[30].rect;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, &bar);
    /* 0x427f74..0x427fa1: scroll start/end scaled by total list length. */
    int count = 0;
    while (filtered_map(count) >= 0) ++count;
    int rows = visible_map_rows();
    if (count) {
        int end = mapscroll + rows < count ? mapscroll + rows : count;
        irect_t thumb = {bar.x, bar.y + bar.h * mapscroll / count,
                         bar.w, bar.h * end / count - bar.h * mapscroll / count};
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); /* Native default colour 1. */
        SDL_RenderDrawRect(renderer, &bar);
        SDL_RenderFillRect(renderer, &thumb);
    }
}

void M_Drawer(const app_t *app) {
    if (!menuactive) return;
    float sx, sy;
    SDL_Rect oldclip;
    bool clipped = SDL_RenderIsClipEnabled(app->renderer);
    SDL_RenderGetClipRect(app->renderer, &oldclip);
    SDL_RenderSetClipRect(app->renderer, NULL);
    SDL_RenderGetScale(app->renderer, &sx, &sy);
    SDL_RenderSetScale(app->renderer, (float)app->win.w / screen.w, (float)app->win.h / screen.h);
    irect_t dst = {0, 0, screen.w, screen.h};
    if (background.numlumps) R_DrawSprite(app->renderer, &background, 0, -1, NULL, &dst,
                  SDL_FLIP_NONE, (SDL_Color){255, 255, 255, 255}, SDL_BLENDMODE_NONE);
    for (int i = 0; i < 300; ++i) {
        const menucontrol_t *c = &controls[i];
        if (c->visible && c->sequence) draw_gadget(app->renderer, c);
        if (c->visible && (c->kind == PICTURE || c->kind == PUSH || c->kind == CHECK))
            draw_picture(app->renderer, c);
    }
    if (page == SKIRMISH) draw_map_list(app->renderer);
    for (int i = 0; i < 300; ++i) {
        const menucontrol_t *c = &controls[i];
        if (!c->visible || !c->text[0]) continue;
        const bitmapfont_t *font = &fonts[c->font];
        ivec2_t at = {c->rect.x, c->rect.y};
        if (c->centered) at = ivec2_add(at, (ivec2_t){(c->rect.w - HU_TextWidth(font, c->text, 1)) / 2, (c->rect.h - font->glyph_size.h) / 2});
        else if (c->kind == LABEL) at = ivec2_add(at, (ivec2_t){font->glyph_size.w, (c->rect.h - font->glyph_size.h) / 2});
        if (c->centered && c->kind != TEXT)
            at = ivec2_add(at, (ivec2_t){(font->glyph_size.w + 1) / 2, 0});
        int intensity = c->kind == PUSH || c->kind == CHECK ? control_intensity(c) : 16;
        HU_DrawTextRemapped(app->renderer, font, at.x, at.y, c->text,
                            (SDL_Color){255, 255, 255, 255}, 1, intensity * 8 + c->remap);
    }
    if (prose) {
        /* Text viewport arguments at 0x4023f8/0x403030, separate from widgets. */
        irect_t clip = page == STORY ? (irect_t){10, 13, 579, 420} : (irect_t){310, 212, 294, 225};
        SDL_RenderSetClipRect(app->renderer, &clip);
        HU_DrawTextWrapped(app->renderer, &fonts[0], clip.x, clip.y - scroll * fonts[0].line_h,
                           clip.w, prose, (SDL_Color){255, 255, 255, 255}, 1);
        SDL_RenderSetClipRect(app->renderer, NULL);
    }
    SDL_SetWindowTitle(app->window, notice ? notice : "Dark Colony");
    SDL_RenderSetScale(app->renderer, sx, sy);
    SDL_RenderSetClipRect(app->renderer, clipped ? &oldclip : NULL);
}
