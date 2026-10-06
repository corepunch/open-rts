#include "engine.h"
#include "w2_local.h"

#include <stdio.h>
#include <string.h>

/* The title is REZDAT entry 13 with the retail five-button column: 224x28
 * buttons at x 208, rows from y 240 every 36 pixels, below the top-left logo.
 * The in-level game menu uses panel 1 on the left, not the centred Wargus box. */

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
    [TITLE_CREDITS] = TITLE_BUTTON(3, "Show Credits", SDLK_h, menu_note,
                                   "Credits are not shown in this build."),
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
        .ink = 0xffffe84au, .routine = menu_note,
        .userdata = "Options are not stored in this build.",
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

/* Not M_MenuBeginLevel itself: the simple panel relabels that button. */
static void single_player(menu_t *menu, menuitem_t *item, menuaction_t action) {
    M_MenuBeginLevel(menu, item, action);
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

bool G_InitMenus(app_t *app, const char *root) {
    (void)app;
    if (!w2_load_menu_art(root, &art))
        fprintf(stderr, "warcraft-2: menu art was not loaded\n");
    return true;
}

menu_t *G_ControlPanel(app_t *app, bool inlevel) {
    (void)app;
    const bitmapfont_t *font = art.font.sprite.numlumps ? &art.font : NULL;
    if (!inlevel) {
        /* The front end is drawn as the orc side (Wargus SetDefaultRaceView). */
        for (int i = 0; i < TITLE_COUNT; ++i)
            bind_widget(&title_items[i], &art.widgets[1], 16, 17);
        title_menu.background = art.title.numlumps ? &art.title : NULL;
        title_menu.palette = art.title.numlumps ? art.title.source_palette : NULL;
        return M_SimpleControlPanel(&title_menu);
    }
    int race = side();
    game_items[GAME_PANEL].sheet = art.panel[race].numlumps ? &art.panel[race] : NULL;
    game_items[GAME_PANEL].opaque = true;
    for (int s = 0; s < MS_STATES; ++s)
        game_items[GAME_PANEL].look[s].cell = game_items[GAME_PANEL].sheet ? 0 : -1;
    game_items[GAME_LABEL].font = font;
    bind_widget(&game_items[GAME_SAVE], &art.widgets[race], 10, 11);
    bind_widget(&game_items[GAME_LOAD], &art.widgets[race], 10, 11);
    bind_widget(&game_items[GAME_OPTIONS], &art.widgets[race], 16, 17);
    bind_widget(&game_items[GAME_HELP], &art.widgets[race], 16, 17);
    bind_widget(&game_items[GAME_OBJECTIVES], &art.widgets[race], 16, 17);
    bind_widget(&game_items[GAME_END], &art.widgets[race], 16, 17);
    bind_widget(&game_items[GAME_RETURN], &art.widgets[race], 16, 17);
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
