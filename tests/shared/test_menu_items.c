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

int main(void) {
    RTS_RUN(input());
    RTS_RUN(hud_targets());
    RTS_RUN(layout());
    RTS_RUN(lists());
    RTS_RUN(animation());
    RTS_RUN(hud_input());
    RTS_RUN(drawing());
    puts("PASS: menu items focus, activate, check, type, scroll, animate, target, edit, anchor, align, layer and draw");
    return 0;
}
