#include "engine.h"
#include "t_local.h"

#include <string.h>

#define CHECK(c) RTS_CHECK(c, "menu items", #c)

enum { W = 64, MARKER = 77, GLYPH = 2 };
enum { BUTTON, HIDDEN, CHECK_BOX, RADIO_A, RADIO_B, FIELD, LIST, BAR, DOWN, NUMITEMS };

static menuitem_t items[NUMITEMS];
static menu_t menu = {.items = items, .numitems = NUMITEMS};
static app_t app = {.win = {W, W}};
static int activated[NUMITEMS], changed[NUMITEMS], escaped;

static void routine(menu_t *screen, menuitem_t *item, menuaction_t action) {
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
                                .palette = {-1, -1, -1}};
    items[BUTTON].kind = MI_BUTTON;
    items[BUTTON].rect = (irect_t){0, 0, 10, 4};
    items[BUTTON].sheet = &sheet;
    items[BUTTON].cell[MS_FOCUS] = 1;
    items[BUTTON].cell[MS_PUSHED] = 2;
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
    items[FIELD].centered = true;
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
    V_FreeScreen();
    return 0;
}

int main(void) {
    RTS_RUN(input());
    RTS_RUN(lists());
    RTS_RUN(drawing());
    puts("PASS: menu items focus, activate, check, type, scroll and draw");
    return 0;
}
