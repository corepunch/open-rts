#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront skin", #c)

/* The interface's metal skins (art/ui.py, hu_skin.c): the sidebar's plate, screens and buttons
 * are nine-slices of assets/ui.pcx, and a button shows its four states. The engine's nine-slice
 * keeps the rims their size whatever the rectangle. OPEN_RTS_RF_UI_SHOTS=DIR writes the sidebar
 * in each state as a BMP. */

static const char *MAP = "games/rustfront/maps/skirmish.txt";
static app_t app = {.win = {1280, 960}, .running = true};
static tileset_t tiles;
static spritesheet_t fallback;

static void draw(menu_t *menu, const char *name) {
    V_BeginFrame(0xff000000u);
    if (menu->refresh) menu->refresh(menu);
    M_MenuDrawer(menu);
    const char *dir = getenv("OPEN_RTS_RF_UI_SHOTS");
    if (!dir) return;
    isize2_t size = V_DrawSize();
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, size.w, size.h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return;
    V_ReadPixels(surface->pixels, surface->pitch);
    char path[1024];
    snprintf(path, sizeof(path), "%s/rustfront-skin-%s.bmp", dir, name);
    SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
}

/* The framebuffer's colours over an item's rectangle, as a hash: tells two looks apart. */
static uint32_t look_of(irect_t rect) {
    isize2_t size = V_DrawSize();
    uint32_t *pixels = malloc((size_t)size.w * size.h * 4);
    if (!pixels) return 0;
    V_ReadPixels(pixels, size.w * 4);
    uint32_t h = 2166136261u;
    for (int y = rect.y; y < rect.y + rect.h; ++y)
        for (int x = rect.x; x < rect.x + rect.w; ++x) h = (h ^ pixels[y * size.w + x]) * 16777619u;
    free(pixels);
    return h;
}

static irect_t on_screen(const menuitem_t *item) {
    isize2_t size = V_DrawSize();
    return (irect_t){item->rect.x * size.w / 640, item->rect.y * size.h / 480,
                     item->rect.w * size.w / 640, item->rect.h * size.h / 480};
}

static menuitem_t *by_hotkey(menu_t *menu, SDL_Keycode key) {
    for (int i = 0; i < menu->numitems; ++i)
        if (menu->items[i].hotkey == key) return &menu->items[i];
    return NULL;
}

/* The same button drawn 100 and 300 pixels wide keeps its rims: the left and right 9 pixels of
 * each match, and only the middle has stretched. */
static int check_nine_slice(void) {
    V_BeginFrame(0xff000000u);
    RF_DrawSkin(RF_SKIN_BUTTON, (irect_t){10, 10, 100, 40}, 2);
    RF_DrawSkin(RF_SKIN_BUTTON, (irect_t){10, 100, 300, 40}, 2);
    CHECK(look_of((irect_t){10, 10, 9, 40}) == look_of((irect_t){10, 100, 9, 40}));
    CHECK(look_of((irect_t){101, 10, 9, 40}) == look_of((irect_t){301, 100, 9, 40}));
    CHECK(look_of((irect_t){10, 10, 100, 40}) != look_of((irect_t){10, 100, 100, 40}));
    puts("PASS: a nine-slice keeps its rims their size and stretches its middle");
    return 0;
}

static int test_sidebar(void) {
    P_FreeLevel(&level);
    P_InitThinkers();
    CHECK(G_DoLoadLevel(MAP, &level) && P_InitSight());
    CHECK(P_LoadThings(MAP) > 0);
    CHECK(W_LoadAssets(g_game_default_root, &level, NULL, &tiles, &fallback));
    R_SetLevelPalette(&tiles);
    V_AllocScreen(app.win.w, app.win.h);
    menu_t *menu = RF_InitHUD(&app, g_game_default_root);
    CHECK(menu && RF_HaveSkin());
    if (check_nine_slice()) return 1;
    menuitem_t *panel = &menu->items[0], *sell = by_hotkey(menu, SDLK_x), *repair = by_hotkey(menu, SDLK_r);
    CHECK(panel->sheet && panel->stretch && panel->slice > 0 && panel->fill == 0);
    CHECK(sell && sell->sheet && sell->disabled_look);
    for (int s = 0; s < MS_STATES; ++s) CHECK(sell->look[s].part.w > 0);
    /* Nothing selected: selling is out, its button dull. */
    draw(menu, "idle");
    CHECK(!sell->enabled);
    uint32_t disabled = look_of(on_screen(sell));
    /* The Command Center selected: its build list shows as button rows, SELL wakes up, and the
     * undamaged HQ keeps REPAIR dull. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == 0 && u->type_id == MT_HQ) P_MobjSetSelected(u, true);
    }
    draw(menu, "hq");
    CHECK(sell->enabled && !repair->enabled);
    uint32_t normal = look_of(on_screen(sell));
    CHECK(normal != disabled);
    /* Pressed: the face sinks. */
    menu->held = sell;
    menu->over = true;
    draw(menu, "pressed");
    uint32_t pressed = look_of(on_screen(sell));
    CHECK(pressed != normal && pressed != disabled);
    menu->held = NULL;
    /* Focus: the pointer over it lights the lamp. */
    irect_t r = on_screen(sell);
    menu->cursor = (ivec2_t){r.x + r.w / 2, r.y + r.h / 2};
    menu->itemOn = (int)(sell - menu->items);
    draw(menu, "focus");
    uint32_t focus = look_of(on_screen(sell));
    CHECK(focus != normal && focus != pressed && focus != disabled);
    RF_ShutdownHUD();
    puts("PASS: the sidebar wears its metal: plate, screens, and buttons normal, focused, pressed and disabled");
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RTS_RUN(test_sidebar());
    SDL_Quit();
    return 0;
}
