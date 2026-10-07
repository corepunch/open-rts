#define _DEFAULT_SOURCE
#include "engine.h"
#include "warcraft-2.h"
#include "w2_local.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* Native dialog geometry comes from REZDAT linked records; assets come from
 * its GFUs and bitmaps. Callback dispatch and dynamic contents belong here.
 *
 * Single-player campaign entry follows Blizzard's manual: New Campaign,
 * race selection, then mission one. Wargus's mission selector is not retail
 * behavior. Other screens still need retail layout verification.
 * Screens are built when opened, so each starts from fresh state.
 * Rendering flags and complete retail screen flow still need static tracing.
 * Other popups use the dimmed title (REZDAT 15). */

static void menu_note(menu_t *menu, menuitem_t *item, menuaction_t action);
static void return_to_game(menu_t *menu, menuitem_t *item, menuaction_t action);
static void end_scenario(menu_t *menu, menuitem_t *item, menuaction_t action);
static void single_player(menu_t *menu, menuitem_t *item, menuaction_t action);
static void single_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void result_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void show_credits(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_options(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_text(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_save(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_load(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_multiplayer(menu_t *menu, menuitem_t *item, menuaction_t action);

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

#define YELLOW 0xffffe84au
#define WHITE 0xffffffffu
enum { SCREEN_ITEMS = 80, MAX_SCENARIOS = 128, MAX_SAVES = 64 };

typedef struct { menu_t menu; menuitem_t items[SCREEN_ITEMS]; } screen_t;

enum {
    A_STANDARD = 1, A_LOAD, A_PREVIOUS,
    A_SELECT, A_START, A_CANCEL, A_RESOURCES,
    A_CAMPAIGN, A_LIST,
    A_LOWER, A_RAISE, A_SPEED, A_OPTIONS_OK,
    A_SAVE_OK, A_DELETE, A_FILE_CANCEL, A_NAME
};
typedef struct { const char *text; int at, len; } label_t;
typedef struct { char path[1200]; saveinfo_t info; } saveentry_t;

static screen_t title_screen, game_screen, single_screen, campaign_screen, setup_screen, pick_screen, credits_screen, options_screen,
                result_screen, stats_screen, file_screen;
static char data_root[1024];
static app_t *front_app;
static char scenario[1200];
static int resources_mode, launch_resources;
static char launch_path[1200];
static struct { char file[1200]; char label[160]; w2_pud_info_t info; bool directory; int archive; } entries[MAX_SCENARIOS];
static int entry_count;
static char pick_directory[1024];
static int scenario_type = 1, size_filter, player_filter;
static menu_t *options_return, *file_return;
static int button_race = 1; /* widget art of the screen being built; the front end is orc */
static int volume_item, speed_item;
static saveentry_t saves[MAX_SAVES];
static int save_count, name_item;
static bool saving;
static char credits_text[3072], help_text[4096], objective_text[512];

static const struct { const char *name; int percent; } speeds[] = {
    { "50%", 50 }, { "75%", 75 }, { "100%", 100 }, { "150%", 150 }, { "200%", 200 },
};
enum { NUM_SPEEDS = sizeof(speeds) / sizeof(speeds[0]), NUM_RESOURCES = 4 };
static char net_text[NETTEXT_COUNT][160];

/* Text comes from STRDAT when the data has it, and from the fallback if not. */
static label_t L(int entry, int index, const char *fallback) {
    static w2_text_t ring[16];
    static int next;
    w2_text_t *text = &ring[next++ & 15];
    if (w2_label(entry, index, text)) return (label_t){text->text, text->mark_at, text->mark_len};
    return (label_t){fallback, 0, 0};
}
#define LIT(s) ((label_t){(s), 0, 0})

static void apply_label(menuitem_t *item, label_t text) {
    snprintf(item->text, sizeof(item->text), "%s", text.text);
    item->mark_at = text.at;
    item->mark_len = text.len;
}

static const bitmapfont_t *large(void) { return art.font.sprite.numlumps ? &art.font : NULL; }
static const bitmapfont_t *small(void) {
    return art.small_font.sprite.numlumps ? &art.small_font : large();
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
        item->font = large();
        for (int state = 0; state < MS_STATES; ++state) item->look[state].palette = 1;
        if (item->kind == MI_BUTTON) bind_button(item, &art.widgets[button_race]);
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

static menuitem_t *add_label(screen_t *screen, irect_t rect, label_t text, int align,
                             bool small_font) {
    menuitem_t *item = screen_add(screen, MI_STATIC, rect);
    item->font = small_font ? small() : large();
    item->ink = 0;
    for (int i = 0; i < MS_STATES; ++i) item->look[i].palette = 1;
    item->align = align;
    snprintf(item->text, sizeof(item->text), "%s", text.text);
    return item;
}

/* The panel's own size centres it on the 640x480 screen. */
static irect_t add_panel(screen_t *screen, const spritesheet_t *sheet) {
    isize2_t size = sheet->numlumps ? (isize2_t){sheet->cells[0].rect.w, sheet->cells[0].rect.h}
                                    : (isize2_t){0, 0};
    irect_t where = {(640 - size.w) / 2, (480 - size.h) / 2, size.w, size.h};
    menuitem_t *item = screen_add(screen, MI_STATIC, where);
    item->sheet = sheet->numlumps ? sheet : NULL;
    item->opaque = true;
    for (int state = 0; state < MS_STATES; ++state) item->look[state].cell = item->sheet ? 0 : -1;
    return where;
}

static void front_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void campaign_action(menu_t *menu, menuitem_t *item, menuaction_t action);
static void pick_action(menu_t *menu, menuitem_t *item, menuaction_t action);

static menuitem_t *add_button(screen_t *screen, irect_t rect, label_t text, SDL_Keycode key,
                              int id) {
    menuitem_t *item = screen_add(screen, MI_BUTTON, rect);
    item->hotkey = key;
    item->id = id;
    item->routine = front_action;
    apply_label(item, text);
    bind_button(item, &art.widgets[button_race]);
    return item;
}

static void set_text(screen_t *screen, int index, const char *format, ...) __attribute__((format(printf, 3, 4)));
static void set_text(screen_t *screen, int index, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(screen->items[index].text, sizeof(screen->items[index].text), format, args);
    va_end(args);
}

static int last_item(const screen_t *screen) { return screen->menu.numitems - 1; }

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

/* Scenario info line for the setup and picker screens. */
static void describe(const w2_pud_info_t *info, char *out, size_t size) {
    static const int era_strings[4] = {21, 22, 23, 0};
    static const char *const era_fallback[4] = {"Forest", "Winter", "Wasteland", "Swamp"};
    int era = info->era & 3;
    label_t name = L(STR_SETUP_VALUES, era_strings[era], era_fallback[era]);
    snprintf(out, size, "%s%s%d x %d, %s", info->description, info->description[0] ? " - " : "",
             info->width, info->height, name.text);
}

static label_t owner_type(int owner) {
    switch (owner) {
    case 4: return L(STR_SETUP_VALUES, 6, "Computer");
    case 5: return L(STR_MESSAGES, 31, "You");
    default: return LIT("");
    }
}

static label_t resource_name(int mode) {
    static const char *const fallback[NUM_RESOURCES] = {"Map Default", "Low", "Medium", "High"};
    return L(STR_SETUP_VALUES, 10 + mode, fallback[mode]);
}

/* Custom game setup: the map decides the sides; the starting resources are the one
 * setting the player owns (retail's Resources row). */
static void open_setup(void) {
    if (!scenario[0]) snprintf(scenario, sizeof(scenario), "%s", g_game_default_map);
    char path[1200], line[160];
    w2_pud_info_t info;
    if (scenario[0] == '/') snprintf(path, sizeof(path), "%s", scenario);
    else M_PathJoin(path, sizeof(path), data_root, scenario);
    bool known = w2_pud_info(path, &info);
    screen_begin(&setup_screen, backdrop(), escape_to_single);
    button_race = 1;
    screen_t *s = &setup_screen;
    add_label(s, (irect_t){330, 34, 310, 24}, L(STR_CUSTOM_MENU, 4, "Custom Game"), MALIGN_CENTER,
              false);
    int row = 0;
    for (int i = 0; known && i < 8; ++i) {
        label_t type = owner_type(info.owners[i]);
        if (!type.text[0]) continue;
        int y = 110 + row++ * 22;
        snprintf(line, sizeof(line), "%d", i + 1);
        add_label(s, (irect_t){344, y, 40, 16}, LIT(line), MALIGN_LEFT, true);
        add_label(s, (irect_t){390, y, 140, 16}, type, MALIGN_LEFT, true);
        label_t race = info.sides[i] == 0 ? L(STR_SETUP_VALUES, 19, "Human") :
                       info.sides[i] == 1 ? L(STR_SETUP_VALUES, 20, "Orc") : LIT("-");
        add_label(s, (irect_t){540, y, 90, 16}, race, MALIGN_LEFT, true);
    }
    add_label(s, (irect_t){16, 224, 224, 16}, L(STR_SETUP, 7, "Resources:"), MALIGN_LEFT, true);
    add_button(s, (irect_t){16, 244, 224, 28}, resource_name(resources_mode), SDLK_r, A_RESOURCES);
    s->items[last_item(s)].hotkey_ink = 0;
    add_label(s, (irect_t){16, 360, 120, 20}, L(STR_SETUP, 11, "Scenario:"), MALIGN_LEFT, false);
    snprintf(line, sizeof(line), "%.*s", (int)strcspn(scenario, "."), scenario);
    add_label(s, (irect_t){16, 384, 370, 20}, LIT(line), MALIGN_LEFT, false);
    if (known) describe(&info, line, sizeof(line)); else snprintf(line, sizeof(line), "?");
    add_label(s, (irect_t){16, 408, 370, 40}, LIT(line), MALIGN_LEFT, true);
    add_button(s, (irect_t){400, 360, 224, 28}, L(STR_PICK, 9, "Select Scenario"), SDLK_e, A_SELECT);
    menuitem_t *start = add_button(s, (irect_t){400, 396, 224, 28}, L(STR_CUSTOM_MENU, 2, "Start Game"),
                                   SDLK_s, A_START);
    start->enabled = known;
    add_button(s, (irect_t){400, 432, 224, 28}, L(STR_PICK, 2, "Cancel"), SDLK_c, A_CANCEL);
    show(&s->menu);
}

static int compare_entries(const void *a, const void *b) {
    return strcasecmp(((const char *)a), ((const char *)b)); /* file is the first member */
}

static int scenario_players(const w2_pud_info_t *info) {
    int players = 0;
    for (int i = 0; i < 8; ++i) players += info->owners[i] == 4 || info->owners[i] == 5;
    return players;
}

static void scan_scenarios(const char *directory, int size, bool folders) {
    entry_count = 0;
    if (folders && directory[0]) {
        memset(&entries[entry_count], 0, sizeof(entries[0]));
        const char *slash = strrchr(directory, '/');
        snprintf(entries[entry_count].file, sizeof(entries[0].file), "%.*s",
                 slash ? (int)(slash - directory) : 0, directory);
        snprintf(entries[entry_count].label, sizeof(entries[0].label), "..");
        entries[entry_count++].directory = true;
    }
    char base[1200];
    M_PathJoin(base, sizeof(base), data_root, directory);
    DIR *dir = opendir(base);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir)) && entry_count < MAX_SCENARIOS) {
        size_t len = strlen(ent->d_name);
        if (ent->d_name[0] == '.' || len >= sizeof(entries[0].file)) continue;
        char path[1200];
        M_PathJoin(path, sizeof(path), base, ent->d_name);
        struct stat st;
        if (stat(path, &st)) continue;
        bool folder = S_ISDIR(st.st_mode);
        if (folder && !folders) continue;
        if (!folder && (len < 5 || strcasecmp(ent->d_name + len - 4, ".pud"))) continue;
        memset(&entries[entry_count], 0, sizeof(entries[0]));
        if (!folder && !w2_pud_info(path, &entries[entry_count].info)) continue;
        if (!folder && size && (entries[entry_count].info.width != size ||
                               entries[entry_count].info.height != size)) continue;
        if (!folder && player_filter && scenario_players(&entries[entry_count].info) != player_filter) continue;
        entries[entry_count].directory = folder;
        snprintf(entries[entry_count].file, sizeof(entries[0].file), "%s%s%s", directory,
                 directory[0] ? "/" : "", ent->d_name);
        snprintf(entries[entry_count].label, sizeof(entries[0].label), "%.*s%s",
                 (int)len - (folder ? 0 : 4), ent->d_name, folder ? "/" : "");
        ++entry_count;
    }
    closedir(dir);
    qsort(entries, (size_t)entry_count, sizeof(entries[0]), compare_entries);
}

static const char *pick_row(const menuitem_t *item, int row) {
    (void)item;
    return row >= 0 && row < entry_count ? entries[row].label : "";
}

/* STRDAT keeps each campaign map's name: pairs of human then orc after the objectives. */
static void level_title(int level, bool orc, char *out, size_t size) {
    label_t title = L(STR_LEVELS, 35 + 2 * (level - 1) + (orc ? 1 : 0), "");
    snprintf(out, size, "%s", title.text[0] ? title.text : M_va("%d", level));
}

static void pick_changed(screen_t *s, int row) {
    for (int id = 5; id <= 7; ++id) M_MenuFind(&s->menu, id)->text[0] = '\0';
    if (row >= 0 && row < entry_count) {
        if (!entries[row].directory) {
            int players = scenario_players(&entries[row].info);
            menuitem_t *dimensions = M_MenuFind(&s->menu, 5);
            snprintf(dimensions->text, sizeof(dimensions->text), "%d x %d", entries[row].info.width,
                     entries[row].info.height);
            menuitem_t *count = M_MenuFind(&s->menu, 6);
            snprintf(count->text, sizeof(count->text), "%d %s", players, players == 1 ? "player" : "players");
        }
    }
    M_MenuFind(&s->menu, -2)->enabled = row >= 0 && row < entry_count;
}

static const char *choice_row(const menuitem_t *item, int row) {
    static const char *const types[] = {"Built-in scenario", "Custom scenario"};
    static const char *const sizes[] = {"Any size", "32 x 32", "64 x 64", "96 x 96", "128 x 128"};
    static const char *const players[] = {"Any players", "1 player", "2 players", "3 players", "4 players",
                                         "5 players", "6 players", "7 players", "8 players"};
    if (item->id == 2) return types[row];
    if (item->id == 3) return sizes[row];
    return players[row];
}

static void native_rows(menuitem_t *item) {
    item->sheet = &art.widgets[1];
    item->font = large();
    item->opaque = item->stretch = true;
    item->ink = item->look[MS_PUSHED].ink = 0;
    item->row_height = 18;
    item->inset = (ivec2_t){4, 1};
    item->border = YELLOW;
    for (int i = 0; i < MS_STATES; ++i) {
        item->look[i].cell = 46;
        item->look[i].palette = i == MS_PUSHED ? 0 : 1;
    }
}

static void bind_choice(menuitem_t *item, int rows, int value) {
    native_rows(item);
    item->disabled_look = true;
    item->routine = pick_action;
    item->row = choice_row;
    item->rows = rows;
    item->value = value;
    item->popup_rows = 6;
    item->border = 0xff848484u;
    item->look[MS_FOCUS].palette = 0;
    item->look[MS_DISABLED].cell = 45;
    for (int i = 0; i < MS_STATES; ++i)
        item->arrow[i] = (menulook_t){.cell = i == MS_DISABLED ? 31 : i == MS_PUSHED ? 33 : 32,
                                     .part = {0, 0, 19, 20}};
}

static void open_scenario(void) {
    screen_t *s = &pick_screen;
    *s = setup_screen;
    s->menu.items = s->items;
    for (int i = 0; i < s->menu.numitems; ++i) {
        s->items[i].enabled = false;
        s->items[i].disabled_look = false;
        s->items[i].hotkey = 0;
        s->items[i].id = 0;
    }
    s->menu.held = s->menu.keyheld = s->menu.dropdown = NULL;
    s->menu.escape = escape_to_setup;
    button_race = 1;
    if (scenario_type) scan_scenarios(pick_directory, 32 * size_filter, true);
    else {
        entry_count = 0;
        char path[1200];
        M_PathJoin(path, sizeof(path), data_root, "DATA/MAINDAT.WAR");
        w2_archive_t archive;
        if (w2_archive_open(&archive, path)) {
            for (int i = 220; i < 248; ++i) {
                w2_blob_t blob = {0};
                w2_pud_info_t info;
                bool ok = w2_archive_extract(&archive, i, &blob) && w2_pud_info_bytes(blob.data, blob.size, &info);
                w2_blob_free(&blob);
                if (!ok || (size_filter && (info.width != 32 * size_filter || info.height != 32 * size_filter)) ||
                    (player_filter && scenario_players(&info) != player_filter)) continue;
                memset(&entries[entry_count], 0, sizeof(entries[0]));
                entries[entry_count].info = info;
                entries[entry_count].archive = i;
                snprintf(entries[entry_count].label, sizeof(entries[0].label), "%s", info.description);
                ++entry_count;
            }
            w2_archive_close(&archive);
        }
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
    bind_choice(type, 2, scenario_type);
    bind_choice(size, 5, size_filter);
    bind_choice(players, 9, player_filter);
    native_rows(list);
    list->routine = pick_action;
    list->row = pick_row;
    list->value = entry_count ? 0 : -1;
    for (int i = 0; i < entry_count; ++i)
        if (!strcasecmp(entries[i].file, scenario)) list->value = i;
    int list_index = (int)(list - s->items);
    list->first_row = list->value < 0 ? 0 : list->value;
    M_MenuSetRows(list, entry_count);
    /* Kind 7 is a hidden scrollbar placeholder in the resource. Construct
     * the composite from the native list rectangle and authored arrow size. */
    irect_t arrow_rect = art.widgets[1].cells[29].rect;
    bar->visible = bar->enabled = true;
    bar->rect = (irect_t){list->rect.x + list->rect.w, list->rect.y + arrow_rect.h,
                          arrow_rect.w, list->rect.h - 2 * arrow_rect.h};
    bar->sheet = &art.widgets[1];
    bar->link = list_index;
    bar->opaque = bar->stretch = true;
    bar->thumb = (menulook_t){.cell = 40, .part = {0, 0, 17, 17}};
    for (int i = 0; i < MS_STATES; ++i) bar->look[i].cell = 42;
    for (int i = 0; i < 2; ++i) {
        menuitem_t *arrow = screen_add(s, MI_BUTTON,
            (irect_t){bar->rect.x, list->rect.y + (list->rect.h - arrow_rect.h) * i,
                       arrow_rect.w, arrow_rect.h});
        bind_widget(arrow, &art.widgets[1], i ? 32 : 29, i ? 33 : 30);
        arrow->link = list_index;
        arrow->step = i ? 1 : -1;
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

static void start_campaign(bool orc) {
    char path[sizeof(launch_path)];
    if (!w2_extract_campaign_level(data_root, 1, orc, path, sizeof(path))) {
        M_StartMessage("The campaign level could not be read.");
        return;
    }
    W2_SetCampaign(1, orc);
    start_level(path, false);
}

/* Credits are the retail text. */
static void open_credits_screen(void) {
    w2_text_t text;
    if (w2_label(STR_CREDITS, 0, &text)) snprintf(credits_text, sizeof(credits_text), "%s", text.text);
    else snprintf(credits_text, sizeof(credits_text),
                  "open-rts\nAn open reimplementation of classic real-time strategy engines.\n\n"
                  "Warcraft II: Tides of Darkness was made by Blizzard Entertainment.");
    screen_t *s = &credits_screen;
    screen_begin(s, &art.dimmed, escape_to_title);
    button_race = 1;
    menuitem_t *prose = screen_add(s, MI_STATIC, (irect_t){80, 30, 480, 370});
    prose->font = large();
    prose->prose = credits_text;
    add_button(s, (irect_t){208, 420, 224, 28}, L(STR_CAMPAIGN, 3, "Previous Menu"), SDLK_ESCAPE,
               A_PREVIOUS);
    show(&s->menu);
}

static void show_credits(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu; (void)item;
    if (action == MA_ACTIVATE) open_credits_screen();
}

/* Options: sound volume and game speed, saved with the engine settings. */
static int speed_index(void) {
    int best = 2;
    for (int i = 0; i < NUM_SPEEDS; ++i)
        if (abs(speeds[i].percent - game_speed) < abs(speeds[best].percent - game_speed)) best = i;
    return best;
}

static void draw_volume(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    uint8_t color = V_NearestIndex(YELLOW);
    V_DrawRectOutline(rect, color);
    int filled = (rect.w - 4) * gamesettings.sound / 10;
    if (filled > 0) V_FillRect((irect_t){rect.x + 2, rect.y + 2, filled, rect.h - 4}, color);
}

static void options_escape(menu_t *menu) {
    (void)menu;
    D_SaveSettings(game_speed);
    show(options_return);
}

static void options_refresh(screen_t *s) {
    set_text(s, volume_item, "%s: %d%%", L(STR_SOUND, 9, "Sound Volume").text, gamesettings.sound * 10);
    set_text(s, speed_item, "%s: %s", L(STR_SPEED, 4, "Game Speed").text, speeds[speed_index()].name);
}

static void open_options_screen(menu_t *back, bool inlevel) {
    options_return = back;
    screen_t *s = &options_screen;
    screen_begin(s, inlevel ? NULL : &art.dimmed, options_escape);
    int race = button_race = inlevel ? side() : 1;
    irect_t box = add_panel(s, &art.panel[race][W2_PANEL_OPTIONS]);
    int x = box.x, y = box.y;
    add_label(s, (irect_t){x, y + 11, box.w, 20}, L(STR_OPTIONS, 5, "Game Options"), MALIGN_CENTER, false);
    add_label(s, (irect_t){x + 16, y + 44, 256, 20}, LIT(""), MALIGN_LEFT, false);
    volume_item = last_item(s);
    menuitem_t *bar = screen_add(s, MI_STATIC, (irect_t){x + 16, y + 68, 256, 18});
    bar->ownerdraw = draw_volume;
    add_button(s, (irect_t){x + 16, y + 96, 106, 28}, LIT("-"), SDLK_LEFT, A_LOWER);
    add_button(s, (irect_t){x + 166, y + 96, 106, 28}, LIT("+"), SDLK_RIGHT, A_RAISE);
    add_button(s, (irect_t){x + 32, y + 148, 224, 28}, LIT(""), SDLK_g, A_SPEED);
    speed_item = last_item(s);
    add_button(s, (irect_t){x + 91, y + 208, 106, 28}, L(STR_SOUND, 1, "OK"), SDLK_o, A_OPTIONS_OK);
    button_race = 1;
    options_refresh(s);
    show(&s->menu);
}

static void open_options(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action == MA_ACTIVATE) open_options_screen(menu, true);
}

/* Help (the retail key list) and Scenario Objectives: a panel over the level with text. */
static void open_text(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_ACTIVATE) return;
    bool help = item->id == 4;
    int race = side();
    screen_t *s = &options_screen;
    options_return = menu;
    screen_begin(s, NULL, options_escape);
    irect_t box = add_panel(s, &art.panel[race][W2_PANEL_OPTIONS]);
    button_race = race;
    const char *body;
    char title[160];
    if (help) {
        snprintf(title, sizeof(title), "%s", L(STR_HELP_MENU, 4, "Help").text);
        if (!w2_label_lines(STR_KEYS, 0, 41, help_text, sizeof(help_text)))
            snprintf(help_text, sizeof(help_text), "%s", (const char *)item->userdata);
        body = help_text;
    } else {
        snprintf(title, sizeof(title), "%s %s", L(STR_OBJECTIVES, 2, "Scenario").text,
                 L(STR_OBJECTIVES, 3, "Objectives").text);
        char level_name[160] = "";
        const w2_mission_t *mission = level.mission;
        if (mission && mission->campaign.number > 0)
            level_title(mission->campaign.number, mission->campaign.orc, level_name, sizeof(level_name));
        int objective = mission && mission->campaign.number ?
                        2 * (mission->campaign.number - 1) + mission->campaign.orc : 34;
        label_t goal = L(STR_LEVELS, objective, (const char *)item->userdata);
        snprintf(objective_text, sizeof(objective_text), "%s%s%s", level_name, level_name[0] ? "\n\n" : "",
                 goal.text + (goal.text[0] == '-'));
        body = objective_text;
    }
    add_label(s, (irect_t){box.x, box.y + 11, box.w, 20}, LIT(title), MALIGN_CENTER, false);
    menuitem_t *prose = screen_add(s, MI_STATIC, (irect_t){box.x + 16, box.y + 40, box.w - 32,
                                                         box.h - 96});
    prose->font = large();
    prose->prose = body;
    add_button(s, (irect_t){box.x + (box.w - 106) / 2, box.y + box.h - 44, 106, 28},
               L(STR_SOUND, 1, "OK"), SDLK_o, A_OPTIONS_OK);
    button_race = 1;
    show(&s->menu);
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
        char path[sizeof(launch_path)];
        if (!w2_extract_campaign_level(data_root, campaign.number + 1, campaign.orc, path, sizeof(path))) {
            M_StartMessage("The campaign level could not be read.");
            return;
        }
        W2_SetCampaign(campaign.number + 1, campaign.orc);
        start_level(path, false);
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
    if (file_return == &game_screen.menu) show(&game_screen.menu);
    else open_single();
}

static void open_file(bool save, menu_t *back) {
    saving = save;
    file_return = back;
    scan_saves();
    bool inlevel = back == &game_screen.menu;
    int race = button_race = inlevel ? side() : 1;
    screen_t *s = &file_screen;
    screen_begin(s, inlevel ? NULL : &art.dimmed, file_escape);
    irect_t box = add_panel(s, &art.panel[race][W2_PANEL_FILE]);
    int x = box.x, y = box.y;
    add_label(s, (irect_t){x, y + 11, box.w, 20},
              save ? L(STR_SAVE, 4, "Save Game") : L(STR_LOAD, 3, "Load Game"), MALIGN_CENTER, false);
    menuitem_t *list = screen_add(s, MI_LIST, (irect_t){x + 16, y + 40, 330, 120});
    list->id = A_LIST;
    list->routine = front_action;
    list->font = large();
    list->ink = YELLOW;
    list->look[MS_PUSHED].ink = WHITE;
    list->color = 0xff5a3c14u;
    list->row_height = 18;
    list->row = save_row;
    list->inset = (ivec2_t){4, 1};
    list->value = -1;
    int list_index = last_item(s);
    menuitem_t *bar = screen_add(s, MI_SCROLLBAR, (irect_t){x + 352, y + 40, 12, 120});
    bar->link = list_index;
    bar->color = 0xffb89040u;
    M_MenuSetRows(&s->items[list_index], save_count);
    if (save) {
        menuitem_t *field = screen_add(s, MI_TEXTFIELD, (irect_t){x + 16, y + 172, 348, 20});
        field->id = A_NAME;
        field->routine = front_action;
        field->font = large();
        field->ink = WHITE;
        field->fill = 0xff0c1216u;
        field->border = YELLOW;
        field->align = MALIGN_LEFT;
        field->inset = (ivec2_t){4, 1};
        field->maxchars = 32;
        name_item = last_item(s);
        s->menu.itemOn = name_item;
        add_button(s, (irect_t){x + 16, y + 212, 106, 28}, L(STR_SAVE, 1, "Save"), SDLK_RETURN, A_SAVE_OK);
        add_button(s, (irect_t){x + 139, y + 212, 106, 28}, L(STR_SAVE, 2, "Delete"), SDLK_DELETE, A_DELETE);
        add_button(s, (irect_t){x + 262, y + 212, 106, 28}, L(STR_SAVE, 3, "Cancel"), SDLK_ESCAPE,
                   A_FILE_CANCEL);
    } else {
        name_item = -1;
        s->menu.itemOn = list_index;
        add_button(s, (irect_t){x + 72, y + 212, 106, 28}, L(STR_LOAD, 1, "Load"), SDLK_RETURN, A_SAVE_OK);
        add_button(s, (irect_t){x + 206, y + 212, 106, 28}, L(STR_LOAD, 2, "Cancel"), SDLK_ESCAPE,
                   A_FILE_CANCEL);
    }
    button_race = 1;
    show(&s->menu);
}

static int file_selection(void) {
    for (int i = 0; i < file_screen.menu.numitems; ++i)
        if (file_screen.items[i].id == A_LIST) return file_screen.items[i].value;
    return -1;
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

static int net_map_count(void) {
    scan_scenarios("", 0, false);
    return entry_count;
}
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
static void net_style_label(menuitem_t *item) {
    item->font = large();
}
static void net_back(app_t *app) {
    (void)app;
    open_title();
}

static void net_word(int id, int entry, int index) {
    w2_text_t text;
    if (w2_label(entry, index, &text)) {
        snprintf(net_text[id], sizeof(net_text[id]), "%s", text.text);
    }
}

static void open_multiplayer(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu; (void)item;
    if (action != MA_ACTIVATE) return;
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
        .style_button = net_style_button, .style_label = net_style_label, .back = net_back,
        .map_count = net_map_count, .map_path = net_map_path, .map_title = net_map_title,
        .max_players = 8,
    };
    for (int i = 0; i < NETTEXT_COUNT; ++i) ui.text[i] = net_text[i][0] ? net_text[i] : NULL;
    M_NetOpen(front_app, &ui);
}

static void accept_pick(void) {
    menuitem_t *list = M_MenuFind(&pick_screen.menu, 1);
    int row = list ? list->value : -1;
    if (row < 0 || row >= entry_count) return;
    if (entries[row].directory) {
        snprintf(pick_directory, sizeof(pick_directory), "%s", entries[row].file);
        open_scenario();
        return;
    }
    if (entries[row].archive) {
        char name[64];
        snprintf(name, sizeof(name), "scenario-%d.pud", entries[row].archive);
        if (!w2_extract_map(data_root, entries[row].archive, name, scenario, sizeof(scenario))) {
            M_StartMessage("The scenario could not be read.");
            return;
        }
    } else snprintf(scenario, sizeof(scenario), "%s", entries[row].file);
    open_setup();
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
    case 4: player_filter = item->value; open_scenario(); break;
    case -2: accept_pick(); break;
    case -3: if (menu->escape) menu->escape(menu); break;
    default: break;
    }
}

static void front_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (action != MA_ACTIVATE && !((item->kind == MI_LIST || item->kind == MI_DROPDOWN) && action == MA_CHANGE)) return;
    switch (item->id) {
    case A_CAMPAIGN: open_campaign(); break;
    case A_STANDARD: open_setup(); break;
    case A_LOAD: open_file(false, &single_screen.menu); break;
    case A_PREVIOUS: {
        menu_t *current = currentmenu;
        if (current && current->escape) current->escape(current);
        break;
    }
    case A_SELECT: open_scenario(); break;
    case A_RESOURCES:
        resources_mode = (resources_mode + 1) % NUM_RESOURCES;
        apply_label(item, resource_name(resources_mode));
        break;
    case A_START: {
        char path[1200];
        snprintf(path, sizeof(path), "%s", scenario);
        W2_SetCampaign(0, false);
        start_level(path, true);
        break;
    }
    case A_CANCEL: open_single(); break;
    case A_LIST:
        if (saving && item->value >= 0 && item->value < save_count)
            snprintf(file_screen.items[name_item].text, sizeof(file_screen.items[0].text), "%s",
                     saves[item->value].info.name);
        break;
    case A_SAVE_OK: file_activate(); break;
    case A_NAME: file_activate(); break;
    case A_DELETE: {
        int row = file_selection();
        if (saving && row >= 0 && row < save_count) {
            remove(saves[row].path);
            open_file(true, file_return);
        }
        break;
    }
    case A_FILE_CANCEL: file_escape(&file_screen.menu); break;
    case A_LOWER:
    case A_RAISE:
        gamesettings.sound += item->id == A_RAISE ? 1 : -1;
        if (gamesettings.sound < 0) gamesettings.sound = 0;
        if (gamesettings.sound > 10) gamesettings.sound = 10;
        S_SetVolume(gamesettings.sound * 10);
        options_refresh(&options_screen);
        break;
    case A_SPEED:
        D_SetGameSpeed(speeds[(speed_index() + 1) % NUM_SPEEDS].percent);
        options_refresh(&options_screen);
        break;
    case A_OPTIONS_OK: options_escape(&options_screen.menu); break;
    default: break;
    }
}

static void single_player(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu; (void)item;
    if (action == MA_ACTIVATE) open_single();
}

bool G_InitMenus(app_t *app, const char *root) {
    front_app = app;
    snprintf(data_root, sizeof(data_root), "%s", root && root[0] ? root : g_game_default_root);
    scenario[0] = '\0';
    pick_directory[0] = '\0';
    scenario_type = 1;
    size_filter = player_filter = 0;
    resources_mode = launch_resources = 0;
    W2_SetCampaign(0, false);
    if (!w2_load_menu_art(root, &art))
        fprintf(stderr, "warcraft-2: menu art was not loaded\n");
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
    const menuroutine_t routines[] = {NULL, open_save, open_load, open_options, open_text,
                                     open_text, end_scenario};
    const SDL_Keycode keys[] = {0, SDLK_F11, SDLK_F12, SDLK_F5, SDLK_F1, SDLK_o, SDLK_e};
    for (int i = 1; i < screen->menu.numitems; ++i) {
        menuitem_t *item = &screen->items[i];
        if (item->kind != MI_BUTTON) continue;
        if (item->id != -3 && (item->id < 1 || item->id > 6)) continue;
        item->routine = item->id == -3 ? return_to_game : routines[item->id];
        item->hotkey = item->id == -3 ? SDLK_ESCAPE : keys[item->id];
        if (item->id == 4) item->userdata = "F10 opens this menu. WASD pans.";
        if (item->id == 5) item->userdata = "Defeat the opposing side.";
    }
    return &screen->menu;
}

void G_ShutdownMenus(void) {
    w2_free_menu_art(&art);
    w2_strings_free();
}
