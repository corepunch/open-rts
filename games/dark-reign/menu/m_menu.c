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

/* dkreign.exe's shell (C:\WinTactics\Shell\shell.c): screens are DIAL
 * widgets over SHELL.RLD backgrounds, laid out from shell/SHELLCFG.H and
 * labelled from local/MLSTRING.CFG. State machine 0x579de0; see
 * docs/DR_EXE_FINDINGS.md, Native shell. */
bool menuactive;
bool menuerror;
const char *menumap;
drscreen_t drscreen;

static char root[1024], mapname[512];
static bool initialized, inlevel;
static bitmapfont_t fonts[DR_NUMFONTS];
static spritesheet_t background, pictures[8];
static int numpictures;
static const char *notice;

/* Shell states of 0x579de0 that this port draws. */
typedef enum { MAIN = 3, SINGLE = 0xa, CREDITS = 0xe, LOAD = 0xf, CUSTOM = 0x10,
               MISSIONS = 0x13, OPTIONS = 0x14, BRIEFING = 0x1a } shellpage_t;
static shellpage_t page, previous;
static int side; /* 0xda Freedom Guard or 0xdb Imperium, as the logo ids */
static bool training;

/* #define NAME value lines of SHELLCFG.H and MLSTRING.CFG. */
typedef struct { char name[48], value[208]; } define_t;
static define_t *config, *strings;
static int numconfig, numstrings;

static bool read_defines(const char *name, define_t **out, int *count) {
    char path[1024], line[512];
    M_PathJoin(path, sizeof(path), root, name);
    FILE *file = fopen(path, "r");
    if (!file) return false;
    bool ok = true;
    while (fgets(line, sizeof(line), file)) {
        define_t d = {0};
        char *value;
        if (sscanf(line, " #define %47s", d.name) != 1) continue;
        value = strstr(line, d.name) + strlen(d.name);
        while (isspace((unsigned char)*value)) ++value;
        if (*value == '"') {
            char *end = strchr(++value, '"');
            if (end) *end = '\0';
        } else value[strcspn(value, " \t\r\n/")] = '\0';
        snprintf(d.value, sizeof(d.value), "%s", value);
        define_t *grown = realloc(*out, (size_t)(*count + 1) * sizeof(**out));
        if (!grown) { ok = false; break; }
        *out = grown;
        (*out)[(*count)++] = d;
    }
    fclose(file);
    return ok;
}

static const char *lookup(const define_t *table, int count, const char *name) {
    for (int i = 0; i < count; ++i)
        if (!strcmp(table[i].name, name)) return table[i].value;
    return NULL;
}

static int cfg(const char *name) {
    const char *value = lookup(config, numconfig, name);
    return value ? atoi(value) : 0;
}

/* 0x4bfac0 returns the localized string, or the key when it is missing. */
static const char *ss(const char *name) {
    const char *value = lookup(strings, numstrings, name);
    return value ? value : name;
}

const char *DR_String(const char *name) { return ss(name); }

/* ── screen items ───────────────────────────────────────────────────────── */

static menustate_t text_state(const menu_t *screen, const menuitem_t *item) {
    const menuitem_t *button = item->kind == MI_BUTTON ? item : NULL;
    if (!button || !item->enabled) return MS_NORMAL;
    if (screen->held == button) return MS_PUSHED;
    return screen->itemOn == (int)(button - screen->items) ? MS_FOCUS : MS_NORMAL;
}

/* 0x57bf90: the TEXT widget picks its font by state and aligns inside its
 * rectangle; a zero width centres on the x coordinate. */
static void draw_text(const menu_t *screen, const menuitem_t *item) {
    int i = (int)(item - screen->items);
    const bitmapfont_t *font = drscreen.fonts[i][text_state(screen, item)];
    if (!font || !item->text[0]) return;
    int flags = drscreen.flags[i], w = V_TextWidth(font, item->text);
    ivec2_t at = {item->rect.x, item->rect.y};
    if (flags & 0x20) at.x += (item->rect.w - w) / 2;
    else if (flags & 0x10) at.x += item->rect.w - w;
    if (flags & 0x80) at.y += (item->rect.h - font->glyph_size.h) / 2;
    else if (flags & 0x40) at.y += item->rect.h - font->glyph_size.h;
    if (item->prose) {
        V_DrawTextWrapped(item->rect, font, item->prose, NULL, item->first_row * font->line_h);
        return;
    }
    V_DrawText(at, font, item->text, NULL);
}

void DR_ScreenClear(void) {
    memset(drscreen.items, 0, sizeof(drscreen.items));
    memset(drscreen.fonts, 0, sizeof(drscreen.fonts));
    memset(drscreen.flags, 0, sizeof(drscreen.flags));
    for (int i = 0; i < DR_MAXITEMS; ++i) drscreen.ids[i] = -1;
    drscreen.count = 0;
    drscreen.menu.items = drscreen.items;
    drscreen.menu.numitems = 0;
    drscreen.menu.held = NULL;
    drscreen.menu.itemOn = -1;
}

static menuitem_t *add(menuitemkind_t kind, irect_t rect) {
    if (drscreen.count == DR_MAXITEMS) return NULL;
    int i = drscreen.count++;
    menuitem_t *item = &drscreen.items[i];
    *item = (menuitem_t){.kind = kind, .rect = rect, .visible = true, .enabled = true,
                         .link = -1, .value = -1};
    for (int s = 0; s < MS_STATES; ++s) item->look[s] = (menulook_t){.cell = -1, .palette = -1};
    drscreen.menu.numitems = drscreen.count;
    return item;
}

menuitem_t *DR_Text(irect_t rect, int flags, const char *text, const bitmapfont_t *normal,
                    const bitmapfont_t *hover, const bitmapfont_t *pressed) {
    menuitem_t *item = add(MI_STATIC, rect);
    if (!item) return NULL;
    int i = (int)(item - drscreen.items);
    drscreen.flags[i] = flags;
    drscreen.fonts[i][MS_NORMAL] = normal;
    drscreen.fonts[i][MS_FOCUS] = hover ? hover : normal;
    drscreen.fonts[i][MS_PUSHED] = pressed ? pressed : normal;
    snprintf(item->text, sizeof(item->text), "%s", text ? text : "");
    item->ownerdraw = draw_text;
    return item;
}

static void routine(menu_t *screen, menuitem_t *item, menuaction_t action);

/* 0x570620: a BTTN with a click-through TEXT child (flags 0xa3) centred in
 * it; the text follows the button's hover and pressed state. */
menuitem_t *DR_Button(irect_t rect, int id, const char *text, const bitmapfont_t *normal,
                      const bitmapfont_t *hover, const bitmapfont_t *pressed) {
    menuitem_t *item = DR_Text(rect, 0xa0, text, normal, hover, pressed);
    if (!item) return NULL;
    item->kind = MI_BUTTON;
    item->routine = routine;
    drscreen.ids[item - drscreen.items] = id;
    return item;
}

int DR_ItemId(const menuitem_t *item) {
    int i = (int)(item - drscreen.items);
    return i >= 0 && i < drscreen.count ? drscreen.ids[i] : -1;
}

menuitem_t *DR_FindId(int id) {
    for (int i = 0; i < drscreen.count; ++i)
        if (drscreen.ids[i] == id) return &drscreen.items[i];
    return NULL;
}

static menuitem_t *outer_button(int x, int y, int w, int id, const char *label) {
    return DR_Button((irect_t){x, y, w, 30}, id, ss(label), &fonts[10], &fonts[11], &fonts[12]);
}

static void title(const char *label, int font) {
    DR_Text((irect_t){320, 0, 0, 30}, 0x20 | 0x80, ss(label), &fonts[font], NULL, NULL);
}

static const spritesheet_t *picture(const char *name) {
    if (numpictures == (int)(sizeof(pictures) / sizeof(*pictures)) ||
        !DR_ShellImage(name, &pictures[numpictures])) return NULL;
    return &pictures[numpictures++];
}

/* 0x570680: the picture of an inner-shell button shows only while it is
 * hovered or pressed, at its own position; the background already shows
 * the normal state. Its label uses fonts 2/3/4. */
static menuitem_t *image_button(irect_t rect, int id, const char *label, const char *sprite, ivec2_t at) {
    menuitem_t *item = DR_Button(rect, id, label ? ss(label) : "", &fonts[2], &fonts[3], &fonts[4]);
    const spritesheet_t *sheet = sprite ? picture(sprite) : NULL;
    if (!item || (sprite && !sheet)) return NULL;
    if (sheet) {
        pictures[numpictures - 1].cells[0].displacement = (ivec2_t){at.x - rect.x, at.y - rect.y};
        item->sheet = sheet;
        item->look[MS_FOCUS].cell = item->look[MS_PUSHED].cell = 0;
    }
    return item;
}

/* ── lists ──────────────────────────────────────────────────────────────── */

typedef struct { char name[64], path[512]; } fileentry_t;
static fileentry_t *files;
static int numfiles;
static char *prose;

static const char *file_row(const menuitem_t *item, int row) {
    (void)item;
    return row >= 0 && row < numfiles ? files[row].name : "";
}

static int compare_files(const void *a, const void *b) {
    return strcasecmp(((const fileentry_t *)a)->name, ((const fileentry_t *)b)->name);
}

/* Custom missions are the scenario directories under scenario/SINGLE other
 * than the editor's DEFAULT template. */
static bool scan_custom(void) {
    char directory[1024];
    M_PathJoin(directory, sizeof(directory), root, "scenario/SINGLE");
    DIR *dir = opendir(directory);
    if (!dir) return true;
    struct dirent *entry;
    bool ok = true;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.' || !strcasecmp(entry->d_name, "DEFAULT")) continue;
        fileentry_t file = {0};
        char path[1024];
        snprintf(file.name, sizeof(file.name), "%s", entry->d_name);
        snprintf(file.path, sizeof(file.path), "scenario/SINGLE/%s/%s.SCN", entry->d_name, entry->d_name);
        M_PathJoin(path, sizeof(path), root, file.path);
        FILE *scn = fopen(path, "r");
        if (!scn) continue;
        fclose(scn);
        fileentry_t *grown = realloc(files, (size_t)(numfiles + 1) * sizeof(*files));
        if (!grown) { ok = false; break; }
        files = grown;
        files[numfiles++] = file;
    }
    closedir(dir);
    if (numfiles) qsort(files, numfiles, sizeof(*files), compare_files);
    return ok;
}

static menuitem_t *list(irect_t rect, const char *empty) {
    menuitem_t *item = add(MI_LIST, rect);
    if (!item) return NULL;
    item->font = &fonts[8];
    item->row_height = fonts[8].line_h;
    item->color = background.source_palette[0xaa]; /* 0x57bf90 flag 0x100 fill */
    item->prose = empty;
    item->routine = routine;
    return item;
}

/* .BRF sections start at \<digit>; \n breaks a line, \s is a space and \c
 * centres (not reproduced). The briefing shows section 1. */
static bool read_briefing(const char *mission) {
    char path[1024];
    M_PathJoin(path, sizeof(path), root, M_va("scenario/FIXED/%s/%s.BRF", mission, mission));
    blob_t file;
    if (!W_ReadFile(path, &file)) return false;
    free(prose);
    prose = malloc(file.size + 1);
    if (!prose) { W_FreeFile(&file); return false; }
    size_t n = 0;
    int section = -1;
    for (size_t i = 0; i < file.size; ++i) {
        char c = (char)file.bytes[i];
        if (c == '\\' && i + 1 < file.size) {
            char e = (char)file.bytes[++i];
            if (isdigit((unsigned char)e)) section = e - '0';
            else if (section == 1 && e == 'n') prose[n++] = '\n';
            else if (section == 1 && e == 's') prose[n++] = ' ';
        } else if (section == 1) prose[n++] = c == '\r' || c == '\n' ? ' ' : c;
    }
    prose[n] = '\0';
    W_FreeFile(&file);
    return true;
}

/* ── credits ────────────────────────────────────────────────────────────── */

/* 0x578bc0 scrolls USACREDT and AUSCREDT side by side, centred on
 * CREDITS_USA_CENTER and CREDITS_AUS_CENTER, then CREDITS.TXT. ~T lines use
 * CREDITS_TITLE_FONT, ~N CREDITS_NAME_FONT and ~S CREDITS_SUBTEXT_FONT. */
typedef struct { char text[96]; int font, center, y; } creditline_t;
static creditline_t *credits;
static int numcredits, credits_height, credits_scroll, credits_speed;

static int add_credits(const char *name, int center, int y) {
    char path[1024], line[256];
    M_PathJoin(path, sizeof(path), root, M_va("shell/%s", name));
    FILE *file = fopen(path, "r");
    if (!file) return y;
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        int font = cfg("CREDITS_NAME_FONT");
        char *text = line;
        if (text[0] == '~' && text[1]) {
            font = cfg(text[1] == 'T' ? "CREDITS_TITLE_FONT" : text[1] == 'S' ?
                       "CREDITS_SUBTEXT_FONT" : "CREDITS_NAME_FONT");
            text += 2;
        }
        if (font < 0 || font >= DR_NUMFONTS) font = 1;
        creditline_t *grown = realloc(credits, (size_t)(numcredits + 1) * sizeof(*credits));
        if (!grown) break;
        credits = grown;
        creditline_t *c = &credits[numcredits++];
        snprintf(c->text, sizeof(c->text), "%s", text);
        c->font = font;
        c->center = center;
        c->y = y;
        y += fonts[font].line_h ? fonts[font].line_h + 2 : 16;
    }
    fclose(file);
    return y;
}

static void draw_credits(const menu_t *screen, const menuitem_t *item) {
    (void)screen;
    irect_t clip = V_GetClip();
    V_SetClip(item->rect);
    for (int i = 0; i < numcredits; ++i) {
        const creditline_t *c = &credits[i];
        const bitmapfont_t *font = &fonts[c->font];
        int y = item->rect.y + item->rect.h + c->y - credits_scroll;
        if (y < item->rect.y - 20 || y > item->rect.y + item->rect.h) continue;
        V_DrawText((ivec2_t){c->center - V_TextWidth(font, c->text) / 2, y}, font, c->text, NULL);
    }
    V_SetClip(clip);
}

/* ── screens ────────────────────────────────────────────────────────────── */

static void free_screen(void) {
    R_FreeSprite(&background);
    for (int i = 0; i < numpictures; ++i) R_FreeSprite(&pictures[i]);
    numpictures = 0;
    free(files);
    files = NULL;
    numfiles = 0;
    free(prose);
    prose = NULL;
    free(credits);
    credits = NULL;
    numcredits = 0;
    DR_ScreenClear();
}

static const char *const mission_names[] = {"M01F", "M01I"};

static bool load_screen(shellpage_t next) {
    static const struct { shellpage_t page; const char *background; } backgrounds[] = {
        {MAIN, "main"}, {SINGLE, "single"}, {CREDITS, "credits"}, {LOAD, "loadgame"},
        {CUSTOM, "custom"}, {MISSIONS, "missions"}, {OPTIONS, "options"},
    };
    free_screen();
    previous = page;
    page = next;
    notice = NULL;
    const char *name = next == BRIEFING ? (side == 0xdb ? "brief_i" : "brief_f") : NULL;
    for (size_t i = 0; i < sizeof(backgrounds) / sizeof(*backgrounds); ++i)
        if (backgrounds[i].page == next) name = backgrounds[i].background;
    if (!name || !DR_ShellImage(name, &background)) return false;
    drscreen.menu.background = &background;
    drscreen.menu.palette = background.source_palette;
    bool ok = true;
    switch (next) {
    case MAIN:
        title("SS_MAIN_MENU", 13);
        outer_button(cfg("BTN_MAIN_SINGLE_X"), cfg("BTN_MAIN_SINGLE_Y"), 160, SINGLE, "SS_SINGLE_PLAYER");
        outer_button(cfg("BTN_MAIN_MULTI_X"), cfg("BTN_MAIN_MULTI_Y"), 160, 0xb, "SS_MULTI_PLAYER");
        outer_button(cfg("BTN_MAIN_INSTANT_X"), cfg("BTN_MAIN_INSTANT_Y"), 160, 0xc, "SS_INSTANT_ACTION");
        /* The construction kit and the intro movie are outside this port. */
        outer_button(cfg("BTN_MAIN_CONSTRUCTION_X"), cfg("BTN_MAIN_CONSTRUCTION_Y"), 160, 6,
                     "SS_CONSTRUCTION_KIT")->enabled = false;
        outer_button(cfg("BTN_MAIN_REPLAY_X"), cfg("BTN_MAIN_REPLAY_Y"), 160, 2,
                     "SS_REPLAY_INTRO")->enabled = false;
        outer_button(cfg("BTN_MAIN_CREDITS_X"), cfg("BTN_MAIN_CREDITS_Y"), 160, CREDITS, "SS_CREDITS");
        outer_button(cfg("BTN_MAIN_QUIT_X"), cfg("BTN_MAIN_QUIT_Y"), 160, 4, "SS_QUIT");
        break;
    case SINGLE:
        title("SS_SINGLE_PLAYER_OPTIONS", 13);
        outer_button(cfg("BTN_SINGLE_START_X"), cfg("BTN_SINGLE_START_Y"), 160, 0x12, "SS_START_NEW_GAME");
        outer_button(cfg("BTN_SINGLE_LOAD_X"), cfg("BTN_SINGLE_LOAD_Y"), 160, LOAD, "SS_LOAD_SAVED_GAME");
        outer_button(cfg("BTN_SINGLE_CUSTOM_X"), cfg("BTN_SINGLE_CUSTOM_Y"), 160, CUSTOM,
                     "SS_PLAY_CUSTOM_MISSION");
        outer_button(cfg("BTN_SINGLE_PREVIOUS_X"), cfg("BTN_SINGLE_PREVIOUS_Y"), 160, MAIN, "SS_PREVIOUS_MENU");
        break;
    case CUSTOM: {
        int w = cfg("BTN_DEFAULT_WIDTH");
        title("SS_CUSTOM_MISSION_SELECTION", 13);
        DR_Text((irect_t){62, 80, 0, 0}, 0, ss("SS_CUSTOM_MISSIONS"), &fonts[13], NULL, NULL);
        DR_Text((irect_t){62, 230, 0, 0}, 0, ss("SS_SAVED_CUSTOM_MISSIONS"), &fonts[13], NULL, NULL);
        ok = scan_custom();
        menuitem_t *missions = list((irect_t){60, 102, 346, 118}, "");
        if (missions) { missions->row = file_row; M_MenuSetRows(missions, numfiles); }
        list((irect_t){60, 253, 346, 132}, "");
        /* Centred on their x, over the value boxes. */
        DR_Text((irect_t){cfg("BTN_CUSTOM_SIDE_X"), cfg("BTN_CUSTOM_SIDE_Y"), 0, 0}, 0x20,
                ss("SS_SIDE"), &fonts[13], NULL, NULL);
        DR_Text((irect_t){cfg("BTN_CUSTOM_SIZE_X"), cfg("BTN_CUSTOM_SIZE_Y"), 0, 0}, 0x20,
                ss("SS_MAP_SIZE"), &fonts[13], NULL, NULL);
        DR_Text((irect_t){cfg("BTN_CUSTOM_ENEMIES_X"), cfg("BTN_CUSTOM_ENEMIES_Y"), 0, 0}, 0x20,
                ss("SS_NUMBER_OF_ENEMIES"), &fonts[13], NULL, NULL);
        outer_button(cfg("BTN_CUSTOM_PREVIOUS_X"), cfg("BTN_CUSTOM_PREVIOUS_Y"), w, SINGLE, "SS_PREVIOUS_MENU");
        outer_button(cfg("BTN_CUSTOM_LOAD_X"), cfg("BTN_CUSTOM_LOAD_Y"), w, 5, "SS_LOAD_MISSION");
        outer_button(cfg("BTN_CUSTOM_DELETE_X"), cfg("BTN_CUSTOM_DELETE_Y"), w, 0xdf, "SS_DELETE")->enabled = false;
        break;
    }
    case LOAD: {
        int w = cfg("BTN_DEFAULT_WIDTH");
        title("SS_LOAD_SAVED_GAME", 13);
        DR_Text((irect_t){62, 80, 0, 0}, 0, ss("SS_SAVED_FIXED_MISSIONS"), &fonts[13], NULL, NULL);
        /* This port has no Dark Reign saved games yet. */
        list((irect_t){cfg("BOX_LOAD_SAVED_GAMES_LEFT"), cfg("BOX_LOAD_SAVED_GAMES_TOP"),
                       cfg("BOX_LOAD_SAVED_GAMES_WIDTH"), cfg("BOX_LOAD_SAVED_GAMES_HEIGHT")}, "");
        DR_Text((irect_t){cfg("TEXT_LOAD_LOCATION_X"), cfg("TEXT_LOAD_LOCATION_Y"), 0, 0}, 0,
                ss("SS_SAVE_LOCATION"), &fonts[13], NULL, NULL);
        DR_Text((irect_t){cfg("TEXT_LOAD_PROGRESSION_X"), cfg("TEXT_LOAD_PROGRESSION_Y"), 0, 0}, 0,
                ss("SS_MISSION_PROGRESSION"), &fonts[13], NULL, NULL);
        outer_button(cfg("BTN_LOAD_BACK_X"), cfg("BTN_LOAD_BACK_Y"), w, SINGLE, "SS_PREVIOUS_MENU");
        outer_button(cfg("BTN_LOAD_LAUNCH_X"), cfg("BTN_LOAD_LAUNCH_Y"), w, 5, "SS_LOAD_MISSION")->enabled = false;
        outer_button(cfg("BTN_CUSTOM_DELETE_X"), cfg("BTN_CUSTOM_DELETE_Y"), w, 0xe0, "SS_DELETE")->enabled = false;
        break;
    }
    case CREDITS: {
        title("SS_CREDITS", 13);
        int y = add_credits("USACREDT.TXT", cfg("CREDITS_USA_CENTER"), 0);
        int y2 = add_credits("AUSCREDT.TXT", cfg("CREDITS_AUS_CENTER"), 0);
        credits_height = add_credits("CREDITS.TXT", 320, (y > y2 ? y : y2) + 40);
        credits_scroll = 0;
        credits_speed = 1;
        menuitem_t *scroll = add(MI_STATIC, (irect_t){0, 40, 640, 320});
        if (scroll) scroll->ownerdraw = draw_credits;
        outer_button(cfg("BTN_CREDITS_PREVIOUS_X"), cfg("BTN_CREDITS_PREVIOUS_Y"), 160, MAIN, "SS_PREVIOUS_MENU");
        int fw = cfg("BTN_CREDITS_FASTER_W"), fh = cfg("BTN_CREDITS_FASTER_H");
        DR_Button((irect_t){cfg("BTN_CREDITS_FASTER_X"), cfg("BTN_CREDITS_FASTER_Y"), fw, fh}, 0x68, "",
                  NULL, NULL, NULL);
        DR_Button((irect_t){cfg("BTN_CREDITS_SLOWER_X"), cfg("BTN_CREDITS_SLOWER_Y"), fw, fh}, 0x69, "",
                  NULL, NULL, NULL);
        break;
    }
    case MISSIONS: {
        /* 0x5743a0: training buttons, the two side logos (0xda/0xdb lead to
         * the briefing) and the sidebar (0x66 options, 0x67 archive). The
         * twelve mission nodes and the archive are not reproduced. */
        irect_t basic = {cfg("BTN_MISSION_BASIC_X"), cfg("BTN_MISSION_BASIC_Y"),
                         cfg("BTN_MISSION_BASIC_W"), cfg("BTN_MISSION_BASIC_H")};
        irect_t advanced = {cfg("BTN_MISSION_ADVANCED_X"), cfg("BTN_MISSION_ADVANCED_Y"),
                            cfg("BTN_MISSION_ADVANCED_W"), cfg("BTN_MISSION_ADVANCED_H")};
        image_button(basic, 0x17, "SS_BASIC_TRAINING", "m_basic", (ivec2_t){basic.x, basic.y});
        image_button(advanced, 0x18, "SS_ADVANCED_TRAINING", "m_adv", (ivec2_t){advanced.x, advanced.y});
        image_button((irect_t){235, 105, 45, 40}, 0xda, NULL, "m_flogo2", (ivec2_t){235, 105});
        image_button((irect_t){365, 105, 45, 40}, 0xdb, NULL, "m_ilogo2", (ivec2_t){365, 105});
        image_button((irect_t){0, 200, 80, 80}, 0x66, NULL, "cube_lft", (ivec2_t){0, 200});
        DR_Text((irect_t){320, 68, 0, 30}, 0x20, "", &fonts[6], NULL, NULL);
        break;
    }
    case BRIEFING: {
        bool imperium = side == 0xdb;
        if (!read_briefing(training ? (side == 0x17 ? "T1" : "T2") : mission_names[imperium])) ok = false;
        irect_t text = imperium ? (irect_t){350, 150, 255, 295} : (irect_t){25, 20, 275, 340};
        menuitem_t *box = DR_Text(text, 0, "", &fonts[8], NULL, NULL);
        if (box) { box->text[0] = ' '; box->prose = prose; }
        if (imperium) {
            image_button((irect_t){145, 348, 105, 75}, 5, "SS_LAUNCH", "bi_lnch", (ivec2_t){138, 350});
            DR_Button((irect_t){10, 400, 80, 65}, 9, ss("SS_BACK"), &fonts[2], &fonts[3], &fonts[4]);
        } else {
            image_button((irect_t){250, 405, 160, 60}, 5, "SS_LAUNCH", "bf_lnch", (ivec2_t){250, 410});
            DR_Button((irect_t){15, 405, 135, 60}, 9, ss("SS_BACK"), &fonts[2], &fonts[3], &fonts[4]);
        }
        break;
    }
    case OPTIONS: {
        /* 0x574f90. This port has no Dark Reign saved games: Load, Save and
         * Delete stay disabled. */
        DR_Text((irect_t){320, 20, 0, 30}, 0x20, ss("SS_OPTIONS"), &fonts[5], NULL, NULL);
        DR_Text((irect_t){320, 68, 0, 30}, 0x20, inlevel ? M_FileName(level.map_path) : "", &fonts[6], NULL, NULL);
        DR_Text((irect_t){78, 116, 0, 0}, 0, ss("SS_AVAILABLE_GAMES"), &fonts[6], NULL, NULL);
        list((irect_t){75, 142, 294, 200}, "");
        DR_Text((irect_t){cfg("TEXT_OPTIONS_STATS_X"), 116, 0, 0}, 0, ss("SS_SAVE_LOCATION"), &fonts[6], NULL, NULL);
        DR_Text((irect_t){cfg("TEXT_OPTIONS_STATS_X"), cfg("TEXT_OPTIONS_PROG_Y"), 0, 0}, 0,
                ss("SS_MISSION_PROGRESSION"), &fonts[6], NULL, NULL);
        image_button((irect_t){67, 342, 150, 32}, 0xe1, "SS_LOAD", "o_btn1", (ivec2_t){67, 342})->enabled = false;
        image_button((irect_t){218, 342, 150, 32}, 0xe2, "SS_SAVE", "o_btn2", (ivec2_t){218, 342})->enabled = false;
        image_button((irect_t){369, 342, 150, 32}, 0xe3, "SS_DELETE", "o_btn3", (ivec2_t){369, 342})->enabled = false;
        image_button((irect_t){60, 382, 238, 66}, 0xe4, "SS_QUIT_TO_MAIN_MENU", "o_quittm", (ivec2_t){48, 382});
        image_button((irect_t){298, 409, 238, 66}, 0xe5, "SS_QUIT_TO_WIN95", "o_quitt95", (ivec2_t){286, 409});
        image_button((irect_t){560, 200, 80, 80}, 0x67, "SS_BACK", "cube_rgt", (ivec2_t){560, 200});
        break;
    }
    }
    for (int i = 0; i < drscreen.count; ++i)
        if (drscreen.items[i].kind == MI_BUTTON && !drscreen.items[i].routine) ok = false;
    return ok;
}

/* ── actions ────────────────────────────────────────────────────────────── */

static void start_level(const char *path) {
    snprintf(mapname, sizeof(mapname), "%s", path);
    menumap = mapname;
    menuactive = false;
    SDL_StopTextInput();
}

static void fail(app_t *app) {
    fprintf(stderr, "Could not load Dark Reign shell screen 0x%x\n", page);
    menuerror = true;
    app->running = false;
}

static void go(app_t *app, shellpage_t next) {
    if (!load_screen(next)) fail(app);
}

void DR_ShellReturn(app_t *app) {
    DR_MultiClose();
    go(app, MAIN);
}

static void activate(app_t *app, int id) {
    switch (page) {
    case MAIN:
        if (id == 4) app->running = false;
        else if (id == 0xb || id == 0xc) {
            if (!DR_MultiOpen(app, root, id == 0xc)) fail(app);
        } else if (id == SINGLE || id == CREDITS) go(app, id);
        break;
    case SINGLE:
        if (id == 0x12) { training = false; go(app, MISSIONS); }
        else if (id == LOAD || id == CUSTOM || id == MAIN) go(app, id);
        break;
    case CUSTOM: {
        const menuitem_t *missions = &drscreen.items[3];
        if (id == SINGLE) go(app, SINGLE);
        else if (id == 5 && missions->value >= 0 && missions->value < numfiles)
            start_level(files[missions->value].path);
        break;
    }
    case LOAD:
        if (id == SINGLE) go(app, SINGLE);
        break;
    case CREDITS:
        if (id == MAIN) go(app, MAIN);
        else if (id == 0x68 && credits_speed < cfg("CREDITS_MAX_SCROLL")) ++credits_speed;
        else if (id == 0x69 && credits_speed > 1) --credits_speed;
        break;
    case MISSIONS:
        if (id == 0xda || id == 0xdb) { side = id; training = false; go(app, BRIEFING); }
        else if (id == 0x17 || id == 0x18) { side = id; training = true; go(app, BRIEFING); }
        else if (id == 0x66) go(app, OPTIONS);
        break;
    case BRIEFING:
        if (id == 9) go(app, MISSIONS);
        else if (id == 5) {
            const char *mission = training ? (side == 0x17 ? "T1" : "T2") : mission_names[side == 0xdb];
            start_level(M_va("scenario/FIXED/%s/%s.SCN", mission, mission));
        }
        break;
    case OPTIONS:
        if (id == 0xe5) app->running = false;
        else if (id == 0xe4) {
            /* 0x1c: back to the main menu; a running level is released first. */
            if (inlevel) { menuleave = true; menuactive = false; }
            else go(app, MAIN);
        } else if (id == 0x67) {
            if (inlevel) menuactive = false;
            else go(app, previous == OPTIONS ? MISSIONS : previous);
        }
        break;
    }
}

static void routine(menu_t *screen, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    activate(screen->owner, DR_ItemId(item));
}

/* 0x570060: Escape returns to the main menu from the outer shell; in a
 * level it closes the options screen. */
static void escape(menu_t *screen) {
    app_t *app = screen->owner;
    if (inlevel) menuactive = false;
    else if (page != MAIN) go(app, MAIN);
}

/* ── lifecycle ──────────────────────────────────────────────────────────── */

static void free_fonts(void) {
    for (int i = 0; i < DR_NUMFONTS; ++i) HU_FreeFont(&fonts[i]);
}

void M_Shutdown(void) {
    M_StopMessage();
    DR_MultiClose();
    free_screen();
    free_fonts();
    DR_ShellClose();
    free(config);
    free(strings);
    config = strings = NULL;
    numconfig = numstrings = 0;
    initialized = menuactive = false;
    menumap = NULL;
    SDL_StopTextInput();
}

bool M_Init(app_t *app, const char *data_root) {
    (void)app;
    menuerror = false;
    M_StopMessage();
    if (strlen(data_root) >= sizeof(root)) return false;
    strcpy(root, data_root);
    bool ok = read_defines("shell/SHELLCFG.H", &config, &numconfig) &&
              read_defines("local/MLSTRING.CFG", &strings, &numstrings) && DR_ShellOpen(root);
    /* 0x579685: font slots come from FONT_n_NAME. */
    for (int i = 0; ok && i < DR_NUMFONTS; ++i) {
        const char *name = lookup(config, numconfig, M_va("FONT_%d_NAME", i));
        ok = name && DR_ShellFont(name, &fonts[i]);
    }
    drscreen.menu.modal = true;
    drscreen.menu.escape = escape;
    page = MAIN;
    initialized = ok && load_screen(MAIN);
    if (!initialized) M_Shutdown();
    return initialized;
}

void M_StartControlPanel(app_t *app) {
    if (!initialized || menuactive) return;
    inlevel = level.width > 0;
    drscreen.menu.owner = app;
    if (!load_screen(inlevel ? OPTIONS : MAIN)) { fail(app); return; }
    menuactive = true;
    app->dragging_select = false;
    app->selection_rect = (irect_t){0};
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
    drscreen.menu.owner = app;
    if (DR_MultiActive()) return DR_MultiResponder(app, event);
    return M_MenuResponder(&drscreen.menu, app, event);
}

void M_Ticker(void) {
    if (!menuactive) return;
    if (DR_MultiActive()) { DR_MultiTicker(); return; }
    if (page == CREDITS) {
        credits_scroll += credits_speed;
        if (credits_scroll > credits_height + 320) credits_scroll = 0;
    }
    M_MenuTicker(&drscreen.menu);
}

void M_Drawer(const app_t *app) {
    (void)app;
    if (!menuactive) return;
    if (DR_MultiActive()) { DR_MultiDrawer(); return; }
    M_MenuDrawer(&drscreen.menu);
    if (notice) V_DrawText((ivec2_t){20, 450}, &fonts[6], notice, NULL);
}

/* The HUD's MENU page opens the same options screen as Escape. */
void DR_OpenOptions(app_t *app) {
    M_StartControlPanel(app);
}
