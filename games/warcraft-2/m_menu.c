#define _DEFAULT_SOURCE
#include "engine.h"
#include "w2_local.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The title is REZDAT entry 13 with the retail five-button column: 224x28
 * buttons at x 208, rows from y 240 every 36 pixels, below the top-left logo.
 * The in-level game menu uses panel 1 on the left, not the centred Wargus box.
 *
 * Every other screen follows Wargus scripts/guichan.lua and menus/options.lua:
 * single player, game setup, scenario and campaign pickers, credits and the
 * options box. Screens are built when opened, so each starts from fresh state.
 * Popups sit on the dimmed title (REZDAT 15) and use the REZDAT panels. */

enum {
    TITLE_SINGLE, TITLE_MULTI, TITLE_INTRO, TITLE_CREDITS, TITLE_EXIT, TITLE_COUNT
};
enum {
    GAME_PANEL, GAME_LABEL, GAME_SAVE, GAME_LOAD, GAME_OPTIONS, GAME_HELP,
    GAME_OBJECTIVES, GAME_END, GAME_RETURN, GAME_COUNT
};

static void menu_note(menu_t *menu, menuitem_t *item, menuaction_t action);
static void return_to_game(menu_t *menu, menuitem_t *item, menuaction_t action);
static void end_scenario(menu_t *menu, menuitem_t *item, menuaction_t action);
static void single_player(menu_t *menu, menuitem_t *item, menuaction_t action);
static void show_credits(menu_t *menu, menuitem_t *item, menuaction_t action);
static void open_options(menu_t *menu, menuitem_t *item, menuaction_t action);

static w2_menu_art_t art;
#define TITLE_BUTTON(row, label, key, action, note) { \
        .kind = MI_BUTTON, .visible = true, .enabled = true, .hotkey = key, \
        .rect = { 208, 240 + 36 * (row), 224, 28 }, .text = label, \
        .align = MALIGN_CENTER, .ink = 0xffffe84au, .routine = action, .userdata = note }
static menuitem_t title_items[TITLE_COUNT] = {
    [TITLE_SINGLE] = TITLE_BUTTON(0, "Single Player Game", SDLK_s, single_player, NULL),
    [TITLE_MULTI] = TITLE_BUTTON(1, "Multi Player Game", SDLK_m, menu_note,
                                 "Start a network game with --host or --join."),
    [TITLE_INTRO] = TITLE_BUTTON(2, "Replay Introduction", SDLK_r, menu_note,
                                 "The introduction is not played in this build."),
    [TITLE_CREDITS] = TITLE_BUTTON(3, "Show Credits", SDLK_h, show_credits, NULL),
    [TITLE_EXIT] = TITLE_BUTTON(4, "Exit Program", SDLK_x, M_MenuQuitGame, NULL),
};
#undef TITLE_BUTTON
static menuitem_t game_items[GAME_COUNT] = {
    [GAME_PANEL] = { .visible = true, .opaque = true, .rect = { 0, 96, 256, 288 } },
    [GAME_LABEL] = {
        .visible = true, .rect = { 0, 107, 256, 20 }, .text = "Game Menu",
        .align = MALIGN_CENTER, .ink = 0xffffe84au,
    },
    [GAME_SAVE] = {
        .kind = MI_BUTTON, .visible = true, .enabled = true, .hotkey = SDLK_F11,
        .rect = { 16, 136, 106, 28 }, .text = "Save", .align = MALIGN_CENTER,
        .ink = 0xffffe84au, .routine = menu_note,
        .userdata = "Saved games are not stored in this build.",
    },
    [GAME_LOAD] = {
        .kind = MI_BUTTON, .visible = true, .enabled = true, .hotkey = SDLK_F12,
        .rect = { 134, 136, 106, 28 }, .text = "Load", .align = MALIGN_CENTER,
        .ink = 0xffffe84au, .routine = menu_note,
        .userdata = "Saved games are not stored in this build.",
    },
    [GAME_OPTIONS] = {
        .kind = MI_BUTTON, .visible = true, .enabled = true, .hotkey = SDLK_F5,
        .rect = { 16, 172, 224, 28 }, .text = "Options", .align = MALIGN_CENTER,
        .ink = 0xffffe84au, .routine = open_options,
    },
    [GAME_HELP] = {
        .kind = MI_BUTTON, .visible = true, .enabled = true, .hotkey = SDLK_F1,
        .rect = { 16, 208, 224, 28 }, .text = "Help", .align = MALIGN_CENTER,
        .ink = 0xffffe84au, .routine = menu_note,
        .userdata = "Left click selects. Drag selects your units. Right click clears the selection. A click away from your units orders them. F10 opens this menu. WASD pans.",
    },
    [GAME_OBJECTIVES] = {
        .kind = MI_BUTTON, .visible = true, .enabled = true, .hotkey = SDLK_o,
        .rect = { 16, 244, 224, 28 }, .text = "Scenario Objectives", .align = MALIGN_CENTER,
        .ink = 0xffffe84au, .routine = menu_note,
        .userdata = "Defeat the opposing side.",
    },
    [GAME_END] = {
        .kind = MI_BUTTON, .visible = true, .enabled = true, .hotkey = SDLK_e,
        .rect = { 16, 280, 224, 28 }, .text = "End Scenario", .align = MALIGN_CENTER,
        .ink = 0xffffe84au, .routine = end_scenario,
    },
    [GAME_RETURN] = {
        .kind = MI_BUTTON, .visible = true, .enabled = true, .hotkey = SDLK_ESCAPE,
        .rect = { 16, 344, 224, 28 }, .text = "Return to Game", .align = MALIGN_CENTER,
        .ink = 0xffffe84au, .routine = return_to_game,
    },
};
static menu_t title_menu = { .items = title_items, .numitems = TITLE_COUNT };
static menu_t game_menu = { .items = game_items, .numitems = GAME_COUNT };

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
    item->font = art.font.sprite.numlumps ? &art.font : NULL;
    /* Yellow text with a white hotkey letter; all white under the pointer,
     * and pressed text sits one pixel right and down. */
    item->ink = 0xffffe84au;
    item->hotkey_ink = 0xffffffffu;
    item->look[MS_FOCUS].ink = 0xffffffffu;
    item->look[MS_PUSHED].ink = 0xffffffffu;
    item->look[MS_PUSHED].shift = (ivec2_t){1, 1};
    item->align = MALIGN_CENTER;
    item->stretch = false;
    item->fill = item->sheet ? 0 : 0xff18242du;
    item->look[MS_NORMAL].cell = item->sheet ? normal : -1;
    item->look[MS_FOCUS].cell = item->sheet ? normal : -1;
    item->look[MS_PUSHED].cell = item->sheet ? pressed : -1;
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
enum { SCREEN_ITEMS = 80, MAX_SCENARIOS = 128 };

typedef struct { menu_t menu; menuitem_t items[SCREEN_ITEMS]; } screen_t;

enum {
    A_STANDARD = 1, A_CAMPAIGN, A_LOAD, A_PREVIOUS,
    A_SELECT, A_START, A_CANCEL, A_RESOURCES,
    A_HUMAN, A_ORC,
    A_LIST, A_PICK_OK, A_PICK_CANCEL,
    A_LOWER, A_RAISE, A_SPEED, A_OPTIONS_OK
};
typedef enum { PICK_SCENARIO, PICK_HUMAN, PICK_ORC } pickmode_t;

static screen_t single_screen, setup_screen, campaign_screen, pick_screen, credits_screen,
                options_screen;
static char data_root[1024];
static app_t *front_app;
static char scenario[64];
static int resources_mode;
static char launch_path[1200];
static pickmode_t pick_mode;
static struct { char file[64]; char label[48]; w2_pud_info_t info; } entries[MAX_SCENARIOS];
static int entry_count, pick_name_item, pick_detail_item;
static menu_t *options_return;
static int button_race = 1; /* widget art of the screen being built; the front end is orc */
static int volume_item, speed_item;

static const char *const resource_names[] = {
    "Map Default", "Low", "Medium", "High", "Quick Start"
};
static const char *const era_names[] = { "Forest", "Winter", "Wasteland", "Swamp" };
static const struct { const char *name; int percent; } speeds[] = {
    { "Slowest", 50 }, { "Slow", 75 }, { "Normal", 100 }, { "Fast", 150 }, { "Fastest", 200 },
};
enum { NUM_SPEEDS = sizeof(speeds) / sizeof(speeds[0]) };

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

static menuitem_t *add_label(screen_t *screen, irect_t rect, const char *text, int align,
                             bool small_font) {
    menuitem_t *item = screen_add(screen, MI_STATIC, rect);
    item->font = small_font ? small() : large();
    item->ink = YELLOW;
    item->align = align;
    snprintf(item->text, sizeof(item->text), "%s", text);
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

static menuitem_t *add_button(screen_t *screen, irect_t rect, const char *text, SDL_Keycode key,
                              int id) {
    menuitem_t *item = screen_add(screen, MI_BUTTON, rect);
    item->hotkey = key;
    item->id = id;
    item->routine = front_action;
    snprintf(item->text, sizeof(item->text), "%s", text);
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
static void open_campaign(void);
static void open_pick(pickmode_t mode);

static void escape_to_title(menu_t *menu) { (void)menu; open_title(); }
static void escape_to_single(menu_t *menu) { (void)menu; open_single(); }
static void escape_to_setup(menu_t *menu) { (void)menu; open_setup(); }
static void escape_to_campaign(menu_t *menu) { (void)menu; open_campaign(); }

static const spritesheet_t *backdrop(void) { return &art.title; }

/* Single Player: Wargus RunSinglePlayerTypeMenu. */
static void open_single(void) {
    screen_begin(&single_screen, backdrop(), escape_to_title);
    add_label(&single_screen, (irect_t){208, 206, 224, 28}, "Single Player", MALIGN_CENTER, false);
    add_button(&single_screen, (irect_t){208, 240, 224, 28}, "Standard Game", SDLK_s, A_STANDARD);
    add_button(&single_screen, (irect_t){208, 276, 224, 28}, "Campaign Game", SDLK_c, A_CAMPAIGN);
    add_button(&single_screen, (irect_t){208, 312, 224, 28}, "Load Game", SDLK_l, A_LOAD);
    add_button(&single_screen, (irect_t){208, 348, 224, 28}, "Previous Menu (Esc)", SDLK_ESCAPE,
               A_PREVIOUS);
    show(&single_screen.menu);
}

static void open_campaign(void) {
    screen_begin(&campaign_screen, backdrop(), escape_to_single);
    add_label(&campaign_screen, (irect_t){208, 206, 224, 28}, "Campaign Game", MALIGN_CENTER, false);
    add_button(&campaign_screen, (irect_t){208, 240, 224, 28}, "Human Campaign", SDLK_h, A_HUMAN);
    add_button(&campaign_screen, (irect_t){208, 276, 224, 28}, "Orc Campaign", SDLK_o, A_ORC);
    add_button(&campaign_screen, (irect_t){208, 312, 224, 28}, "Previous Menu (Esc)", SDLK_ESCAPE,
               A_PREVIOUS);
    show(&campaign_screen.menu);
}

/* Scenario info line for the setup and picker screens. */
static void describe(const w2_pud_info_t *info, char *out, size_t size) {
    snprintf(out, size, "%s%s%d x %d, %s", info->description, info->description[0] ? " - " : "",
             info->width, info->height, era_names[info->era & 3]);
}

static const char *owner_type(int owner) {
    switch (owner) {
    case 4: return "Computer";
    case 5: return "Person";
    case 6: return "Rescue (passive)";
    case 7: return "Rescue (active)";
    default: return NULL;
    }
}

/* Single Player Game Setup: Wargus RunSinglePlayerGameMenu. The map decides
 * the sides; the starting resources are the one setting the player owns. */
static void open_setup(void) {
    if (!scenario[0]) snprintf(scenario, sizeof(scenario), "%s", g_game_default_map);
    char path[1200], line[160];
    w2_pud_info_t info;
    snprintf(path, sizeof(path), "%s/%s", data_root, scenario);
    bool known = w2_pud_info(path, &info);
    screen_begin(&setup_screen, backdrop(), escape_to_single);
    screen_t *s = &setup_screen;
    add_label(s, (irect_t){330, 34, 310, 24}, "Single Player Game Setup", MALIGN_CENTER, false);
    add_label(s, (irect_t){344, 88, 40, 16}, "Player", MALIGN_LEFT, true);
    add_label(s, (irect_t){390, 88, 140, 16}, "Type", MALIGN_LEFT, true);
    add_label(s, (irect_t){540, 88, 90, 16}, "Race", MALIGN_LEFT, true);
    int row = 0;
    for (int i = 0; known && i < 8; ++i) {
        const char *type = owner_type(info.owners[i]);
        if (!type) continue;
        int y = 110 + row++ * 22;
        snprintf(line, sizeof(line), "%d", i + 1);
        add_label(s, (irect_t){344, y, 40, 16}, line, MALIGN_LEFT, true);
        add_label(s, (irect_t){390, y, 140, 16}, type, MALIGN_LEFT, true);
        add_label(s, (irect_t){540, y, 90, 16},
                  info.sides[i] == 0 ? "Human" : info.sides[i] == 1 ? "Orc" : "Neutral",
                  MALIGN_LEFT, true);
    }
    add_label(s, (irect_t){16, 224, 224, 16}, "Starting Resources", MALIGN_LEFT, true);
    add_button(s, (irect_t){16, 244, 224, 28}, resource_names[resources_mode], SDLK_r, A_RESOURCES);
    s->items[last_item(s)].hotkey_ink = 0;
    add_label(s, (irect_t){16, 360, 60, 20}, "Scenario:", MALIGN_LEFT, false);
    snprintf(line, sizeof(line), "%.*s", (int)strcspn(scenario, "."), scenario);
    add_label(s, (irect_t){16, 384, 370, 20}, line, MALIGN_LEFT, false);
    if (known) describe(&info, line, sizeof(line)); else snprintf(line, sizeof(line), "Scenario not found");
    add_label(s, (irect_t){16, 408, 370, 40}, line, MALIGN_LEFT, true);
    add_button(s, (irect_t){400, 360, 224, 28}, "Select Scenario", SDLK_e, A_SELECT);
    menuitem_t *start = add_button(s, (irect_t){400, 396, 224, 28}, "Start Game", SDLK_s, A_START);
    start->enabled = known;
    add_button(s, (irect_t){400, 432, 224, 28}, "Cancel Game", SDLK_c, A_CANCEL);
    show(&s->menu);
}

static int compare_entries(const void *a, const void *b) {
    return strcasecmp(((const char *)a), ((const char *)b)); /* file is the first member */
}

static void scan_scenarios(void) {
    entry_count = 0;
    DIR *dir = opendir(data_root);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir)) && entry_count < MAX_SCENARIOS) {
        size_t len = strlen(ent->d_name);
        if (ent->d_name[0] == '.' || len < 5 || len >= sizeof(entries[0].file) ||
            strcasecmp(ent->d_name + len - 4, ".pud")) continue;
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", data_root, ent->d_name);
        if (!w2_pud_info(path, &entries[entry_count].info)) continue;
        snprintf(entries[entry_count].file, sizeof(entries[0].file), "%s", ent->d_name);
        snprintf(entries[entry_count].label, sizeof(entries[0].label), "%.*s", (int)len - 4, ent->d_name);
        ++entry_count;
    }
    closedir(dir);
    qsort(entries, (size_t)entry_count, sizeof(entries[0]), compare_entries);
}

static const char *pick_row(const menuitem_t *item, int row) {
    (void)item;
    return row >= 0 && row < entry_count ? entries[row].label : "";
}

static void pick_changed(screen_t *s, int row) {
    char line[160] = "";
    if (row >= 0 && row < entry_count) describe(&entries[row].info, line, sizeof(line));
    set_text(s, pick_name_item, "%s", row >= 0 && row < entry_count ? entries[row].label : "");
    set_text(s, pick_detail_item, "%s", line);
    int ok = 0;
    for (int i = 0; i < s->menu.numitems; ++i) if (s->items[i].id == A_PICK_OK) ok = i;
    s->items[ok].enabled = row >= 0 && row < entry_count;
}

/* Select scenario (Wargus RunSelectScenarioMenu) or a campaign's levels. */
static void open_pick(pickmode_t mode) {
    pick_mode = mode;
    entry_count = 0;
    if (mode == PICK_SCENARIO) {
        scan_scenarios();
    } else {
        w2_pud_info_t levels[W2_CAMPAIGN_LEVELS];
        w2_campaign_infos(data_root, mode == PICK_ORC, levels);
        for (int i = 0; i < W2_CAMPAIGN_LEVELS; ++i) {
            if (!levels[i].width) continue;
            entries[entry_count].info = levels[i];
            snprintf(entries[entry_count].file, sizeof(entries[0].file), "%d", i + 1);
            snprintf(entries[entry_count].label, sizeof(entries[0].label), "%d. %s", i + 1,
                     levels[i].description[0] ? levels[i].description : "Level");
            ++entry_count;
        }
    }
    screen_t *s = &pick_screen;
    screen_begin(s, &art.dimmed, mode == PICK_SCENARIO ? escape_to_setup : escape_to_campaign);
    irect_t box = add_panel(s, &art.panel[1][W2_PANEL_SCENARIO]);
    int x = box.x, y = box.y;
    add_label(s, (irect_t){x, y + 8, box.w, 24}, mode == PICK_SCENARIO ? "Select scenario" :
              mode == PICK_HUMAN ? "Human Campaign" : "Orc Campaign", MALIGN_CENTER, false);
    menuitem_t *list = screen_add(s, MI_LIST, (irect_t){x + 16, y + 40, 300, 216});
    list->id = A_LIST;
    list->routine = front_action;
    list->font = large();
    list->ink = YELLOW;
    list->look[MS_PUSHED].ink = WHITE;
    list->color = 0xff5a3c14u;
    list->row_height = 18;
    list->row = pick_row;
    list->inset = (ivec2_t){4, 1};
    list->value = -1;
    int list_index = last_item(s);
    menuitem_t *bar = screen_add(s, MI_SCROLLBAR, (irect_t){x + 322, y + 40, 12, 216});
    bar->link = list_index;
    bar->color = 0xffb89040u;
    M_MenuSetRows(&s->items[list_index], entry_count);
    pick_name_item = last_item(s) + 1;
    add_label(s, (irect_t){x + 16, y + 264, 320, 20}, "", MALIGN_LEFT, false);
    pick_detail_item = last_item(s) + 1;
    add_label(s, (irect_t){x + 16, y + 286, 320, 16}, "", MALIGN_LEFT, true);
    add_button(s, (irect_t){x + 48, y + 318, 106, 28}, "OK", SDLK_o, A_PICK_OK);
    add_button(s, (irect_t){x + 198, y + 318, 106, 28}, "Cancel", SDLK_ESCAPE, A_PICK_CANCEL);
    int selected = 0;
    if (mode == PICK_SCENARIO)
        for (int i = 0; i < entry_count; ++i)
            if (!strcasecmp(entries[i].file, scenario)) selected = i;
    s->items[list_index].value = entry_count ? selected : -1;
    if (selected >= 0) {
        s->items[list_index].first_row = selected;
        M_MenuSetRows(&s->items[list_index], entry_count);
    }
    s->menu.itemOn = list_index;
    pick_changed(s, s->items[list_index].value);
    show(&s->menu);
}

static void start_level(const char *path, bool apply_resources) {
    snprintf(launch_path, sizeof(launch_path), "%s", path);
    W2_SetStartResources(apply_resources ? resources_mode : 0);
    menumap = launch_path;
    M_ClearMenus();
}

/* Credits: the engine's own, then what it follows. */
static void open_credits_screen(void) {
    static const char text[] =
        "open-rts\n"
        "An open reimplementation of classic real-time strategy engines.\n\n"
        "Warcraft II: Tides of Darkness was made by Blizzard Entertainment. "
        "This build runs from the game data of your own copy.\n\n"
        "Screens, menu layout and rules follow the Wargus project, "
        "a Stratagus game that plays Warcraft II.\n\n"
        "Warcraft is a trademark of Blizzard Entertainment.";
    screen_t *s = &credits_screen;
    screen_begin(s, &art.dimmed, escape_to_title);
    add_label(s, (irect_t){0, 24, 640, 28}, "Credits", MALIGN_CENTER, false);
    menuitem_t *prose = screen_add(s, MI_STATIC, (irect_t){80, 70, 480, 320});
    prose->font = large();
    prose->prose = text;
    add_button(s, (irect_t){208, 420, 224, 28}, "Previous Menu (Esc)", SDLK_ESCAPE, A_PREVIOUS);
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
    set_text(s, volume_item, "Sound Volume: %d%%", gamesettings.sound * 10);
    set_text(s, speed_item, "Game Speed: %s", speeds[speed_index()].name);
}

static void open_options_screen(menu_t *back, bool inlevel) {
    options_return = back;
    screen_t *s = &options_screen;
    screen_begin(s, inlevel ? NULL : &art.dimmed, options_escape);
    int race = button_race = inlevel ? side() : 1;
    irect_t box = add_panel(s, &art.panel[race][W2_PANEL_OPTIONS]);
    int x = box.x, y = box.y;
    add_label(s, (irect_t){x, y + 11, box.w, 20}, "Game Options", MALIGN_CENTER, false);
    add_label(s, (irect_t){x + 16, y + 44, 256, 20}, "", MALIGN_LEFT, false);
    volume_item = last_item(s);
    menuitem_t *bar = screen_add(s, MI_STATIC, (irect_t){x + 16, y + 68, 256, 18});
    bar->ownerdraw = draw_volume;
    add_button(s, (irect_t){x + 16, y + 96, 106, 28}, "Lower", SDLK_LEFT, A_LOWER);
    add_button(s, (irect_t){x + 166, y + 96, 106, 28}, "Raise", SDLK_RIGHT, A_RAISE);
    add_button(s, (irect_t){x + 32, y + 148, 224, 28}, "", SDLK_g, A_SPEED);
    speed_item = last_item(s);
    add_button(s, (irect_t){x + 91, y + 208, 106, 28}, "OK", SDLK_o, A_OPTIONS_OK);
    button_race = 1;
    options_refresh(s);
    show(&s->menu);
}

static void open_options(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)item;
    if (action == MA_ACTIVATE) open_options_screen(menu, true);
}

static void front_action(menu_t *menu, menuitem_t *item, menuaction_t action) {
    (void)menu;
    if (action != MA_ACTIVATE && !(item->kind == MI_LIST && action == MA_CHANGE)) return;
    switch (item->id) {
    case A_STANDARD: open_setup(); break;
    case A_CAMPAIGN: open_campaign(); break;
    case A_LOAD: M_StartMessage("There are no saved games."); break;
    case A_PREVIOUS: {
        menu_t *current = currentmenu;
        if (current && current->escape) current->escape(current);
        break;
    }
    case A_SELECT: open_pick(PICK_SCENARIO); break;
    case A_RESOURCES:
        resources_mode = (resources_mode + 1) % (int)(sizeof(resource_names) / sizeof(*resource_names));
        snprintf(item->text, sizeof(item->text), "%s", resource_names[resources_mode]);
        break;
    case A_START: {
        char path[1200];
        snprintf(path, sizeof(path), "%s", scenario);
        start_level(path, true);
        break;
    }
    case A_CANCEL: open_single(); break;
    case A_HUMAN: open_pick(PICK_HUMAN); break;
    case A_ORC: open_pick(PICK_ORC); break;
    case A_LIST: pick_changed(&pick_screen, item->value); break;
    case A_PICK_OK: {
        int row = -1;
        for (int i = 0; i < pick_screen.menu.numitems; ++i)
            if (pick_screen.items[i].id == A_LIST) row = pick_screen.items[i].value;
        if (row < 0 || row >= entry_count) break;
        if (pick_mode == PICK_SCENARIO) {
            snprintf(scenario, sizeof(scenario), "%s", entries[row].file);
            open_setup();
        } else if (w2_extract_campaign_level(data_root, atoi(entries[row].file),
                                             pick_mode == PICK_ORC, launch_path, sizeof(launch_path))) {
            start_level(launch_path, false);
        } else {
            M_StartMessage("The campaign level could not be read.");
        }
        break;
    }
    case A_PICK_CANCEL: {
        menu_t *current = currentmenu;
        if (current && current->escape) current->escape(current);
        break;
    }
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
    resources_mode = 0;
    if (!w2_load_menu_art(root, &art))
        fprintf(stderr, "warcraft-2: menu art was not loaded\n");
    return true;
}

menu_t *G_ControlPanel(app_t *app, bool inlevel) {
    front_app = app;
    const bitmapfont_t *font = art.font.sprite.numlumps ? &art.font : NULL;
    if (!inlevel) {
        /* The front end is drawn as the orc side (Wargus SetDefaultRaceView). */
        for (int i = 0; i < TITLE_COUNT; ++i)
            bind_button(&title_items[i], &art.widgets[1]);
        title_menu.background = art.title.numlumps ? &art.title : NULL;
        title_menu.palette = art.title.numlumps ? art.title.source_palette : NULL;
        return M_SimpleControlPanel(&title_menu);
    }
    int race = side();
    game_items[GAME_PANEL].sheet = art.panel[race][W2_PANEL_GAME].numlumps ?
        &art.panel[race][W2_PANEL_GAME] : NULL;
    game_items[GAME_PANEL].opaque = true;
    for (int s = 0; s < MS_STATES; ++s)
        game_items[GAME_PANEL].look[s].cell = game_items[GAME_PANEL].sheet ? 0 : -1;
    game_items[GAME_LABEL].font = font;
    bind_button(&game_items[GAME_SAVE], &art.widgets[race]);
    bind_button(&game_items[GAME_LOAD], &art.widgets[race]);
    bind_button(&game_items[GAME_OPTIONS], &art.widgets[race]);
    bind_button(&game_items[GAME_HELP], &art.widgets[race]);
    bind_button(&game_items[GAME_OBJECTIVES], &art.widgets[race]);
    bind_button(&game_items[GAME_END], &art.widgets[race]);
    bind_button(&game_items[GAME_RETURN], &art.widgets[race]);
    game_menu.background = NULL;
    game_menu.palette = NULL;
    game_menu.modal = true;
    game_menu.escape = game_escape;
    game_menu.held = NULL;
    game_menu.itemOn = -1;
    game_menu.target = NULL;
    return &game_menu;
}

void G_ShutdownMenus(void) {
    w2_free_menu_art(&art);
}
