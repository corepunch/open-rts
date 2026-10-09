#include "engine.h"
#include "t_local.h"

#include <string.h>

#define CHECK(c) RTS_CHECK(c, "menu items", #c)

enum { W = 64, MARKER = 77, GLYPH = 2 };
enum { BUTTON, HIDDEN, CHECK_BOX, RADIO_A, RADIO_B, FIELD, LIST, BAR, DOWN, NUMITEMS };

static menuitem_t items[NUMITEMS];
static menu_t menu = {.items = items, .numitems = NUMITEMS, .modal = true};
static app_t app = {.win = {W, W}};
static int activated[NUMITEMS], changed[NUMITEMS], escaped;
static int secondary, wheeled, targeted, cancelled, refreshed, tips;

static void routine(menu_t *screen, menuitem_t *item, menuaction_t action) {
    if (action == MA_SECONDARY) { ++secondary; return; }
    if (action == MA_TARGET) { ++targeted; return; }
    if (action == MA_CANCEL) { ++cancelled; return; }
    if (action == MA_WHEEL) { wheeled += screen->wheel; return; }
    ++(action == MA_ACTIVATE ? activated : changed)[item - screen->items];
}

static void escape(menu_t *screen) {
    (void)screen;
    ++escaped;
}

static const char *row(const menuitem_t *item, int index) {
    (void)item;
    static char text[2];
    text[0] = (char)('a' + index);
    return text;
}

static void key(SDL_Keycode code) {
    SDL_Event event = {.type = SDL_KEYDOWN};
    event.key.keysym.sym = code;
    M_MenuResponder(&menu, &app, &event);
}

static void mouse(Uint32 type, int x, int y) {
    SDL_Event event = {.type = type};
    if (type == SDL_MOUSEMOTION) {
        event.motion.x = x;
        event.motion.y = y;
    } else {
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = x;
        event.button.y = y;
    }
    M_MenuResponder(&menu, &app, &event);
}

static void click(int x, int y) {
    mouse(SDL_MOUSEBUTTONDOWN, x, y);
    mouse(SDL_MOUSEBUTTONUP, x, y);
}

static void type(const char *text) {
    SDL_Event event = {.type = SDL_TEXTINPUT};
    strcpy(event.text.text, text);
    M_MenuResponder(&menu, &app, &event);
}

/* Cell n of the sheet is one pixel of palette index n + 1. Every glyph of the
 * font is a GLYPH-wide square of index 9. */
static uint8_t cell_pixels[3] = {1, 2, 3};
static spritecell_t cells[3] = {
    {.rect = {0, 0, 1, 1}}, {.rect = {0, 0, 1, 1}}, {.rect = {0, 0, 1, 1}, .displacement = {2, 1}},
};
static spritelump_t lumps[3] = {{&cell_pixels[0]}, {&cell_pixels[1]}, {&cell_pixels[2]}};
static spritesheet_t sheet = {.cells = cells, .lumps = lumps, .numlumps = 3};
static uint8_t glyph_pixels[GLYPH * GLYPH] = {9, 9, 9, 9};
static spritecell_t glyph_cell = {.rect = {0, 0, GLYPH, GLYPH}};
static spritelump_t glyph_lump = {glyph_pixels};
static bitmapfont_t font = {
    .sprite = {.cells = &glyph_cell, .lumps = &glyph_lump, .numlumps = 1},
    .glyph_size = {GLYPH, GLYPH}, .line_h = GLYPH, .draw_divisor = 1,
};

static uint8_t pixel(int x, int y) {
    return screens[0].pixels[y * W + x];
}

static void draw(void) {
    memset(screens[0].pixels, MARKER, (size_t)W * W);
    M_MenuDrawer(&menu);
}

static void build(void) {
    memset(items, 0, sizeof(items));
    for (int i = 0; i < NUMITEMS; ++i)
        items[i] = (menuitem_t){.visible = true, .enabled = true, .routine = routine, .link = -1,
                                .look = {{.palette = -1}, {.palette = -1}, {.palette = -1}}};
    items[BUTTON].kind = MI_BUTTON;
    items[BUTTON].rect = (irect_t){0, 0, 10, 4};
    items[BUTTON].sheet = &sheet;
    items[BUTTON].look[MS_FOCUS].cell = 1;
    items[BUTTON].look[MS_PUSHED].cell = 2;
    items[HIDDEN].kind = MI_BUTTON;
    items[HIDDEN].rect = (irect_t){0, 0, 10, 4}; /* under BUTTON, and never live */
    items[HIDDEN].visible = false;
    items[CHECK_BOX].kind = MI_CHECK;
    items[CHECK_BOX].rect = (irect_t){0, 6, 4, 4};
    items[RADIO_A].kind = items[RADIO_B].kind = MI_CHECK;
    items[RADIO_A].group = items[RADIO_B].group = 1;
    items[RADIO_A].rect = (irect_t){6, 6, 4, 4};
    items[RADIO_B].rect = (irect_t){12, 6, 4, 4};
    items[RADIO_A].value = 1;
    items[FIELD].kind = MI_TEXTFIELD;
    items[FIELD].rect = (irect_t){0, 12, 20, 4};
    items[FIELD].font = &font;
    items[FIELD].maxchars = 4;
    items[LIST].kind = MI_LIST;
    items[LIST].rect = (irect_t){0, 20, 20, 8}; /* four rows of two pixels */
    items[LIST].font = &font;
    items[LIST].row_height = GLYPH;
    items[LIST].row = row;
    items[LIST].value = -1;
    items[LIST].color = 0xff050505u;
    items[BAR].kind = MI_SCROLLBAR;
    items[BAR].rect = (irect_t){22, 20, 4, 20};
    items[BAR].link = LIST;
    items[BAR].color = 0xff070707u;
    items[DOWN].kind = MI_BUTTON;
    items[DOWN].rect = (irect_t){26, 20, 4, 4};
    items[DOWN].link = LIST;
    items[DOWN].step = 3;
    M_MenuSetRows(&items[LIST], 10);
    menu.itemOn = 0;
    menu.held = NULL;
    menu.dropdown = NULL;
    menu.escape = escape;
    memset(activated, 0, sizeof(activated));
    memset(changed, 0, sizeof(changed));
    escaped = 0;
}

static int input(void) {
    build();
    /* A press activates; the hidden button under it never gets the click. */
    click(5, 2);
    CHECK(activated[BUTTON] == 1 && !activated[HIDDEN] && menu.itemOn == BUTTON && !menu.held);
    /* Focus steps over items that are hidden or disabled, and wraps. */
    items[CHECK_BOX].enabled = false;
    key(SDLK_DOWN);
    CHECK(menu.itemOn == RADIO_A);
    key(SDLK_UP);
    CHECK(menu.itemOn == BUTTON);
    key(SDLK_UP);
    CHECK(menu.itemOn == DOWN);
    key(SDLK_TAB);
    CHECK(menu.itemOn == BUTTON);
    SDL_Event backwards = {.key = {.type = SDL_KEYDOWN,
        .keysym = {.sym = SDLK_TAB, .mod = KMOD_SHIFT}}};
    M_MenuResponder(&menu, &app, &backwards);
    CHECK(menu.itemOn == DOWN);
    key(SDLK_TAB);
    key(SDLK_RETURN);
    CHECK(activated[BUTTON] == 2);
    /* Enter on a disabled item does nothing. */
    items[BUTTON].enabled = false;
    key(SDLK_RETURN);
    CHECK(activated[BUTTON] == 2);
    key(SDLK_ESCAPE);
    CHECK(escaped == 1);
    /* Moving the pointer focuses; it does not activate. */
    mouse(SDL_MOUSEMOTION, 13, 7);
    CHECK(menu.itemOn == RADIO_B && !activated[RADIO_B]);

    /* A check box toggles; a group keeps exactly one of its boxes set. */
    build();
    click(1, 7);
    CHECK(items[CHECK_BOX].value == 1 && activated[CHECK_BOX] == 1);
    click(1, 7);
    CHECK(items[CHECK_BOX].value == 0);
    click(13, 7);
    CHECK(!items[RADIO_A].value && items[RADIO_B].value == 1 && activated[RADIO_B] == 1);
    click(13, 7);
    CHECK(!items[RADIO_A].value && items[RADIO_B].value == 1);
    CHECK(items[CHECK_BOX].value == 0);

    /* The focused field takes text up to its limit and turns text input on. */
    click(1, 13);
    CHECK(menu.itemOn == FIELD && SDL_IsTextInputActive());
    type("ab\tcdef");
    CHECK(!strcmp(items[FIELD].text, "abcd") && changed[FIELD] == 1);
    key(SDLK_BACKSPACE);
    CHECK(!strcmp(items[FIELD].text, "abc") && changed[FIELD] == 2);
    click(5, 2);
    CHECK(!SDL_IsTextInputActive());
    type("x");
    CHECK(!strcmp(items[FIELD].text, "abc"));
    return 0;
}

static int lists(void) {
    build();
    menuitem_t *list = &items[LIST];
    /* A click selects the row under it; a click below the last row does not. */
    click(1, 20 + 2 * GLYPH + 1);
    CHECK(list->value == 2 && changed[LIST] == 1 && menu.itemOn == LIST);
    M_MenuSetRows(list, 2);
    click(1, 20 + 3 * GLYPH);
    CHECK(list->value == 2 && changed[LIST] == 1);
    M_MenuSetRows(list, 10);
    /* Up and Down move the selection and keep it in view. */
    key(SDLK_DOWN);
    key(SDLK_DOWN);
    CHECK(list->value == 4 && list->first_row == 1 && changed[LIST] == 3);
    for (int i = 0; i < 12; ++i) key(SDLK_DOWN);
    CHECK(list->value == 9 && list->first_row == 6);
    for (int i = 0; i < 12; ++i) key(SDLK_UP);
    CHECK(list->value == 0 && list->first_row == 0);
    /* The wheel and a linked button scroll the view, never past either end. */
    SDL_Event wheel = {.type = SDL_MOUSEWHEEL};
    wheel.wheel.y = -2;
    M_MenuResponder(&menu, &app, &wheel);
    CHECK(list->first_row == 2 && list->value == 0);
    click(27, 21);
    CHECK(list->first_row == 5 && activated[DOWN] == 1);
    click(27, 21);
    CHECK(list->first_row == 6);
    wheel.wheel.y = 50;
    M_MenuResponder(&menu, &app, &wheel);
    CHECK(list->first_row == 0);
    /* Dragging the bar centres the four visible rows on the pointer, and the
     * drag keeps hold of the bar outside its rectangle. */
    mouse(SDL_MOUSEBUTTONDOWN, 23, 30);
    CHECK(list->first_row == 3 && menu.held == &items[BAR]);
    mouse(SDL_MOUSEMOTION, 50, 38);
    CHECK(list->first_row == 6);
    mouse(SDL_MOUSEMOTION, 50, 0);
    CHECK(list->first_row == 0);
    mouse(SDL_MOUSEBUTTONUP, 50, 0);
    CHECK(!menu.held);
    /* Fewer rows pull the view back inside them. */
    list->first_row = 6;
    M_MenuSetRows(list, 5);
    CHECK(list->first_row == 1);
    M_MenuSetRows(list, 3);
    CHECK(list->first_row == 0);
    menu.itemOn = LIST;
    M_MenuSetRows(list, 10);
    key(SDLK_END);
    CHECK(list->value == 9 && list->first_row == 6);
    key(SDLK_PAGEUP);
    CHECK(list->value == 5);
    key(SDLK_HOME);
    CHECK(list->value == 0 && list->first_row == 0);
    SDL_Event double_click = {.button = {.type = SDL_MOUSEBUTTONDOWN,
        .button = SDL_BUTTON_LEFT, .x = 1, .y = 23, .clicks = 2}};
    M_MenuResponder(&menu, &app, &double_click);
    CHECK(list->value == 1 && activated[LIST] == 1);
    return 0;
}

static int dropdowns(void) {
    build();
    menuitem_t *choice = &items[FIELD];
    choice->kind = MI_DROPDOWN;
    choice->rows = 10;
    choice->row_height = 2;
    choice->popup_rows = 3;
    choice->row = row;
    choice->value = 0;
    /* The popup overlaps the list; a choice must not activate that list. */
    click(1, 13);
    CHECK(menu.dropdown == choice);
    key(SDLK_END);
    CHECK(menu.dropdown_row == 9 && choice->first_row == 7 && choice->value == 0);
    key(SDLK_ESCAPE);
    CHECK(!menu.dropdown && choice->value == 0 && !escaped);
    click(1, 13);
    key(SDLK_DOWN);
    key(SDLK_RETURN);
    CHECK(!menu.dropdown && choice->value == 1 && changed[FIELD] == 1);
    click(1, 13);
    click(1, 20);
    CHECK(!menu.dropdown && choice->value == 2 && !changed[LIST]);
    click(1, 13);
    click(5, 2);
    CHECK(!menu.dropdown && !activated[BUTTON]);
    /* Disabled choices do not open. */
    choice->enabled = false;
    click(1, 13);
    CHECK(!menu.dropdown);
    choice->enabled = true;
    /* A popup near the bottom opens upward, inside the screen. */
    choice->rect.y = W - 4;
    click(1, W - 3);
    CHECK(menu.dropdown == choice);
    key(SDLK_HOME);
    click(1, W - 9);
    CHECK(!menu.dropdown && choice->value == 0);
    return 0;
}

static int drawing(void) {
    build();
    V_AllocScreen(W, W);
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) palette[i] = 0xff000000u | (unsigned)i * 0x010101u;
    I_SetPalette(palette);
    memcpy(sheet.source_palette, palette, sizeof(palette));
    memcpy(font.sprite.source_palette, palette, sizeof(palette));
    for (int i = 0; i < 128; ++i) {
        font.glyph_index[i] = i >= 32 ? 0 : -1;
        font.glyph_width[i] = GLYPH + 1;
    }
    /* The picture follows the state: focused, plain, then pushed. The pushed
     * cell is drawn at its own displacement. */
    draw();
    CHECK(pixel(0, 0) == 2);
    menu.itemOn = FIELD;
    draw();
    CHECK(pixel(0, 0) == 1);
    menu.held = &items[BUTTON];
    draw();
    CHECK(pixel(0, 0) == MARKER && pixel(2, 1) == 3);
    menu.held = NULL;
    /* Text starts at the inset; the focused field ends in a caret. */
    strcpy(items[FIELD].text, "ab");
    items[FIELD].inset = (ivec2_t){1, 1};
    draw();
    CHECK(pixel(0, 13) == MARKER && pixel(1, 13) == 9 && pixel(4, 13) == 9);
    CHECK(pixel(7, 13) == 9 && pixel(10, 13) == MARKER);
    menu.itemOn = BUTTON;
    draw();
    CHECK(pixel(4, 13) == 9 && pixel(7, 13) == MARKER);
    /* Centred text is centred both ways. */
    items[FIELD].inset = (ivec2_t){0};
    items[FIELD].align = MALIGN_CENTER;
    draw();
    CHECK(pixel(6, 13) == MARKER && pixel(7, 13) == 9 && pixel(11, 13) == 9);
    CHECK(pixel(7, 12) == MARKER && pixel(7, 14) == 9 && pixel(7, 15) == MARKER);
    /* A list draws its visible rows and fills behind the selected one. The
     * thumb covers rows 2..5 of 10 on a 20-pixel bar. */
    items[LIST].value = 3;
    items[LIST].first_row = 2;
    draw();
    CHECK(pixel(0, 20) == 9 && pixel(10, 20) == MARKER);
    CHECK(pixel(0, 22) == 9 && pixel(10, 22) == 5 && pixel(10, 24) == MARKER);
    CHECK(pixel(22, 20) == 7 && pixel(23, 21) == MARKER);
    CHECK(pixel(23, 24) == 7 && pixel(23, 31) == 7 && pixel(23, 32) == MARKER);
    /* An empty list shows its prose instead. */
    M_MenuSetRows(&items[LIST], 0);
    items[LIST].prose = "none";
    draw();
    CHECK(pixel(0, 20) == 9 && pixel(22, 20) == MARKER);
    /* Wrapped log lines share the list's scroll position and native thumb. */
    items[LIST].row=NULL;
    items[LIST].prose="xxxx\nx\nx\nx\nx\nx";
    M_MenuSetRows(&items[LIST],6);
    items[LIST].first_row=0;
    draw();CHECK(pixel(3,20)==9);
    items[LIST].first_row=2;
    draw();CHECK(pixel(0,20)==9&&pixel(3,20)==MARKER);
    M_MenuSetRows(&items[LIST],0);
    items[LIST].prose="none";
    /* A fill goes behind the item; a hidden item draws nothing. */
    items[CHECK_BOX].fill = 0xff040404u;
    draw();
    CHECK(pixel(0, 6) == 4 && pixel(3, 9) == 4 && pixel(4, 6) == MARKER);
    items[CHECK_BOX].visible = false;
    draw();
    CHECK(pixel(0, 6) == MARKER);
    /* A crop can fill a scaled control rectangle, retaining color keying. */
    items[BUTTON].stretch = true;
    items[BUTTON].rect = (irect_t){30, 0, 4, 6};
    menu.itemOn = BUTTON;
    draw();
    CHECK(pixel(30, 0) == 2 && pixel(33, 5) == 2 && pixel(34, 5) == MARKER);
    uint8_t offset_pixels[] = {3};
    spritecell_t offset_cell = {.rect = {0, 0, 1, 1}, .displacement = {2, 1}};
    spritelump_t offset_lump = {offset_pixels};
    spritesheet_t offset_sheet = {.cells = &offset_cell, .lumps = &offset_lump,
        .numlumps = 1, .frame_size = {4, 4}};
    memcpy(offset_sheet.source_palette, palette, sizeof(palette));
    items[BUTTON].sheet = &offset_sheet;
    items[BUTTON].rect = (irect_t){30, 0, 8, 8};
    items[BUTTON].look[MS_FOCUS].cell = 0;
    draw();
    CHECK(pixel(34, 2) == 3 && pixel(35, 3) == 3 && pixel(33, 2) == MARKER);
    items[BUTTON].sheet = &sheet;
    items[BUTTON].look[MS_FOCUS].cell = 1;
    /* Picture rims sit outside the artwork. Focus and a pending world target
     * mark the outer rim; pressing does not overwrite the picture's edge. */
    items[BUTTON].rect = (irect_t){30, 10, 4, 6};
    items[BUTTON].frame.outer = 0xff040404u;
    items[BUTTON].frame.inner = 0xff050505u;
    items[BUTTON].color = 0xff060606u;
    menu.itemOn = -1;
    draw();
    CHECK(pixel(28, 8) == 4 && pixel(29, 9) == 5 && pixel(30, 10) == 1);
    CHECK(pixel(35, 17) == 4 && pixel(34, 16) == 5 && pixel(33, 15) == 1);
    menu.target = &items[BUTTON];
    draw();
    CHECK(pixel(28, 8) == 6 && pixel(29, 9) == 5 && pixel(30, 10) == 2);
    menu.target = NULL;
    menu.held = &items[BUTTON];
    draw();
    CHECK(pixel(28, 8) == 6 && pixel(30, 10) == 3);
    menu.held = NULL;
    items[BUTTON].enabled = false;
    draw();
    CHECK(pixel(28, 8) == 4 && pixel(30, 10) == 1);
    items[BUTTON].disabled_look = true;
    items[BUTTON].look[MS_DISABLED].cell = 1;
    draw();
    CHECK(pixel(30, 10) == 2);
    V_FreeScreen();
    return 0;
}

static int two_tics(const menuitem_t *item) {
    (void)item;
    return 2;
}

static int hud_input(void) {
    build();
    menu.modal = false;
    items[BUTTON].hotkey = SDLK_b;
    items[HIDDEN].hotkey = SDLK_h;
    items[CHECK_BOX].enabled = false;
    SDL_Event event = {.key = {.type = SDL_KEYDOWN, .keysym.sym = SDLK_x}};
    CHECK(!M_MenuResponder(&menu, &app, &event));
    event.key.keysym.sym = SDLK_b;
    CHECK(M_MenuResponder(&menu, &app, &event) && activated[BUTTON] == 1);
    event.key.repeat = 1;
    CHECK(!M_MenuResponder(&menu, &app, &event) && activated[BUTTON] == 1);
    event.key.repeat = 0;
    event.key.keysym.sym = SDLK_h;
    CHECK(M_MenuResponder(&menu, &app, &event) && activated[HIDDEN] == 1);
    event = (SDL_Event){.button = {.type = SDL_MOUSEBUTTONDOWN,
        .button = SDL_BUTTON_LEFT, .x = 40, .y = 40}};
    CHECK(!M_MenuResponder(&menu, &app, &event));
    event.button.x = 1; event.button.y = 7;
    CHECK(M_MenuResponder(&menu, &app, &event) && !activated[CHECK_BOX]);
    event.button.y = 1; event.button.button = SDL_BUTTON_RIGHT;
    secondary = 0;
    CHECK(M_MenuResponder(&menu, &app, &event) && secondary == 1 && !menu.held);
    event.button.button = SDL_BUTTON_LEFT;
    CHECK(M_MenuResponder(&menu, &app, &event) && menu.held == &items[BUTTON]);
    event.button.type = SDL_MOUSEBUTTONUP;
    event.button.x = 40; event.button.y = 40;
    CHECK(M_MenuResponder(&menu, &app, &event) && !menu.held && menu.itemOn == -1);
    event = (SDL_Event){.motion = {.type = SDL_MOUSEMOTION, .x = 1, .y = 1}};
    CHECK(!M_MenuResponder(&menu, &app, &event) && menu.itemOn == BUTTON);
    event = (SDL_Event){.wheel = {.type = SDL_MOUSEWHEEL, .y = -2}};
    wheeled = 0;
    CHECK(M_MenuResponder(&menu, &app, &event) && wheeled == -2);
    mouse(SDL_MOUSEMOTION, 40, 40);
    CHECK(menu.itemOn == -1 && !M_MenuResponder(&menu, &app, &event));
    menu.modal = true;
    return 0;
}

static int animation(void) {
    build();
    menuitem_t *item = &items[BUTTON];
    item->anim = (menuanim_t){.first = 4, .last = 6, .frame = 5};
    M_MenuTicker(&menu);
    CHECK(item->anim.frame == 5); /* Stopped until it is started. */
    /* A loop wraps to its first frame; one tick a frame without frametics. */
    M_MenuAnimate(item, MANIM_LOOP);
    CHECK(item->anim.frame == 4);
    int seen[5];
    for (int i = 0; i < 5; ++i) {
        M_MenuTicker(&menu);
        seen[i] = item->anim.frame;
    }
    CHECK(seen[0] == 5 && seen[1] == 6 && seen[2] == 4 && seen[3] == 5 && seen[4] == 6);
    /* A hidden item waits. */
    item->visible = false;
    M_MenuTicker(&menu);
    CHECK(item->anim.frame == 6);
    item->visible = true;
    /* A one-off holds its last frame and stops; frametics sets the pace. */
    menu.frametics = two_tics;
    M_MenuAnimate(item, MANIM_ONCE);
    for (int i = 0; i < 4; ++i) {
        M_MenuTicker(&menu);
        seen[i] = item->anim.frame;
    }
    CHECK(seen[0] == 5 && seen[1] == 5 && seen[2] == 6 && seen[3] == 6);
    CHECK(item->anim.mode == MANIM_ONCE);
    M_MenuTicker(&menu);
    CHECK(item->anim.frame == 6 && item->anim.mode == MANIM_STOPPED);
    menu.frametics = NULL;
    return 0;
}

static void count_refresh(menu_t *screen) {
    (void)screen;
    ++refreshed;
}

static void count_tip(const menu_t *screen, const menuitem_t *item) {
    (void)screen;
    if (!strcmp(item->tooltip, "tip")) ++tips;
}

/* A HUD item can wait for a click on the world, own the keyboard while a
 * line is typed, and be found by its id. */
static int hud_targets(void) {
    build();
    menu.modal = false;
    menu.refresh = count_refresh;
    items[BUTTON].id = 42;
    CHECK(M_MenuFind(&menu, 42) == &items[BUTTON] && !M_MenuFind(&menu, 43));
    refreshed = targeted = cancelled = 0;
    /* The world takes clicks until an item waits; then the click and its
     * release are the item's, and it waits no longer. */
    SDL_Event event = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT,
                                  .x = 40, .y = 40}};
    CHECK(!M_MenuResponder(&menu, &app, &event) && refreshed == 1);
    M_MenuTarget(&menu, &items[BUTTON]);
    CHECK(M_MenuResponder(&menu, &app, &event) && targeted == 1 && !menu.target);
    CHECK(menu.cursor.x == 40 && menu.cursor.y == 40);
    event.button.type = SDL_MOUSEBUTTONUP;
    CHECK(M_MenuResponder(&menu, &app, &event));
    CHECK(!M_MenuResponder(&menu, &app, &event));
    /* A click on the HUD itself still goes to the HUD. */
    M_MenuTarget(&menu, &items[BUTTON]);
    click(1, 1);
    CHECK(targeted == 1 && activated[BUTTON] == 1 && menu.target == &items[BUTTON]);
    /* The right button and Escape abandon the wait. */
    event.button.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_RIGHT;
    CHECK(M_MenuResponder(&menu, &app, &event) && cancelled == 1 && !menu.target);
    CHECK(!M_MenuResponder(&menu, &app, &event) && cancelled == 1);
    M_MenuTarget(&menu, &items[BUTTON]);
    SDL_Event escape_key = {.key = {.type = SDL_KEYDOWN, .keysym.sym = SDLK_ESCAPE}};
    CHECK(M_MenuResponder(&menu, &app, &escape_key) && cancelled == 2 && !menu.target);
    CHECK(!M_MenuResponder(&menu, &app, &escape_key));
    /* An edited field takes every key and text; Enter activates it and
     * Escape gives the keyboard back. */
    items[FIELD].text[0] = '\0';
    M_MenuEdit(&menu, &items[FIELD]);
    CHECK(menu.editing == &items[FIELD] && SDL_IsTextInputActive());
    type("hi");
    SDL_Event b_key = {.key = {.type = SDL_KEYDOWN, .keysym.sym = SDLK_b}};
    items[BUTTON].hotkey = SDLK_b;
    CHECK(M_MenuResponder(&menu, &app, &b_key) && activated[BUTTON] == 1);
    CHECK(!strcmp(items[FIELD].text, "hi") && changed[FIELD] == 1);
    key(SDLK_RETURN);
    CHECK(activated[FIELD] == 1 && menu.editing == &items[FIELD]);
    CHECK(M_MenuResponder(&menu, &app, &event)); /* the world gets no clicks */
    CHECK(M_MenuResponder(&menu, &app, &escape_key) && cancelled == 3);
    CHECK(!menu.editing && !SDL_IsTextInputActive());
    CHECK(!M_MenuResponder(&menu, &app, &b_key) || activated[BUTTON] == 2);
    menu.refresh = NULL;
    menu.modal = true;
    return 0;
}

/* Rects anchor to the edges of a larger screen, or the whole table
 * stretches; text aligns in its rect in the font of the item's state; the
 * hovered item's tooltip is drawn last. */
static int layout(void) {
    build();
    app_t wide = {.win = {W * 2, W * 2}};
    menu.size = (isize2_t){W, W};
    menu.app = &wide;
    items[BUTTON].anchor = MANCHOR_RIGHT;
    items[CHECK_BOX].anchor = MANCHOR_BOTTOM | MANCHOR_GROW;
    irect_t r = M_MenuItemRect(&menu, &items[BUTTON]);
    CHECK(r.x == W && r.y == 0 && r.w == 10 && r.h == 4);
    r = M_MenuItemRect(&menu, &items[CHECK_BOX]);
    CHECK(r.x == 0 && r.y == 6 + W && r.h == 4 + W);
    items[CHECK_BOX].anchor = MANCHOR_BOTTOM | MANCHOR_WIDE;
    r = M_MenuItemRect(&menu, &items[CHECK_BOX]);
    CHECK(r.x == 0 && r.y == 6 + W && r.w == 4 + W && r.h == 4);
    CHECK(M_MenuItemRect(&menu, &items[FIELD]).x == 0);
    menu.stretch = true;
    r = M_MenuItemRect(&menu, &items[FIELD]);
    CHECK(r.x == 0 && r.y == 24 && r.w == 40 && r.h == 8);
    menu.stretch = false;
    /* The pointer finds an anchored item where it is drawn. */
    SDL_Event event = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT,
                                  .x = W + 1, .y = 1}};
    CHECK(M_MenuResponder(&menu, &wide, &event) && activated[BUTTON] == 1);
    event.button.type = SDL_MOUSEBUTTONUP;
    CHECK(M_MenuResponder(&menu, &wide, &event) && !menu.held);
    menu.size = (isize2_t){0};
    menu.app = &app;

    V_AllocScreen(W, W);
    uint32_t colors[256];
    for (int i = 0; i < 256; ++i) colors[i] = 0xff000000u | (uint32_t)i * 0x010101u;
    I_SetPalette(colors);
    static uint8_t bold_pixels[GLYPH * GLYPH] = {11, 11, 11, 11};
    static spritelump_t bold_lump = {bold_pixels};
    static spritecell_t bold_cell = {.rect = {0, 0, GLYPH, GLYPH}};
    bitmapfont_t bold = font;
    bold.sprite.cells = &bold_cell;
    bold.sprite.lumps = &bold_lump;
    for (int i = 0; i < 128; ++i) font.glyph_index[i] = bold.glyph_index[i] = 0;
    for (int i = 0; i < NUMITEMS; ++i) if (i != BUTTON) items[i].visible = false;
    items[BUTTON].sheet = NULL;
    items[BUTTON].font = &font;
    items[BUTTON].look[MS_FOCUS].font = &bold;
    strcpy(items[BUTTON].text, "ab");
    items[BUTTON].align = MALIGN_RIGHT;
    menu.itemOn = -1;
    draw();
    CHECK(pixel(5, 0) == MARKER && pixel(6, 0) == 9 && pixel(9, 0) == 9);
    items[BUTTON].tooltip = "tip";
    menu.drawtip = count_tip;
    tips = 0;
    menu.modal = false;
    mouse(SDL_MOUSEMOTION, 1, 1);
    draw();
    CHECK(pixel(6, 0) == 11 && tips == 1);
    menu.drawtip = NULL;
    menu.modal = true;
    /* Loose text draws over later pictures, but not over a later layer. */
    items[FIELD].visible = true;
    items[FIELD].rect = (irect_t){0, 12, 20, 4};
    items[FIELD].align = 0;
    strcpy(items[FIELD].text, "a");
    items[DOWN] = (menuitem_t){.kind = MI_STATIC, .visible = true, .rect = {0, 12, 10, 4},
                               .fill = 0xff0c0c0cu, .link = -1};
    draw();
    CHECK(pixel(0, 12) == 9 && pixel(5, 12) == 12);
    items[DOWN].layer = true;
    draw();
    CHECK(pixel(0, 12) == 12);
    V_FreeScreen();
    return 0;
}

/* Native glyph rectangles can include leading and trailing blank rows. The
 * caption's authored extent, not that storage canvas, is what gets centred. */
static int text_alignment(void) {
    build();
    V_AllocScreen(W, W);
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) palette[i] = 0xff000000u | (uint32_t)i * 0x010101u;
    I_SetPalette(palette);
    uint8_t pixels[12] = {0, 0, 0, 0, 9, 9, 9, 9, 0, 0, 0, 0};
    spritecell_t cell = {.rect = {0, 0, 2, 6}, .bounds = {0, 2, 2, 2}, .displacement = {0, 1}};
    spritelump_t lump = {pixels};
    bitmapfont_t padded = font;
    padded.sprite.cells = &cell;
    padded.sprite.lumps = &lump;
    padded.glyph_size = (isize2_t){2, 6};
    padded.line_h = 6;
    padded.native_origin = true;
    memcpy(padded.sprite.source_palette, palette, sizeof(palette));
    for (int i = 0; i < 128; ++i) {
        padded.glyph_index[i] = 0;
        padded.glyph_width[i] = 3;
    }
    irect_t bounds = V_TextBounds(&padded, "a\na");
    CHECK(bounds.w == 3 && bounds.y == 3 && bounds.h == 8);
    for (int i = 1; i < NUMITEMS; ++i) items[i].visible = false;
    menuitem_t *button = &items[BUTTON];
    button->sheet = NULL;
    button->font = &padded;
    button->rect = (irect_t){10, 10, 10, 10};
    button->align = MALIGN_CENTER;
    strcpy(button->text, "a");
    menu.itemOn = -1;
    draw();
    CHECK(pixel(13, 13) == MARKER && pixel(13, 14) == 9 && pixel(13, 15) == 9 && pixel(13, 16) == MARKER);
    button->look[MS_PUSHED].shift = (ivec2_t){1, 1};
    menu.held = button;
    draw();
    CHECK(pixel(14, 14) == MARKER && pixel(14, 15) == 9 && pixel(14, 16) == 9 && pixel(14, 17) == MARKER);
    menu.held = NULL;
    button->align = MALIGN_BOTTOM;
    draw();
    CHECK(pixel(10, 17) == MARKER && pixel(10, 18) == 9 && pixel(10, 19) == 9);
    button->align = MALIGN_LEFT;
    draw();
    CHECK(pixel(10, 12) == MARKER && pixel(10, 13) == 9 && pixel(10, 14) == 9);
    button->align = MALIGN_CENTER;
    strcpy(button->text, "a\na");
    draw();
    CHECK(pixel(13, 10) == MARKER && pixel(13, 11) == 9 && pixel(13, 12) == 9);
    CHECK(pixel(13, 17) == 9 && pixel(13, 18) == 9 && pixel(13, 19) == MARKER);
    V_FreeScreen();
    return 0;
}

static int sliders(void) {
    build();
    for (int i = 1; i < NUMITEMS; ++i) items[i].visible = false;
    menuitem_t *slider = &items[BUTTON];
    *slider = (menuitem_t){.kind = MI_SLIDER, .visible = true, .enabled = true,
                           .rect = {10, 10, 41, 5}, .range = {-2, 2}, .value = 0,
                           .routine = routine, .sheet = &sheet, .stretch = true,
                           .thumb = {.cell = 0, .part = {0, 0, 1, 1}, .palette = -1}};
    for (int i = 0; i < MS_STATES; ++i) slider->look[i] = (menulook_t){.cell = 1, .palette = -1};
    click(10, 12);
    CHECK(slider->value == -2);
    mouse(SDL_MOUSEBUTTONDOWN, 30, 12);
    CHECK(slider->value == 0 && menu.held == slider);
    mouse(SDL_MOUSEMOTION, 63, 12);
    CHECK(slider->value == 2);
    mouse(SDL_MOUSEMOTION, 0, 12);
    CHECK(slider->value == -2);
    mouse(SDL_MOUSEBUTTONUP, 0, 12);
    CHECK(!menu.held && changed[BUTTON] == 4 && activated[BUTTON] == 0);
    menu.itemOn = BUTTON;
    key(SDLK_END);
    CHECK(slider->value == 2 && menu.itemOn == BUTTON);
    key(SDLK_LEFT);
    CHECK(slider->value == 1);
    key(SDLK_HOME);
    CHECK(slider->value == -2);
    key(SDLK_RIGHT);
    CHECK(slider->value == -1);
    V_AllocScreen(W, W);
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) palette[i] = 0xff000000u | (uint32_t)i;
    I_SetPalette(palette);
    memcpy(sheet.source_palette, palette, sizeof(palette));
    slider->value = 0;
    draw();
    CHECK(pixel(29, 12) == 2 && pixel(30, 12) == 1 && pixel(31, 12) == 2);
    slider->enabled = false;
    click(50, 12);
    CHECK(slider->value == 0);
    V_FreeScreen();
    return 0;
}

static int previews;
static void draw_preview(const menu_t *screen, const menuitem_t *item, irect_t view) {
    (void)screen; (void)item;
    ++previews;
    V_FillRect((irect_t){view.x - 1, view.y - 1, view.w + 2, view.h + 2}, MARKER);
}

static int target_preview(void) {
    build();
    app.win = (isize2_t){1280, 960};
    menu.app = &app;
    menu.modal = false;
    for (int i = 0; i < NUMITEMS; ++i) items[i].visible = false;
    items[BUTTON].visible = true;
    items[BUTTON].rect = (irect_t){0, 0, 10, 10};
    items[BUTTON].drawtarget = draw_preview;
    V_AllocScreen(app.win.w, app.win.h);
    irect_t view = G_WorldViewport(&app);
    menu.cursor = (ivec2_t){view.x + view.w / 2, view.y + view.h / 2};
    M_MenuTarget(&menu, &items[BUTTON]);
    V_SetDrawScale(1);
    V_FillRect((irect_t){0, 0, app.win.w, app.win.h}, 0);
    M_MenuDrawer(&menu);
    CHECK(previews == 1 && V_GetDrawScale() == 1);
    CHECK(screens[0].pixels[menu.cursor.y * app.win.w + menu.cursor.x] == MARKER);
    if (view.x) CHECK(screens[0].pixels[view.y * app.win.w + view.x - 1] != MARKER);
    if (view.y) CHECK(screens[0].pixels[(view.y - 1) * app.win.w + view.x] != MARKER);
    menu.cursor = (ivec2_t){5, 5}; /* Over a visible HUD item. */
    M_MenuDrawer(&menu);
    CHECK(previews == 1);
    menu.cursor = (ivec2_t){view.x + view.w / 2, view.y + view.h / 2};
    key(SDLK_ESCAPE);
    M_MenuDrawer(&menu);
    CHECK(previews == 1 && !menu.target);
    V_FreeScreen();
    app.win = (isize2_t){W, W};
    return 0;
}

static int decorative_input(void) {
    build();menu.modal=false;
    for(int i=0;i<NUMITEMS;i++)items[i].visible=false;
    items[BUTTON]=(menuitem_t){.kind=MI_STATIC,.visible=true,.passthrough=true,.rect={0,0,W,W}};
    SDL_Event event={.type=SDL_MOUSEBUTTONDOWN};
    event.button.button=SDL_BUTTON_LEFT;event.button.x=5;event.button.y=5;
    CHECK(!M_MenuResponder(&menu,&app,&event));
    items[BUTTON].passthrough=false;
    CHECK(M_MenuResponder(&menu,&app,&event));
    items[BUTTON]=(menuitem_t){.kind=MI_BUTTON,.visible=true,.enabled=true,.rect={0,0,W,W},
        .hitbox={10,10,20,20},.routine=routine};
    activated[BUTTON]=0;
    M_MenuResponder(&menu,&app,&event);CHECK(!activated[BUTTON]);
    event.button.x=15;event.button.y=15;
    M_MenuResponder(&menu,&app,&event);CHECK(activated[BUTTON]==1);
    menu.modal=true;
    return 0;
}

int main(void) {
    RTS_RUN(decorative_input());
    RTS_RUN(input());
    RTS_RUN(hud_targets());
    RTS_RUN(layout());
    RTS_RUN(lists());
    RTS_RUN(dropdowns());
    RTS_RUN(animation());
    RTS_RUN(hud_input());
    RTS_RUN(drawing());
    RTS_RUN(text_alignment());
    RTS_RUN(sliders());
    RTS_RUN(target_preview());
    puts("PASS: menu items focus, activate, check, type, scroll, animate, target, edit, anchor, align, layer and draw");
    return 0;
}
