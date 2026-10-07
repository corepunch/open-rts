#define _DEFAULT_SOURCE
#include "engine.h"
#include "warcraft-2.h"
#include "w2_local.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* Native dialog geometry comes from REZDAT linked records; assets come from
 * its GFUs and bitmaps. Callback dispatch and dynamic contents belong here.
 *
 * Single-player campaign entry follows the executable: New Campaign, race,
 * the mission briefing, then the map. In-game popups are the REZDAT dialogs
 * named by the game menu's result switch. Screens are built when opened.
 * A popup over a level leaves the background empty so the map shows; the
 * front end uses the title, or the dimmed title behind a full-screen page. */

static void menu_note(menu_t *menu, menuitem_t *item, menuaction_t action);
static void return_to_game(menu_t *menu, menuitem_t *item, menuaction_t action);
static void end_scenario(menu_t *menu, menuitem_t *item, menuaction_t action);
static void single_player(menu_t *menu, menuitem_t *item, menuaction_t action);
static void single_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void result_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void show_credits(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_options(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_help_menu(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_objectives(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_end_menu(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_save(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_load(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_multiplayer(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_briefing(void);
static void open_connection(void);
static void open_viewgame(void);
static void open_engine_net(int first);

static w2_menu_art_t art;

static void menu_note(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (action == MA_ACTIVATE && item->userdata) M_StartMessage(item->userdata);
}

static void return_to_game(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action == MA_ACTIVATE && level.width) M_ClearMenus();
    (void)menu;
}

static void end_scenario(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu; (void)item;
    if (action != MA_ACTIVATE) return;
    menuleave = true;
    M_ClearMenus();
}

static void game_escape(menu_t *menu) {
    (void)menu;
    if (level.width) M_ClearMenus();
}

static int side(void) {
    const w2_pud_t *pud = level.native_data;
    if (!pud || consoleplayer < 0 || consoleplayer >= 16) return 0;
    return pud->sides[consoleplayer] == 1 ? 1 : 0;
}

/* A button's cells are runs of three equal-size frames in the REZDAT widget
 * sheet: disabled, normal, pressed. Wargus widgets.lua picks the normal and
 * pressed frame of the last run of that size (gm-half 10/11, gm-full 16/17). */
static bool widget_cells(const spritesheet_t *sheet, isize2_t size, int *normal, int *pressed) {
    if (!sheet) return false;
    int found = -1;
    for (int i = 0; i + 2 < sheet->numlumps; ++i) {
        bool run = true;
        for (int k = 0; k < 3; ++k)
            run = run && sheet->cells[i + k].rect.w == size.w && sheet->cells[i + k].rect.h == size.h;
        if (run) { found = i; i += 2; }
    }
    if (found < 0) return false;
    *normal = found + 1;
    *pressed = found + 2;
    return true;
}

static void bind_widget(menuitem_t *item, const spritesheet_t *sheet, int normal, int pressed) {
    item->sheet = sheet && sheet->numlumps > pressed ? sheet : NULL;
    item->opaque = true;
    item->disabled_look = true;
    item->release = true;
    item->font = art.font.sprite.numlumps ? &art.font : NULL;
    /* Yellow text with a white hotkey letter; all white under the pointer,
     * and pressed text sits one pixel right and down. */
    item->ink = 0;
    item->hotkey_ink = 0xffffffffu;
    item->look[MS_NORMAL].palette = item->look[MS_DISABLED].palette = 1;
    item->look[MS_FOCUS].palette = item->look[MS_PUSHED].palette = 0;
    item->look[MS_PUSHED].shift = (ivec2_t){1, 1};
    item->align = MALIGN_CENTER;
    item->stretch = false;
    item->fill = item->sheet ? 0 : 0xff18242du;
    item->look[MS_NORMAL].cell = item->sheet ? normal : -1;
    item->look[MS_FOCUS].cell = item->sheet ? normal : -1;
    item->look[MS_PUSHED].cell = item->sheet ? pressed : -1;
    item->look[MS_DISABLED].cell = item->sheet && normal > 0 ? normal - 1 : -1;
    if (item->sheet) {
        irect_t r = item->sheet->cells[normal].rect;
        if (r.w != item->rect.w || r.h != item->rect.h) item->stretch = true;
    }
}

static void bind_button(menuitem_t *item, const spritesheet_t *sheet) {
    int normal = 0, pressed = 0;
    bool framed = widget_cells(sheet, (isize2_t){item->rect.w, item->rect.h}, &normal, &pressed);
    bind_widget(item, framed ? sheet : NULL, normal, pressed);
}

/* ── the front-end screens ───────────────────────────────────────────────── */

enum { SCREEN_ITEMS = 80, MAX_SCENARIOS = 128, MAX_SAVES = 64 };

typedef struct { menu_t menu; menuitem_t items[SCREEN_ITEMS]; } screen_t;

typedef struct { const char *text; int at, len; } label_t;
typedef struct { char path[1200]; saveinfo_t info; } saveentry_t;

static screen_t title_screen, game_screen, single_screen, campaign_screen, setup_screen, pick_screen, credits_screen, options_screen,
                result_screen, stats_screen, file_screen, dialog_screen, brief_screen, net_screen;
static char data_root[1024];
static app_t *front_app;
static int resources_mode, launch_resources;
static char launch_path[1200];
static struct { char file[1200]; char label[160]; w2_pud_info_t info; int archive; } entries[MAX_SCENARIOS];
static int entry_count;
/* The setup's scenario: a MAINDAT entry, or a file in the data directory. */
static struct { int archive; char file[1200]; char name[160]; } chosen;
static int scenario_type, size_filter;
static menu_t *options_return, *file_return;
static int button_race = 1; /* widget art of the screen being built; the front end is orc */
static saveentry_t saves[MAX_SAVES];
static int save_count, name_item;
static bool saving;
static char credits_text[3072], briefing_text[4096], objective_text[2048];
static char method_name[3][160], method_desc[512];
enum { NUM_RESOURCES = 4 };
/* Engine ranges on the native sliders. Sound and music are gamesettings;
 * CD, mouse and keyboard speeds are stored only. Fog, mouse style and the
 * minimap lines are the screen dialog's radios, stored and not applied. */
static int saved_sound, saved_music, saved_speed, saved_cd, cd_volume = 10;
static int mouse_speed = 10, key_speed = 10;
static int speech_on = 1, ack_on = 1, building_on = 1, cd_music_on = 1;
static int mouse_style, fog_mode, map_info, show_tips = 1, key_page, net_dialog;
static char modem_line[3][40];
static int modem_tone = 1;
enum { CONFIRM_SURRENDER = 1, CONFIRM_RESTART, CONFIRM_MENU, CONFIRM_QUIT, CONFIRM_LOBBY, CONFIRM_CUSTOM };
static int confirm_kind;
static bool brief_from_menu, brief_orc;
static int brief_level;
static menu_t *dialog_return;
static char net_text[NETTEXT_COUNT][160];

/* Text comes from STRDAT when the data has it, and from the fallback if not. */
static label_t L(int entry, int index, const char *fallback) {
    static w2_text_t ring[16];
    static int next;
    w2_text_t *text = &ring[next++ & 15];
    if (w2_label(entry, index, text)) return (label_t){text->text, text->mark_at, text->mark_len};
    return (label_t){fallback, 0, 0};
}

static void apply_label(menuitem_t *item, label_t text) {
    snprintf(item->text, sizeof(item->text), "%s", text.text);
    item->mark_at = text.at;
    item->mark_len = text.len;
}

static const bitmapfont_t *large(void) { return art.font.sprite.numlumps ? &art.font : NULL; }
static const bitmapfont_t *small(void) {
    return art.small_font.sprite.numlumps ? &art.small_font : large();
}
static const bitmapfont_t *tiny(void) {
    return art.tiny_font.sprite.numlumps ? &art.tiny_font : small();
}

static void show(menu_t *menu) {
    menu->app = front_app;
    M_SetupNextMenu(menu);
}

static void screen_begin(screen_t *screen, const spritesheet_t *background,
                         void (*escape)(menu_t *menu)) {
    memset(screen, 0, sizeof(*screen));
    screen->menu.items = screen->items;
    screen->menu.modal = true;
    screen->menu.itemOn = -1;
    screen->menu.escape = escape;
    screen->menu.drawitem = w2_draw_item;
    if (background && background->numlumps) {
        screen->menu.background = background;
        screen->menu.palette = background->source_palette;
    }
}

static menuitem_t *screen_add(screen_t *screen, menuitemkind_t kind, irect_t rect) {
    int index = screen->menu.numitems < SCREEN_ITEMS ? screen->menu.numitems++ : SCREEN_ITEMS - 1;
    menuitem_t *item = &screen->items[index];
    memset(item, 0, sizeof(*item));
    item->kind = kind;
    item->rect = rect;
    item->visible = true;
    item->enabled = kind != MI_STATIC;
    item->link = -1;
    return item;
}

static bool append_scene(screen_t *screen, int entry, const spritesheet_t *panel) {
    int first = screen->menu.numitems, count;
    if (!w2_load_scene(data_root, entry, screen->items + first, SCREEN_ITEMS - first, &count)) {
        fprintf(stderr, "warcraft-2: cannot load native dialog %d\n", entry);
        return false;
    }
    screen->menu.numitems += count;
    for (int i = first + 1; i < screen->menu.numitems; ++i) {
        menuitem_t *item = &screen->items[i];
        unsigned rec = item->flags;
        for (int state = 0; state < MS_STATES; ++state) item->look[state].palette = 1;
        item->disabled_look = true;
        if (item->kind == MI_BUTTON) bind_button(item, &art.widgets[button_race]);
        else if (item->kind == MI_CHECK || item->kind == MI_SLIDER || item->kind == MI_LIST ||
                 item->kind == MI_DROPDOWN || item->kind == MI_SCROLLBAR || item->kind == MI_TEXTFIELD)
            item->sheet = &art.widgets[button_race];
        /* 0x0800 is MAINDAT 281, 0x0400 is 283, otherwise the dialog font 282. */
        item->font = rec & 0x0800 ? large() : rec & 0x0400 ? tiny() : small();
    }
    menuitem_t *window = &screen->items[first];
    window->sheet = panel && panel->numlumps ? panel : NULL;
    window->opaque = true;
    for (int state = 0; state < MS_STATES; ++state) window->look[state].cell = window->sheet ? 0 : -1;
    return true;
}

static bool native_scene(screen_t *screen, int entry, const spritesheet_t *background,
                         const spritesheet_t *panel, void (*escape)(menu_t *)) {
    screen_begin(screen, background, escape);
    return append_scene(screen, entry, panel);
}

static void campaign_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void pick_action(menu_t *menu, menuitem_t *item, menuaction_t action);

static void open_title(void) {
    /* The front end's own first screen: G_ControlPanel builds it. */
    menu_t *title = G_ControlPanel(front_app, false);
    if (title) show(title);
}

static void open_single(void);
static void open_setup(void);
static void open_scenario(void);
static void open_file(bool save, menu_t *back);

static void escape_to_title(menu_t *menu) { (void)menu; open_title(); }
static void escape_to_single(menu_t *menu) { (void)menu; open_single(); }
static void escape_to_setup(menu_t *menu) { (void)menu; open_setup(); }

static const spritesheet_t *backdrop(void) { return &art.title; }

static void open_single(void) {
    button_race = 1;
    if (!native_scene(&single_screen, 6007, backdrop(), NULL, escape_to_title)) return;
    for (int i = 1; i < single_screen.menu.numitems; ++i) {
        menuitem_t *item = &single_screen.items[i];
        item->hotkey = item->id == 1 ? SDLK_n : item->id == 2 ? SDLK_l :
                       item->id == 3 ? SDLK_c : SDLK_ESCAPE;
        item->routine = single_action;
    }
    show(&single_screen.menu);
}

static void open_campaign(void) {
    button_race = 1;
    if (!native_scene(&campaign_screen, 3043, backdrop(), NULL, escape_to_single)) return;
    for (int i = 1; i < campaign_screen.menu.numitems; ++i) {
        menuitem_t *item = &campaign_screen.items[i];
        item->routine = campaign_action;
        item->hotkey = item->id == 1 ? SDLK_o : item->id == 2 ? SDLK_h : SDLK_ESCAPE;
    }
    show(&campaign_screen.menu);
}

/* STRDAT keeps each campaign map's name: pairs of human then orc after the objectives. */
static void level_title(int level, bool orc, char *out, size_t size) {
    label_t title = L(STR_LEVELS, 35 + 2 * (level - 1) + (orc ? 1 : 0), "");
    snprintf(out, size, "%s", title.text[0] ? title.text : M_va("%d", level));
}

static int compare_entries(const void *a, const void *b) {
    return strcasecmp(((const char *)a), ((const char *)b)); /* file is the first member */
}

static const char *pick_row(const menuitem_t *item, int row) {
    (void)item;
    return row >= 0 && row < entry_count ? entries[row].label : "";
}

/* Dropdown choices: strings of one STRDAT table, in the native order. */
typedef struct { int entry, count; int index[8]; const char *fallback[8]; } choices_t;

static const char *choice_row(const menuitem_t *item, int row) {
    const choices_t *choices = item->userdata;
    if (!choices || row < 0 || row >= choices->count) return "";
    return L(choices->entry, choices->index[row], choices->fallback[row]).text;
}

/* Scenario picker contents (0x16ed4, 0x171bc, 0x173b4): single player offers
 * built-in and custom scenarios; Players is the multiplayer filter. */
static const choices_t scenario_types = {STR_SCENARIOS, 2, {0, 1}, {"Built-in scenario", "Custom scenario"}};
static const choices_t map_sizes = {STR_SCENARIOS, 5, {5, 6, 7, 8, 9},
                                    {"Any size", "32 x 32", "64 x 64", "96 x 96", "128 x 128"}};
/* Custom game setup dropdowns (0x14f70), by native control ID. */
static const choices_t setup_races = {STR_SETUP_VALUES, 3, {19, 20, 10}, {"Human", "Orc", "Map Default"}};
static const choices_t setup_opponents = {STR_SETUP_VALUES, 8, {10, 26, 27, 28, 29, 30, 31, 32},
    {"Map Default", "1 Opponent", "2 Opponents", "3 Opponents", "4 Opponents", "5 Opponents",
     "6 Opponents", "7 Opponents"}};
static const choices_t setup_resources = {STR_SETUP_VALUES, NUM_RESOURCES, {10, 11, 12, 13},
                                          {"Map Default", "Low", "Medium", "High"}};
static const choices_t setup_terrain = {STR_SETUP_VALUES, 4, {10, 21, 22, 23},
                                        {"Map Default", "Forest", "Winter", "Wasteland"}};
static const choices_t setup_units = {STR_SETUP_VALUES, 2, {10, 18}, {"Map Default", "One Peasant Only"}};
static const choices_t setup_placement = {STR_SETUP_VALUES, 2, {17, 16}, {"Random", "Fixed"}};

/* The native list and dropdown rows are REZDAT widget frames 45/46, one
 * frame high (0x5910c reads the row height from frame 45). */
static void native_rows(menuitem_t *item) {
    item->sheet = &art.widgets[1];
    item->disabled_look = true;
    item->row_height = art.widgets[1].numlumps > 45 ? art.widgets[1].cells[45].rect.h : 18;
}

static void bind_choice(menuitem_t *item, const choices_t *choices, int value, menuroutine_t routine) {
    native_rows(item);
    item->routine = routine;
    item->row = choice_row;
    item->userdata = choices;
    item->rows = choices->count;
    item->value = value;
    item->popup_rows = 6;
}

/* Native player count: the slots a person plays (owner 5). */
static int scenario_players(const w2_pud_info_t *info) {
    int players = 0;
    for (int i = 0; i < 8; ++i) players += info->owners[i] == 5;
    return players;
}

/* 0x16e00: 32/64/96/128 are sizes 1..4; anything else matches every filter. */
static int size_index(const w2_pud_info_t *info) {
    return info->width == info->height && info->width % 32 == 0 && info->width <= 128 ? info->width / 32 : 0;
}

/* Built-in scenarios are MAINDAT 220..247 with names STRDAT 63/22..49
 * (0x17618). Custom ones are the data directory's *.PUD files, found as
 * plain files (0x177e8 searches with attribute 0) and named by their
 * lowercased file name (0x5f0d1). */
static void scan_custom(void) {
    entry_count = 0;
    DIR *dir = opendir(data_root);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir)) && entry_count < MAX_SCENARIOS) {
        size_t len = strlen(ent->d_name);
        if (len < 5 || len >= sizeof(entries[0].file) || strcasecmp(ent->d_name + len - 4, ".pud")) continue;
        char path[1200];
        M_PathJoin(path, sizeof(path), data_root, ent->d_name);
        struct stat st;
        if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;
        memset(&entries[entry_count], 0, sizeof(entries[0]));
        if (!w2_pud_info(path, &entries[entry_count].info)) continue;
        snprintf(entries[entry_count].file, sizeof(entries[0].file), "%s", ent->d_name);
        for (size_t i = 0; i <= len && i < sizeof(entries[0].label); ++i)
            entries[entry_count].label[i] = (char)tolower((unsigned char)ent->d_name[i]);
        ++entry_count;
    }
    closedir(dir);
    qsort(entries, (size_t)entry_count, sizeof(entries[0]), compare_entries);
}

static void scan_builtin(void) {
    entry_count = 0;
    char path[1200];
    M_PathJoin(path, sizeof(path), data_root, "DATA/MAINDAT.WAR");
    w2_archive_t archive;
    if (!w2_archive_open(&archive, path)) return;
    for (int i = 0; i < W2_SCENARIOS; ++i) {
        w2_blob_t blob = {0};
        w2_pud_info_t info;
        bool ok = w2_archive_extract(&archive, W2_FIRST_SCENARIO + i, &blob) &&
                  w2_pud_info_bytes(blob.data, blob.size, &info);
        w2_blob_free(&blob);
        if (!ok) continue;
        memset(&entries[entry_count], 0, sizeof(entries[0]));
        entries[entry_count].info = info;
        entries[entry_count].archive = W2_FIRST_SCENARIO + i;
        snprintf(entries[entry_count].label, sizeof(entries[0].label), "%s",
                 L(STR_SCENARIOS, 22 + i, info.description).text);
        ++entry_count;
    }
    w2_archive_close(&archive);
}

static bool chosen_entry(int row) {
    return row >= 0 && row < entry_count &&
           (entries[row].archive ? entries[row].archive == chosen.archive :
                                   !chosen.archive && !strcasecmp(entries[row].file, chosen.file));
}

static int custom_count(void) {
    scan_custom();
    return entry_count;
}

/* The setup's scenario line (0x13e84): the type, then the name. */
static void describe_choice(char *out, size_t size) {
    snprintf(out, size, "%s\n%s", L(STR_SETUP_VALUES, chosen.archive ? 0 : 1,
             chosen.archive ? "Built-in scenario" : "Custom scenario").text, chosen.name);
}

static void setup_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void pick_action(menu_t *menu, menuitem_t *item, menuaction_t action);

/* Custom game setup is MUDDAT 6001 in single player (0x15d58). Only the
 * starting resources are carried into the game; the other choices show
 * the map's own settings and stay disabled until the engine applies them. */
static void open_setup(void) {
    if (!chosen.name[0]) {
        scan_builtin();
        if (entry_count) {
            chosen.archive = entries[0].archive;
            snprintf(chosen.name, sizeof(chosen.name), "%s", entries[0].label);
        }
    }
    button_race = 1;
    screen_t *s = &setup_screen;
    if (!native_scene(s, 6001, backdrop(), NULL, escape_to_single)) return;
    static const struct { int id; const choices_t *choices; } dropdowns[] = {
        {10, &setup_races}, {7, &setup_opponents}, {4, &setup_resources},
        {8, &setup_terrain}, {6, &setup_units}, {5, &setup_placement},
    };
    for (size_t i = 0; i < sizeof(dropdowns) / sizeof(dropdowns[0]); ++i) {
        menuitem_t *item = M_MenuFind(&s->menu, dropdowns[i].id);
        if (!item) continue;
        bool resources = dropdowns[i].id == 4;
        bind_choice(item, dropdowns[i].choices, resources ? resources_mode : dropdowns[i].id == 10 ? 2 : 0,
                    setup_action);
        item->enabled = resources && item->enabled;
    }
    menuitem_t *line = M_MenuFind(&s->menu, 11);
    if (line) describe_choice(line->text, sizeof(line->text));
    for (int i = 1; i < s->menu.numitems; ++i) {
        menuitem_t *item = &s->items[i];
        if (item->kind != MI_BUTTON) continue;
        item->routine = setup_action;
        item->hotkey = item->id == 2 ? SDLK_s : item->id == 3 ? SDLK_e : SDLK_ESCAPE;
    }
    show(&s->menu);
}

static void pick_changed(screen_t *s, int row) {
    for (int id = 5; id <= 7; ++id) M_MenuFind(&s->menu, id)->text[0] = '\0';
    bool live = row >= 0 && row < entry_count;
    if (live) {
        const w2_pud_info_t *info = &entries[row].info;
        snprintf(M_MenuFind(&s->menu, 5)->text, sizeof(s->items[0].text), "%s",
                 entries[row].archive ? entries[row].label : info->description);
        int size = size_index(info), players = scenario_players(info);
        if (size) snprintf(M_MenuFind(&s->menu, 6)->text, sizeof(s->items[0].text), "%s",
                           L(STR_SCENARIOS, size + 5, map_sizes.fallback[size]).text);
        if (players) snprintf(M_MenuFind(&s->menu, 7)->text, sizeof(s->items[0].text), "%s",
                              L(STR_SCENARIOS, players + 12, M_va("%d players", players)).text);
    }
    M_MenuFind(&s->menu, -2)->enabled = live;
}

/* REZDAT 89 over the setup screen, which stays visible and inert. */
static void open_scenario(void) {
    screen_t *s = &pick_screen;
    *s = setup_screen;
    s->menu.items = s->items;
    for (int i = 0; i < s->menu.numitems; ++i) {
        if (s->items[i].enabled) s->items[i].disabled_look = false;
        s->items[i].enabled = false;
        s->items[i].hotkey = 0;
        s->items[i].id = 0;
        s->items[i].routine = NULL;
    }
    s->menu.held = s->menu.keyheld = s->menu.dropdown = NULL;
    s->menu.escape = escape_to_setup;
    button_race = 1;
    if (scenario_type) scan_custom(); else scan_builtin();
    if (size_filter) {
        int kept = 0;
        for (int i = 0; i < entry_count; ++i) {
            int size = size_index(&entries[i].info);
            if (!size || size == size_filter) entries[kept++] = entries[i];
        }
        entry_count = kept;
    }
    int panel_index = s->menu.numitems;
    if (!append_scene(s, 3089, &art.panel[1][W2_PANEL_SCENARIO])) return;
    s->items[panel_index].layer = true;
    menuitem_t *type = M_MenuFind(&s->menu, 2), *size = M_MenuFind(&s->menu, 3);
    menuitem_t *players = M_MenuFind(&s->menu, 4), *ok = M_MenuFind(&s->menu, -2);
    menuitem_t *cancel = M_MenuFind(&s->menu, -3), *bar = M_MenuFind(&s->menu, (int16_t)0x8001);
    menuitem_t *list = M_MenuFind(&s->menu, 1);
    if (!type || !size || !players || !ok || !cancel || !bar || !list ||
        !M_MenuFind(&s->menu, 5) || !M_MenuFind(&s->menu, 6) || !M_MenuFind(&s->menu, 7)) return;
    bind_choice(type, &scenario_types, scenario_type, pick_action);
    bind_choice(size, &map_sizes, size_filter, pick_action);
    /* Outside multiplayer 0x14bc8 hides the Players filter and its caption (ID 9). */
    players->visible = players->enabled = false;
    if (M_MenuFind(&s->menu, 9)) M_MenuFind(&s->menu, 9)->visible = false;
    /* 0x5ae28: whole rows two pixels inside the control; the scroll bar
     * (ID 0x8001) is the list's height, one arrow wide, against its right. */
    irect_t control = list->rect;
    native_rows(list);
    int rows = (control.h - 3) / list->row_height;
    control.h = rows * list->row_height + 4;
    list->rect = (irect_t){control.x + 2, control.y + 2, control.w - 4, rows * list->row_height};
    list->routine = pick_action;
    list->row = pick_row;
    list->value = -1;
    for (int i = 0; i < entry_count; ++i)
        if (chosen_entry(i)) list->value = i;
    int list_index = (int)(list - s->items);
    list->first_row = list->value < 0 ? 0 : list->value;
    M_MenuSetRows(list, entry_count);
    isize2_t arrow = {art.widgets[1].cells[28].rect.w, art.widgets[1].cells[28].rect.h};
    bar->visible = bar->enabled = true;
    bar->rect = (irect_t){control.x + control.w, control.y + arrow.h, arrow.w, control.h - 2 * arrow.h};
    bar->sheet = &art.widgets[1];
    bar->link = list_index;
    bar->thumb = (menulook_t){.cell = 40, .part = art.widgets[1].cells[40].rect};
    for (int i = 0; i < 2; ++i) {
        menuitem_t *step = screen_add(s, MI_BUTTON,
            (irect_t){bar->rect.x, control.y + (control.h - arrow.h) * i, arrow.w, arrow.h});
        bind_widget(step, &art.widgets[1], i ? 32 : 29, i ? 33 : 30);
        step->link = list_index;
        step->step = i ? 1 : -1;
    }
    ok->routine = cancel->routine = pick_action;
    ok->hotkey = SDLK_RETURN;
    cancel->hotkey = SDLK_ESCAPE;
    s->menu.itemOn = list_index;
    pick_changed(s, list->value);
    show(&s->menu);
}

static void set_launch_path(const char *path) {
    if (path == launch_path) return;
    size_t root = strlen(data_root);
    if (!strncmp(path, data_root, root) && path[root] == '/') path += root + 1;
    snprintf(launch_path, sizeof(launch_path), "%s", path);
}

static void start_level(const char *path, bool apply_resources) {
    set_launch_path(path);
    launch_resources = apply_resources ? resources_mode : 0;
    W2_SetStartResources(launch_resources);
    menumap = launch_path;
    M_ClearMenus();
}

static const spritesheet_t *game_panel(void) { return &art.panel[button_race][W2_PANEL_GAME]; }

static void wire(screen_t *s, menuroutine_t routine) {
    for (int i = 1; i < s->menu.numitems; ++i) {
        menuitem_t *item = &s->items[i];
        if (item->kind == MI_BUTTON || item->kind == MI_CHECK || item->kind == MI_SLIDER ||
            item->kind == MI_LIST || item->kind == MI_TEXTFIELD)
            item->routine = routine;
    }
}

static void hide_id(screen_t *s, int id) {
    menuitem_t *item = M_MenuFind(&s->menu, id);
    if (item) item->visible = item->enabled = false;
}

/* Grab width is two 20px caps plus the 17px knob, so the engine's drag
 * centre sits on the knob drawn between the caps. */
enum { SLIDER_GRAB = 57 };

static void bind_slider(menuitem_t *item, int min, int max, int value) {
    if (!item) return;
    if (value < min) value = min;
    if (value > max) value = max;
    item->range.min = min;
    item->range.max = max;
    item->value = value;
    item->thumb.cell = 40;
    item->thumb.part = (irect_t){0, 0, SLIDER_GRAB, 17};
}

static void link_bar(screen_t *s, menuitem_t *list, int bar_id) {
    menuitem_t *bar = M_MenuFind(&s->menu, bar_id);
    if (!list || !bar) return;
    bar->flags |= W2_ITEM_ARROWS;
    bar->sheet = &art.widgets[button_race];
    bar->link = (int)(list - s->items);
    if (art.widgets[button_race].numlumps > 40)
        bar->thumb = (menulook_t){.cell = 40, .part = art.widgets[button_race].cells[40].rect};
}

static void set_radio(menu_t *menu, int group, int on_id, int off_id, bool on_first) {
    menuitem_t *on = M_MenuFind(menu, on_id), *off = M_MenuFind(menu, off_id);
    if (on) { on->group = group; on->value = on_first; }
    if (off) { off->group = group; off->value = !on_first; }
}

static void remember_settings(void) {
    saved_sound = gamesettings.sound;
    saved_music = gamesettings.music;
    saved_speed = game_speed;
    saved_cd = cd_volume;
}

static void restore_settings(void) {
    gamesettings.sound = saved_sound;
    gamesettings.music = saved_music;
    cd_volume = saved_cd;
    S_SetVolume(saved_sound * 10);
    D_SetGameSpeed(saved_speed);
}

static void show_parent(menu_t *menu) {
    (void)menu;
    show(options_return);
}

static void show_dialog(menu_t *menu) {
    (void)menu;
    show(dialog_return);
}

/* STRDAT 64+ is one briefing string per side, human then orc. */
static void begin_briefed_level(void) {
    char path[sizeof(launch_path)];
    if (!w2_extract_campaign_level(data_root, brief_level, brief_orc, path, sizeof(path))) {
        M_StartMessage("The campaign level could not be read.");
        return;
    }
    W2_SetCampaign(brief_level, brief_orc);
    start_level(path, false);
}

static void briefing_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (action == MA_ACTIVATE && item->id == -2) begin_briefed_level();
}

static void briefing_escape(menu_t *menu) {
    (void)menu;
    if (brief_from_menu) open_campaign();
    else show(&stats_screen.menu);
}

static void open_briefing(void) {
    button_race = brief_orc ? 1 : 0;
    if (!native_scene(&brief_screen, brief_orc ? 3083 : 3082, &art.dimmed, NULL, briefing_escape)) return;
    int side_index = (brief_orc ? 1 : 0);
    w2_label_copy(64 + 2 * (brief_level - 1) + side_index, 0, briefing_text, sizeof(briefing_text));
    menuitem_t *body = M_MenuFind(&brief_screen.menu, 1);
    if (body) { body->prose = briefing_text; body->text[0] = '\0'; }
    w2_label_copy(STR_LEVELS, 2 * (brief_level - 1) + side_index, objective_text, sizeof(objective_text));
    menuitem_t *goals = M_MenuFind(&brief_screen.menu, -4);
    if (goals) { goals->prose = objective_text; goals->text[0] = '\0'; }
    menuitem_t *title = M_MenuFind(&brief_screen.menu, -5);
    if (title) level_title(brief_level, brief_orc, title->text, sizeof(title->text));
    wire(&brief_screen, briefing_action);
    show(&brief_screen.menu);
}

static void start_campaign(bool orc) {
    brief_level = 1;
    brief_orc = orc;
    brief_from_menu = true;
    open_briefing();
}

/* Credits are REZDAT 84 over the title. The prose is STRDAT 57. */
static void credits_back(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_credits_screen(void) {
    button_race = 1;
    if (!w2_label_copy(STR_CREDITS, 0, credits_text, sizeof(credits_text)))
        snprintf(credits_text, sizeof(credits_text),
                 "open-rts\nAn open reimplementation of classic real-time strategy engines.\n\n"
                 "Warcraft II: Tides of Darkness was made by Blizzard Entertainment.");
    if (!native_scene(&credits_screen, 3084, &art.title, NULL, escape_to_title)) return;
    menuitem_t *body = M_MenuFind(&credits_screen.menu, 1);
    if (body) { body->prose = credits_text; body->text[0] = '\0'; }
    wire(&credits_screen, credits_back);
    show(&credits_screen.menu);
}

static void credits_back(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action == MA_ACTIVATE) escape_to_title(menu);
}

static void show_credits(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu; (void)item;
    if (action == MA_ACTIVATE) open_credits_screen();
}

/* ── in-game dialogs: the game menu's result switch ──────────────────────── */

static int playing(void) { return level.width != 0; }

static void options_escape(menu_t *menu) {
    (void)menu;
    D_SaveSettings(game_speed);
    show(options_return);
}

static void options_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void sound_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void speed_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void screen_action(menu_t *menu, menuitem_t *item, menuaction_t action);

static void sound_cancel(menu_t *menu) {
    (void)menu;
    restore_settings();
    show_dialog(menu);
}

static void open_sound(void) {
    remember_settings();
    dialog_return = &options_screen.menu;
    button_race = playing() ? side() : 1;
    if (!native_scene(&dialog_screen, 3048, playing() ? NULL : &art.dimmed, game_panel(), sound_cancel)) return;
    menu_t *m = &dialog_screen.menu;
    bind_slider(M_MenuFind(m, 1), 0, 10, gamesettings.music);
    bind_slider(M_MenuFind(m, 2), 0, 10, gamesettings.sound);
    bind_slider(M_MenuFind(m, 3), 0, 10, cd_volume);
    menuitem_t *cd = M_MenuFind(m, 7), *speech = M_MenuFind(m, 4);
    menuitem_t *ack = M_MenuFind(m, 5), *builds = M_MenuFind(m, 6);
    if (cd) cd->value = cd_music_on;
    if (speech) speech->value = speech_on;
    if (ack) ack->value = ack_on;
    if (builds) builds->value = building_on;
    wire(&dialog_screen, sound_action);
    show(m);
}

static void open_speed(void) {
    remember_settings();
    dialog_return = &options_screen.menu;
    button_race = playing() ? side() : 1;
    if (!native_scene(&dialog_screen, 3050, playing() ? NULL : &art.dimmed, game_panel(), sound_cancel)) return;
    menu_t *m = &dialog_screen.menu;
    bind_slider(M_MenuFind(m, 1), 10, 200, game_speed);
    bind_slider(M_MenuFind(m, 2), 0, 10, mouse_speed);
    bind_slider(M_MenuFind(m, 3), 0, 10, key_speed);
    wire(&dialog_screen, speed_action);
    show(m);
}

static void open_screen_dialog(void) {
    dialog_return = &options_screen.menu;
    button_race = playing() ? side() : 1;
    if (!native_scene(&dialog_screen, 3049, playing() ? NULL : &art.dimmed, game_panel(), show_dialog)) return;
    menu_t *m = &dialog_screen.menu;
    set_radio(m, 1, 5, 4, mouse_style == 0);
    set_radio(m, 2, 1, 2, fog_mode == 0);
    set_radio(m, 3, 7, 8, map_info == 0);
    wire(&dialog_screen, screen_action);
    show(m);
}

static void sound_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (item->kind == MI_SLIDER && action == MA_CHANGE) {
        if (item->id == 1) gamesettings.music = item->value;
        else if (item->id == 2) { gamesettings.sound = item->value; S_SetVolume(item->value * 10); }
        else if (item->id == 3) cd_volume = item->value;
        return;
    }
    if (action != MA_ACTIVATE || item->kind == MI_CHECK) return;
    if (item->id == -2) {
        menuitem_t *cd = M_MenuFind(menu, 7), *speech = M_MenuFind(menu, 4);
        menuitem_t *ack = M_MenuFind(menu, 5), *builds = M_MenuFind(menu, 6);
        if (cd) cd_music_on = cd->value;
        if (speech) speech_on = speech->value;
        if (ack) ack_on = ack->value;
        if (builds) building_on = builds->value;
        D_SaveSettings(game_speed);
        show_dialog(menu);
    } else if (item->id == -3) sound_cancel(menu);
}

static void speed_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (item->kind == MI_SLIDER && action == MA_CHANGE && item->id == 1) D_SetGameSpeed(item->value);
    if (action != MA_ACTIVATE) return;
    if (item->id == -2) {
        menuitem_t *mouse = M_MenuFind(menu, 2), *keys = M_MenuFind(menu, 3);
        if (mouse) mouse_speed = mouse->value;
        if (keys) key_speed = keys->value;
        D_SaveSettings(game_speed);
        show_dialog(menu);
    } else if (item->id == -3) sound_cancel(menu);
}

static void screen_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE || item->kind == MI_CHECK) return;
    if (item->id == -2) {
        menuitem_t *mouse = M_MenuFind(menu, 5), *fog = M_MenuFind(menu, 1), *info = M_MenuFind(menu, 7);
        if (mouse) mouse_style = mouse->value ? 0 : 1;
        if (fog) fog_mode = fog->value ? 0 : 1;
        if (info) map_info = info->value ? 0 : 1;
    }
    if (item->id == -2 || item->id == -3) show_dialog(menu);
}

static void options_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    if (item->id == 1) open_sound();
    else if (item->id == 2) open_speed();
    else if (item->id == 3) open_screen_dialog();
    else if (item->id == -3) options_escape(menu);
}

static void open_options_screen(menu_t *back, bool inlevel) {
    options_return = back;
    button_race = inlevel ? side() : 1;
    if (!native_scene(&options_screen, 3047, inlevel ? NULL : &art.dimmed, game_panel(), options_escape)) return;
    wire(&options_screen, options_action);
    show(&options_screen.menu);
}

static void open_options(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action == MA_ACTIVATE) open_options_screen(menu, true);
}

/* Help menu 3045, then the key pages (3086) or the tips dialog (3077). */
static void fill_key_page(void) {
    screen_t *s = &dialog_screen;
    menuitem_t *lines[16];
    int n = 0, top = s->menu.numitems ? s->items[0].rect.y + 30 : 0;
    for (int i = 1; i < s->menu.numitems && n < 16; ++i) {
        menuitem_t *line = &s->items[i];
        if (line->kind != MI_STATIC || line->rect.y < top || line->rect.h > 24) continue;
        lines[n++] = line;
    }
    for (int a = 1; a < n; ++a) {
        menuitem_t *key = lines[a];
        int b = a;
        while (b > 0 && lines[b - 1]->rect.y > key->rect.y) { lines[b] = lines[b - 1]; --b; }
        lines[b] = key;
    }
    if (n > 13) n = 13;
    if (key_page < 0) key_page = 0;
    if (key_page > 3) key_page = 3;
    for (int i = 0; i < n; ++i) {
        int index = key_page * 13 + i;
        w2_text_t text;
        if (index < 42 && w2_label(STR_KEYS, index, &text))
            snprintf(lines[i]->text, sizeof(lines[i]->text), "%s", text.text);
        else lines[i]->text[0] = '\0';
    }
}

static void key_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (action != MA_ACTIVATE) return;
    if (item->id == 1) { if (key_page > 0) --key_page; }
    else if (item->id == 2) { if (key_page < 3) ++key_page; }
    else { show_dialog(menu); return; }
    fill_key_page();
}

static void open_key_help(void) {
    dialog_return = &options_screen.menu;
    button_race = playing() ? side() : 1;
    key_page = 0;
    if (!native_scene(&dialog_screen, 3086, playing() ? NULL : &art.dimmed,
                      &art.panel[button_race][W2_PANEL_SCENARIO], show_dialog)) return;
    fill_key_page();
    wire(&dialog_screen, key_action);
    show(&dialog_screen.menu);
}

static void remember_tips(void) {
    menuitem_t *box = M_MenuFind(&dialog_screen.menu, 2);
    if (box && box->kind == MI_CHECK) show_tips = box->value;
}

static void tips_escape(menu_t *menu) {
    remember_tips();
    show_dialog(menu);
}

static void tips_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    if (item->id == 2) { show_tips = item->value; return; }
    if (item->id == -2 || item->id == -3) tips_escape(menu);
}

static void open_tips(void) {
    dialog_return = &options_screen.menu;
    button_race = playing() ? side() : 1;
    if (!native_scene(&dialog_screen, 3077, playing() ? NULL : &art.dimmed,
                      &art.panel[button_race][W2_PANEL_OPTIONS], tips_escape)) return;
    menuitem_t *box = M_MenuFind(&dialog_screen.menu, 2);
    if (box) box->value = show_tips;
    wire(&dialog_screen, tips_action);
    show(&dialog_screen.menu);
}

static void help_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    if (item->id == 1) open_key_help();
    else if (item->id == 2) open_tips();
    else if (item->id == -3) show_parent(menu);
}

static void open_help_menu(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action != MA_ACTIVATE) return;
    options_return = menu;
    button_race = playing() ? side() : 1;
    if (!native_scene(&options_screen, 3045, playing() ? NULL : &art.dimmed, game_panel(), show_parent)) return;
    wire(&options_screen, help_action);
    show(&options_screen.menu);
}

static void open_objectives(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action != MA_ACTIVATE) return;
    options_return = menu;
    button_race = playing() ? side() : 1;
    if (!native_scene(&options_screen, 3081, playing() ? NULL : &art.dimmed, game_panel(), show_parent)) return;
    const w2_mission_t *mission = level.mission;
    int index = mission && mission->campaign.number > 0
                    ? 2 * (mission->campaign.number - 1) + (mission->campaign.orc ? 1 : 0) : 34;
    w2_label_copy(STR_LEVELS, index, objective_text, sizeof(objective_text));
    menuitem_t *body = M_MenuFind(&options_screen.menu, -4);
    if (body) { body->prose = objective_text; body->text[0] = '\0'; }
    wire(&options_screen, help_action);
    show(&options_screen.menu);
}

/* End Mission opens 3046. Bytes 0x8127a and 0x80331 choose which of the three
 * overlapping buttons stays; who writes them was not traced. A network game
 * shows the lobby return, a custom scenario its reload, otherwise restart. */
static void confirm_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_confirm(int resource);

static void end_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    if (item->id == -3) { show_parent(menu); return; }
    int resource = 0;
    switch (item->id) {
    case 1: resource = 3055; confirm_kind = CONFIRM_SURRENDER; break;
    case 2: resource = 3052; confirm_kind = CONFIRM_RESTART; break;
    case 3: resource = 3056; confirm_kind = CONFIRM_MENU; break;
    case 4: resource = 3051; confirm_kind = CONFIRM_QUIT; break;
    case 5: resource = 3053; confirm_kind = CONFIRM_LOBBY; break;
    case 6: resource = 3054; confirm_kind = CONFIRM_CUSTOM; break;
    default: return;
    }
    open_confirm(resource);
}

static void open_end_menu(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action != MA_ACTIVATE) return;
    options_return = menu;
    button_race = playing() ? side() : 1;
    if (!native_scene(&options_screen, 3046, playing() ? NULL : &art.dimmed, game_panel(), show_parent)) return;
    const w2_mission_t *mission = level.mission;
    int number = mission ? mission->campaign.number : 0;
    bool custom = chosen.file[0] && !chosen.archive && number == 0;
    if (netgame) { hide_id(&options_screen, 2); hide_id(&options_screen, 6); }
    else if (custom) { hide_id(&options_screen, 2); hide_id(&options_screen, 5); }
    else { hide_id(&options_screen, 5); hide_id(&options_screen, 6); }
    wire(&options_screen, end_action);
    show(&options_screen.menu);
}

static void reload_level(void) {
    const w2_mission_t *mission = level.mission;
    if (mission) W2_SetCampaign(mission->campaign.number, mission->campaign.orc);
    W2_SetStartResources(launch_resources);
    set_launch_path(level.map_path);
    menumap = launch_path;
    M_ClearMenus();
}

static void confirm_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    if (item->id == -3) { show_dialog(menu); return; }
    if (item->id != -2) return;
    if (confirm_kind == CONFIRM_QUIT) M_MenuQuitGame(menu, item, action);
    else if (confirm_kind == CONFIRM_RESTART || confirm_kind == CONFIRM_CUSTOM) reload_level();
    else end_scenario(menu, item, action);
}

static void open_confirm(int resource) {
    dialog_return = &options_screen.menu;
    button_race = playing() ? side() : 1;
    if (!native_scene(&dialog_screen, resource, playing() ? NULL : &art.dimmed, game_panel(), show_dialog)) return;
    wire(&dialog_screen, confirm_action);
    show(&dialog_screen.menu);
}

/* The end of a scenario: a won campaign level offers the next one; a lost scenario can restart. */
void W2_ShowResult(bool victory) {
    screen_t *s = &result_screen;
    int race = button_race = side();
    w2_mission_t *mission = level.mission;
    if (mission) { mission->done = true; mission->victory = victory; }
    if (!native_scene(s, victory ? 3057 : 3058, NULL, &art.panel[race][W2_PANEL_DIALOG], NULL)) return;
    for (int i = 1; i < s->menu.numitems; ++i) {
        menuitem_t *item = &s->items[i];
        if (item->kind != MI_BUTTON) continue;
        item->routine = result_action;
        item->hotkey = item->id == -2 ? SDLK_RETURN : SDLK_F11;
    }
    button_race = 1;
    show(&s->menu);
}

static void result_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    w2_mission_t *mission = level.mission;
    if (!mission) return;
    if (menu == &result_screen.menu) {
        if (item->id == 1) { open_save(menu, item, action); return; }
        int race = button_race = side();
        if (!native_scene(&stats_screen, 3059, &art.results[race][mission->victory ? 0 : 1], NULL, NULL)) return;
        menuitem_t *result = M_MenuFind(&stats_screen.menu, 1);
        if (result) apply_label(result, L(mission->victory ? STR_WIN : STR_LOSE, 1, ""));
        menuitem_t *next = M_MenuFind(&stats_screen.menu, -2);
        if (next) { next->routine = result_action; next->hotkey = SDLK_RETURN; }
        button_race = 1;
        show(&stats_screen.menu);
        return;
    }
    if (item->id != -2) return;
    w2_campaign_t campaign = mission->campaign;
    if (!mission->victory) {
        W2_SetCampaign(campaign.number, campaign.orc);
        W2_SetStartResources(launch_resources);
        set_launch_path(level.map_path);
        menumap = launch_path;
    } else if (campaign.number > 0 && campaign.number < W2_CAMPAIGN_LEVELS) {
        brief_level = campaign.number + 1;
        brief_orc = campaign.orc;
        brief_from_menu = false;
        open_briefing();
        return;
    } else {
        W2_SetCampaign(0, false);
        menuleave = true;
    }
    M_ClearMenus();
}

/* ── saved games (engine g_save.c, as Doom's) ────────────────────────────── */

static int compare_saves(const void *a, const void *b) {
    return strcasecmp(((const saveentry_t *)a)->info.name, ((const saveentry_t *)b)->info.name);
}

static void scan_saves(void) {
    save_count = 0;
    DIR *dir = opendir(D_UserDirectory());
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir)) && save_count < MAX_SAVES) {
        size_t len = strlen(ent->d_name);
        if (len < 5 || strcmp(ent->d_name + len - 4, ".sav")) continue;
        M_PathJoin(saves[save_count].path, sizeof(saves[0].path), D_UserDirectory(), ent->d_name);
        if (G_SaveInfo(saves[save_count].path, &saves[save_count].info)) ++save_count;
    }
    closedir(dir);
    qsort(saves, (size_t)save_count, sizeof(saves[0]), compare_saves);
}

static const char *save_row(const menuitem_t *item, int row) {
    (void)item;
    return row >= 0 && row < save_count ? saves[row].info.name : "";
}

static void file_escape(menu_t *menu) {
    (void)menu;
    if (playing() && file_return) show(file_return);
    else open_single();
}

static void file_action(menu_t *menu, menuitem_t *item, menuaction_t action);

/* Save is REZDAT 63. Load is 64 over a level and 65 on the front end. */
static void open_file(bool save, menu_t *back) {
    saving = save;
    file_return = back;
    scan_saves();
    bool inlevel = playing();
    button_race = inlevel ? side() : 1;
    int resource = save ? 3063 : inlevel ? 3064 : 3065;
    const spritesheet_t *panel = resource == 3065 ? NULL : &art.panel[button_race][W2_PANEL_FILE];
    screen_t *s = &file_screen;
    if (!native_scene(s, resource, inlevel ? NULL : &art.dimmed, panel, file_escape)) return;
    menuitem_t *list = M_MenuFind(&s->menu, 1);
    if (list) {
        native_rows(list);
        list->row = save_row;
        list->value = -1;
        M_MenuSetRows(list, save_count);
        link_bar(s, list, (int16_t)0x8001);
    }
    name_item = -1;
    if (save) {
        menuitem_t *field = M_MenuFind(&s->menu, 2);
        if (field) {
            field->maxchars = 32;
            name_item = (int)(field - s->items);
            s->menu.itemOn = name_item;
        }
    } else if (list) s->menu.itemOn = (int)(list - s->items);
    wire(s, file_action);
    menuitem_t *ok = M_MenuFind(&s->menu, -2);
    if (ok) ok->hotkey = SDLK_RETURN;
    button_race = 1;
    show(&s->menu);
}

static int file_selection(void) {
    menuitem_t *list = M_MenuFind(&file_screen.menu, 1);
    return list ? list->value : -1;
}

static bool valid_save_name(const char *name) {
    if (!*name) return false;
    for (const char *p = name; *p; ++p)
        if (!(((*p | 32) >= 'a' && (*p | 32) <= 'z') || (*p >= '0' && *p <= '9') || *p == ' ' ||
              *p == '_' || *p == '-')) return false;
    return true;
}

static void file_activate(void) {
    int row = file_selection();
    if (saving) {
        if (name_item < 0) return;
        const char *name = file_screen.items[name_item].text;
        if (!valid_save_name(name)) {
            M_StartMessage("Use letters, numbers, spaces, hyphens or underscores.");
            return;
        }
        snprintf(g_savename, sizeof(g_savename), "%.32s", name);
        M_PathJoin(g_savefile, sizeof(g_savefile), D_UserDirectory(), M_va("%s.sav", g_savename));
        M_ClearMenus();
        return;
    }
    if (row < 0 || row >= save_count) return;
    saveinfo_t info;
    if (!G_SaveInfo(saves[row].path, &info)) {
        M_StartMessage("The saved game is damaged or belongs to other game data.");
        return;
    }
    /* The driver loads the map first, then restores into it. */
    set_launch_path(info.map);
    snprintf(g_loadfile, sizeof(g_loadfile), "%s", saves[row].path);
    menumap = launch_path;
    M_ClearMenus();
}

static void open_save(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action != MA_ACTIVATE) return;
    if (netgame) { M_StartMessage("Saving is available in single-player games."); return; }
    open_file(true, menu);
}

static void open_load(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action != MA_ACTIVATE) return;
    if (netgame) { M_StartMessage("Loading is available in single-player games."); return; }
    open_file(false, menu);
}

/* The campaign, the dice and the launch survive a save, so a loaded campaign level goes on. */
typedef struct { uint32_t dice; int campaign_orc, campaign_level, launch_resources; } extra_t;

size_t G_SaveExtraSize(void) { return sizeof(extra_t); }

void G_SaveExtra(void *out) {
    const w2_mission_t *mission = level.mission;
    extra_t extra = {W2_CombatState(), mission && mission->campaign.orc,
                    mission ? mission->campaign.number : 0, launch_resources};
    memcpy(out, &extra, sizeof(extra));
}

bool G_LoadExtra(const void *data, size_t size) {
    extra_t extra;
    if (size != sizeof(extra)) return false;
    memcpy(&extra, data, sizeof(extra));
    if (extra.campaign_level < 0 || extra.campaign_level > W2_CAMPAIGN_LEVELS) return false;
    W2_SetCombatState(extra.dice);
    w2_mission_t *mission = level.mission;
    if (!mission) return false;
    mission->campaign = (w2_campaign_t){extra.campaign_level, extra.campaign_orc != 0};
    W2_VictoryReset();
    launch_resources = extra.launch_resources;
    return true;
}

/* ── multiplayer: the engine's screens in Warcraft II dress ───────────────── */

static int net_map_count(void) { return custom_count(); }
static const char *net_map_path(int index) {
    return index >= 0 && index < entry_count ? entries[index].file : "";
}
static const char *net_map_title(int index) {
    return index >= 0 && index < entry_count ? entries[index].label : "";
}
static void net_style_button(menuitem_t *item) {
    bind_button(item, &art.widgets[1]);
    if (item->mark_len == 0) item->hotkey_ink = 0;
}
/* The network screens' lists take the native list's rows, rims and bar (0x5ae28). */
static void net_style_list(menuitem_t *list, menuitem_t *bar) {
    irect_t control = list->rect;
    native_rows(list);
    int rows = (control.h - 3) / list->row_height;
    control.h = rows * list->row_height + 4;
    list->rect = (irect_t){control.x + 2, control.y + 2, control.w - 4, rows * list->row_height};
    list->font = small();
    bar->sheet = &art.widgets[1];
    bar->flags = W2_ITEM_FLAGS(7, 0x0018) | W2_ITEM_ARROWS;
    bar->rect = (irect_t){control.x + control.w, control.y, art.widgets[1].cells[28].rect.w, control.h};
    bar->thumb = (menulook_t){.cell = 40, .part = art.widgets[1].cells[40].rect};
}
static void net_style_label(menuitem_t *item) {
    item->font = large();
}
static void net_back(app_t *app) {
    (void)app;
    open_viewgame();
}

static void net_word(int id, int entry, int index) {
    w2_text_t text;
    if (w2_label(entry, index, &text)) {
        snprintf(net_text[id], sizeof(net_text[id]), "%s", text.text);
    }
}

static void open_engine_net(int first) {
    memset(net_text, 0, sizeof(net_text));
    net_word(NETTEXT_TITLE, STR_MAIN_MENU, 2);
    net_word(NETTEXT_CREATE, 38, 3);
    net_word(NETTEXT_JOIN, 38, 1);
    net_word(NETTEXT_PREVIOUS, STR_MULTIPLAYER, 2);
    net_word(NETTEXT_START, STR_CUSTOM_MENU, 2);
    net_word(NETTEXT_CANCEL, STR_PICK, 2);
    net_word(NETTEXT_SCENARIO, STR_PICK, 9);
    net_word(NETTEXT_PLAYERS, STR_PICK, 7);
    net_word(NETTEXT_CONNECT, STR_MULTIPLAYER, 1);
    net_word(NETTEXT_SESSIONS, 38, 5);
    netui_t ui = {
        .background = &art.dimmed, .font = large(), .panel = &art.panel[1][W2_PANEL_SCENARIO],
        .style_button = net_style_button, .style_label = net_style_label, .style_list = net_style_list,
        .back = net_back, .drawitem = w2_draw_item, .first = first,
        .map_count = net_map_count, .map_path = net_map_path, .map_title = net_map_title,
        .max_players = 8,
    };
    for (int i = 0; i < NETTEXT_COUNT; ++i) ui.text[i] = net_text[i][0] ? net_text[i] : NULL;
    M_NetOpen(front_app, &ui);
}

/* STRDAT 60 names the three connection methods. Rows are copied out of L()'s
 * ring because the list draws every row at once. Descriptions are 60/5..7. */
static const char *method_row(const menuitem_t *item, int row) {
    (void)item;
    return row >= 0 && row < 3 ? method_name[row] : "";
}

static void connection_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_link(int resource);

static void open_connection(void) {
    button_race = 1;
    if (!native_scene(&net_screen, 3042, backdrop(), NULL, escape_to_title)) return;
    for (int i = 0; i < 3; ++i) {
        w2_text_t text;
        method_name[i][0] = '\0';
        if (w2_label(STR_CONNECTION, 2 + i, &text))
            snprintf(method_name[i], sizeof(method_name[i]), "%s", text.text);
    }
    menuitem_t *list = M_MenuFind(&net_screen.menu, 1);
    if (list) {
        native_rows(list);
        list->row = method_row;
        list->value = -1;
        M_MenuSetRows(list, 3);
    }
    menuitem_t *desc = M_MenuFind(&net_screen.menu, 2);
    if (desc) { desc->text[0] = '\0'; desc->prose = NULL; }
    wire(&net_screen, connection_action);
    show(&net_screen.menu);
}

static void connection_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (item->id == 1 && action == MA_CHANGE) {
        menuitem_t *desc = M_MenuFind(&net_screen.menu, 2);
        if (desc && item->value >= 0 && item->value < 3) {
            w2_label_copy(STR_CONNECTION, 5 + item->value, method_desc, sizeof(method_desc));
            desc->prose = method_desc;
            desc->text[0] = '\0';
        }
        return;
    }
    if (action != MA_ACTIVATE) return;
    if (item->id == -3) { escape_to_title(menu); return; }
    if (item->id != -2) return;
    menuitem_t *list = M_MenuFind(&net_screen.menu, 1);
    int row = list ? list->value : -1;
    if (row == 0) open_link(3073);
    else if (row == 1) open_link(3072);
    else if (row == 2) open_viewgame();
}

static void take_modem_config(void) {
    menu_t *m = &dialog_screen.menu;
    for (int id = 1; id <= 3; ++id) {
        menuitem_t *field = M_MenuFind(m, id);
        if (field && field->kind == MI_TEXTFIELD)
            snprintf(modem_line[id - 1], sizeof(modem_line[id - 1]), "%s", field->text);
    }
    menuitem_t *tone = M_MenuFind(m, 4);
    if (tone) modem_tone = tone->value ? 1 : 0;
}

static void apply_modem_config(void) {
    menu_t *m = &dialog_screen.menu;
    for (int id = 1; id <= 3; ++id) {
        menuitem_t *field = M_MenuFind(m, id);
        if (field && field->kind == MI_TEXTFIELD)
            snprintf(field->text, sizeof(field->text), "%s", modem_line[id - 1]);
    }
    set_radio(m, 1, 4, 5, modem_tone != 0);
}

static void link_action(menu_t *menu, menuitem_t *item, menuaction_t action);

static void link_escape(menu_t *menu) {
    (void)menu;
    if (net_dialog == 3074) open_link(3073);
    else open_connection();
}

static void link_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (action != MA_ACTIVATE || item->kind != MI_BUTTON) return;
    if (item->id == 7 && net_dialog == 3073) { open_link(3074); return; }
    if (item->id == -2 && net_dialog == 3074) { take_modem_config(); open_link(3073); return; }
    if (item->id == -2 && (net_dialog == 3072 || net_dialog == 3073)) {
        M_StartMessage("This connection is not available.");
        return;
    }
    if (item->id == -3) link_escape(&dialog_screen.menu);
}

/* Direct link 3072 and modem 3073 are display records. COM, baud and IRQ
 * lists are not in STRDAT, so the dropdowns stay empty. 3074 stores its
 * fields and returns to the modem page. */
static void open_link(int resource) {
    net_dialog = resource;
    button_race = 1;
    if (!native_scene(&dialog_screen, resource, backdrop(), NULL, link_escape)) return;
    if (resource == 3074) apply_modem_config();
    wire(&dialog_screen, link_action);
    show(&dialog_screen.menu);
}

static void view_escape(menu_t *menu) { (void)menu; open_connection(); }

static void view_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (action != MA_ACTIVATE) return;
    if (item->id == 3) open_engine_net(1);
    else if (item->id == 1) open_engine_net(2);
    else if (item->id == 4) open_connection();
}

static void open_viewgame(void) {
    button_race = 1;
    if (!native_scene(&net_screen, 3075, backdrop(), NULL, view_escape)) return;
    menuitem_t *list = M_MenuFind(&net_screen.menu, 6);
    if (list) { native_rows(list); list->rows = 0; list->value = -1; }
    wire(&net_screen, view_action);
    show(&net_screen.menu);
}

static void open_multiplayer(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu; (void)item;
    if (action == MA_ACTIVATE) open_connection();
}

static void accept_pick(void) {
    menuitem_t *list = M_MenuFind(&pick_screen.menu, 1);
    int row = list ? list->value : -1;
    if (row < 0 || row >= entry_count) return;
    chosen.archive = entries[row].archive;
    snprintf(chosen.file, sizeof(chosen.file), "%s", entries[row].file);
    snprintf(chosen.name, sizeof(chosen.name), "%s", entries[row].label);
    open_setup();
}

/* Start Game: a built-in scenario is extracted first, as campaign maps are. */
static void start_chosen(void) {
    char path[1200];
    if (chosen.archive) {
        char name[64];
        snprintf(name, sizeof(name), "scenario-%d.pud", chosen.archive);
        if (!w2_extract_map(data_root, chosen.archive, name, path, sizeof(path))) {
            M_StartMessage("The scenario could not be read.");
            return;
        }
    } else snprintf(path, sizeof(path), "%s", chosen.file);
    W2_SetCampaign(0, false);
    start_level(path, true);
}

static void setup_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (item->kind == MI_DROPDOWN) {
        if (action == MA_CHANGE && item->id == 4) resources_mode = item->value;
        return;
    }
    if (action != MA_ACTIVATE) return;
    if (item->id == 2) start_chosen();
    else if (item->id == 3) open_scenario();
    else if (item->id == 1 && menu->escape) menu->escape(menu);
}

static void single_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    if (item->id == 1) open_campaign();
    else if (item->id == 2) open_file(false, &single_screen.menu);
    else if (item->id == 3) open_setup();
    else if (item->id == -3 && menu->escape) menu->escape(menu);
}

static void campaign_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    if (item->id == 1 || item->id == 2) start_campaign(item->id == 1);
    else if (item->id == -3 && menu->escape) menu->escape(menu);
}

static void pick_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE && action != MA_CHANGE) return;
    switch (item->id) {
    case 1:
        if (action == MA_ACTIVATE) accept_pick();
        else pick_changed(&pick_screen, item->value);
        break;
    case 2: scenario_type = item->value; open_scenario(); break;
    case 3: size_filter = item->value; open_scenario(); break;
    case -2: accept_pick(); break;
    case -3: if (menu->escape) menu->escape(menu); break;
    default: break;
    }
}

static void file_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (item->id == 1 && action == MA_CHANGE) {
        if (saving && name_item >= 0 && item->value >= 0 && item->value < save_count)
            snprintf(file_screen.items[name_item].text, sizeof(file_screen.items[0].text), "%s",
                     saves[item->value].info.name);
        return;
    }
    if (action != MA_ACTIVATE) return;
    if (item->id == -2 || item->id == 2 || (item->id == 1 && item->kind == MI_LIST)) file_activate();
    else if (item->id == 3) {
        int row = file_selection();
        if (saving && row >= 0 && row < save_count) {
            remove(saves[row].path);
            open_file(true, file_return);
        }
    } else if (item->id == -3) file_escape(&file_screen.menu);
}

static void single_player(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu; (void)item;
    if (action == MA_ACTIVATE) open_single();
}

bool G_InitMenus(app_t *app, const char *root) {
    front_app = app;
    snprintf(data_root, sizeof(data_root), "%s", root && root[0] ? root : g_game_default_root);
    memset(&chosen, 0, sizeof(chosen));
    scenario_type = size_filter = 0;
    resources_mode = launch_resources = 0;
    W2_SetCampaign(0, false);
    if (!w2_load_menu_art(root, &art))
        fprintf(stderr, "warcraft-2: menu art was not loaded\n");
    w2_dialog_art(&art);
    if (!w2_strings_load(root))
        fprintf(stderr, "warcraft-2: dialog text was not loaded; using built-in labels\n");
    return true;
}

menu_t *G_ControlPanel(app_t *app, bool inlevel) {
    front_app = app;
    button_race = inlevel ? side() : 1;
    screen_t *screen = inlevel ? &game_screen : &title_screen;
    if (!native_scene(screen, inlevel ? 3044 : 3041, inlevel ? NULL : &art.title,
                      inlevel ? &art.panel[button_race][W2_PANEL_GAME] : NULL,
                      inlevel ? game_escape : NULL)) return NULL;
    if (!inlevel) {
        const menuroutine_t routines[] = {NULL, single_player, open_multiplayer, menu_note, show_credits};
        const SDL_Keycode keys[] = {0, SDLK_s, SDLK_m, SDLK_r, SDLK_h};
        for (int i = 1; i < screen->menu.numitems; ++i) {
            menuitem_t *item = &screen->items[i];
            if (item->id != -3 && (item->id < 1 || item->id > 4)) continue;
            item->routine = item->id == -3 ? M_MenuQuitGame : routines[item->id];
            item->hotkey = item->id == -3 ? SDLK_x : keys[item->id];
            if (item->id == 3) item->userdata = "The introduction is not played in this build.";
        }
        return M_SimpleControlPanel(&screen->menu);
    }
    const menuroutine_t routines[] = {NULL, open_save, open_load, open_options, open_help_menu,
                                     open_objectives, open_end_menu};
    const SDL_Keycode keys[] = {0, SDLK_F11, SDLK_F12, SDLK_F5, SDLK_F1, SDLK_o, SDLK_e};
    for (int i = 1; i < screen->menu.numitems; ++i) {
        menuitem_t *item = &screen->items[i];
        if (item->kind != MI_BUTTON) continue;
        if (item->id != -3 && (item->id < 1 || item->id > 6)) continue;
        item->routine = item->id == -3 ? return_to_game : routines[item->id];
        item->hotkey = item->id == -3 ? SDLK_ESCAPE : keys[item->id];
    }
    return &screen->menu;
}

void G_ShutdownMenus(void) {
    w2_free_menu_art(&art);
    w2_strings_free();
}
