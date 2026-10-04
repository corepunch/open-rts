#include "t_local.h"

#define CHECK(c) RTS_CHECK(c, "UI scale", #c)

static int selected_row = -1;
static cell_t target;

static void select_row(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action == MA_CHANGE) selected_row = item->value;
    if (action == MA_TARGET)
        target = R_ScreenToGrid(menu->app, menu->cursor.x, menu->cursor.y);
}

static const char *row(const menuitem_t *item, int index) {
    (void)item;
    return index & 1 ? "B" : "A";
}

static void draw_primitives(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    V_DrawLine((ivec2_t){rect.x, rect.y}, (ivec2_t){rect.x + 9, rect.y + 7}, 9);
    V_DrawRectOutline((irect_t){rect.x + 3, rect.y + 9, 16, 10}, 10);
    V_DrawSmallText((irect_t){rect.x, rect.y + 22, 40, 7}, "HUD", 0xffccccccu, V_DrawSize());
    irect_t previous = V_GetClip();
    V_SetClip((irect_t){rect.x + 2, rect.y + 32, 3, 2});
    uint8_t pixels[] = {3, 4, 5, 6};
    V_DrawBlockScaled((irect_t){rect.x, rect.y + 31, 8, 4}, pixels, (isize2_t){2, 2}, 2, NULL, 0);
    V_SetClip(previous);
}

static void click(menu_t *menu, app_t *app, ivec2_t at) {
    SDL_Event event = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT,
                                .x = at.x, .y = at.y}};
    M_MenuResponder(menu, app, &event);
    event.type = SDL_MOUSEBUTTONUP;
    M_MenuResponder(menu, app, &event);
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) palette[i] = 0xff000000u | (unsigned)i * 0x010101u;
    I_SetPalette(palette);
    uint8_t glyph[] = {8, 0, 8, 8};
    spritecell_t cell = {.rect = {0, 0, 2, 2}, .displacement = {1, 1}};
    spritelump_t lump = {glyph};
    spritesheet_t sheet = {.cells = &cell, .lumps = &lump, .numlumps = 1};
    memcpy(sheet.source_palette, palette, sizeof(palette));
    bitmapfont_t font = {.sprite = sheet, .glyph_size = {2, 2}, .line_h = 3};
    font.glyph_width['A'] = font.glyph_width['B'] = 3;
    menuitem_t items[] = {
        {.visible = true, .rect = {30, 20, 40, 36}, .ownerdraw = draw_primitives},
        {.visible = true, .enabled = true, .kind = MI_BUTTON, .rect = {600, 20, 20, 10},
         .anchor = MANCHOR_RIGHT, .sheet = &sheet, .font = &font, .text = "AB"},
        {.visible = true, .enabled = true, .kind = MI_LIST, .rect = {100, 100, 20, 24},
         .rows = 4, .row_height = 6, .row = row, .font = &font, .routine = select_row},
        {.visible = true, .enabled = true, .kind = MI_MINIMAP, .rect = {590, 200, 40, 40}},
    };
    app_t app = {.win = {640, 480}, .cell = {32, 32}};
    menu_t menu = {.app = &app, .items = items, .numitems = 4, .itemOn = -1, .size = {640, 480}};
    V_AllocScreen(640, 480);
    V_BeginFrame(0xff000000u);
    M_MenuDrawer(&menu);
    uint8_t *native = malloc(640 * 480);
    CHECK(native && I_ReadScreen(native));
    irect_t native_view = G_WorldViewport(&app);

    app.win = (isize2_t){1280, 960};
    V_AllocScreen(app.win.w, app.win.h);
    V_BeginFrame(0xff000000u);
    int scale = R_UIScale(&app);
    irect_t view = G_WorldViewport(&app);
#ifdef RTS_NATIVE_WORLD
    CHECK(scale == 2);
    CHECK(view.x == native_view.x * 2 && view.y == native_view.y * 2);
    CHECK(view.w == native_view.w * 2 && view.h == native_view.h * 2);
#else
    CHECK(scale == 1);
    (void)native_view;
#endif
    M_MenuDrawer(&menu);
    CHECK(V_GetDrawScale() == 1);
    if (scale == 2) {
        for (int y = 0; y < 960; ++y)
            for (int x = 0; x < 1280; ++x)
                CHECK(screens[0].pixels[y * 1280 + x] == native[(y / 2) * 640 + x / 2]);
    }
    free(native);
    click(&menu, &app, (ivec2_t){105 * scale, 113 * scale});
    CHECK(selected_row == 2);

    /* HUD target callbacks keep world pixels; they are never divided by the UI scale. */
    menu.target = &items[2];
    click(&menu, &app, (ivec2_t){900, 500});
    CHECK(target.x == 28 && target.y == 15);
    float x, y;
    R_GridToScreen(&app, 1, 1, &x, &y);
    CHECK(x == 32 && y == 32);
    cell_t at = R_ScreenToGrid(&app, 32, 32);
    CHECK(at.x == 1 && at.y == 1);
    V_DrawBlock((ivec2_t){900, 500}, glyph, (isize2_t){2, 2}, 2, NULL, V_OPAQUE);
    CHECK(screens[0].pixels[500 * 1280 + 900] == 8);
    CHECK(screens[0].pixels[500 * 1280 + 901] == 0);

    level.width = level.height = 200;
    irect_t radar = M_MenuItemRect(&menu, &items[3]);
    ivec2_t mouse = {radar.x + radar.w / 2, radar.y + radar.h / 2};
    click(&menu, &app, mouse);
    fvec2_t centre = {200.0f * (2 * (mouse.x - radar.x) + 1) / (2 * radar.w),
                     200.0f * (2 * (mouse.y - radar.y) + 1) / (2 * radar.h)};
    view = G_WorldViewport(&app);
    R_GridToScreen(&app, centre.x, centre.y, &x, &y);
    CHECK(fabsf(x - (view.x + view.w / 2.0f)) < 0.01f);
    CHECK(fabsf(y - (view.y + view.h / 2.0f)) < 0.01f);
    level.width = level.height = 0;

    app.window = SDL_CreateWindow("UI scale", 0, 0, 800, 600, SDL_WINDOW_HIDDEN);
    CHECK(app.window);
    R_RefreshViewport(&app);
#ifdef RTS_NATIVE_WORLD
    CHECK(app.win.w == 800 && app.win.h == 600);
    CHECK(screens[0].w == 800 && screens[0].h == 600);
    SDL_SetWindowSize(app.window, 1280, 960);
    R_RefreshViewport(&app);
    CHECK(app.win.w == 1280 && app.win.h == 960);
    CHECK(screens[0].w == 1280 && screens[0].h == 960);
    CHECK(R_UIScale(&app) == 2);
#else
    CHECK(app.win.w == 1280 && app.win.h == 960);
#endif
    SDL_DestroyWindow(app.window);
    V_FreeScreen();
    SDL_Quit();
    puts("PASS: scaled HUD pixels/text/clips, list clicks, world targets, minimap centring and resize");
    return 0;
}
