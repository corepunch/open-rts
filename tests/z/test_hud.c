#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdlib.h>

#define CHECK(c) RTS_CHECK(c, "z hud", #c)

static menuitem_t *button(menu_t *hud, int native) {
    for (int i = 0; i < hud->numitems; ++i) {
        menuitem_t *item = &hud->items[i];
        if (item->kind == MI_BUTTON && item->sheet && item->sheet->numlumps == Z_UI_WINDOW + 123 &&
            item->look[MS_NORMAL].cell == Z_UI_WINDOW + native - Z_UI_FIRST) return item;
    }
    return NULL;
}

static void click(menu_t *hud, menuitem_t *item) {
    int scale = R_UIScale(hud->app);
    SDL_Event e = {.type = SDL_MOUSEBUTTONDOWN};
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = (item->rect.x + item->rect.w / 2) * scale;
    e.button.y = (item->rect.y + item->rect.h / 2) * scale;
    M_MenuResponder(hud, hud->app, &e);
    e.type = SDL_MOUSEBUTTONUP;
    M_MenuResponder(hud, hud->app, &e);
    hud->refresh(hud);
}

/* Optional visual evidence, using exactly the renderer exercised by the test. */
static void shot(const char *name) {
    const char *dir = getenv("OPEN_RTS_Z_UI_SHOTS");
    if (!dir) return;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, screens[0].w, screens[0].h, 24, SDL_PIXELFORMAT_RGB24);
    if (!surface) return;
    for (int y = 0; y < screens[0].h; ++y)
        for (int x = 0; x < screens[0].w; ++x) {
            uint32_t c = vpalette[screens[0].pixels[y * screens[0].w + x]];
            uint8_t *to = (uint8_t *)surface->pixels + y * surface->pitch + x * 3;
            to[0] = c >> 16; to[1] = c >> 8; to[2] = c;
        }
    char path[1024];
    snprintf(path, sizeof(path), "%s/z-%s.bmp", dir, name);
    SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
}

static uint32_t picture_hash(irect_t rect, int scale) {
    uint32_t hash = 2166136261u;
    for (int y = 0; y < rect.h; ++y)
        for (int x = 0; x < rect.w; ++x) {
            hash ^= screens[0].pixels[(rect.y + y) * scale * screens[0].w + (rect.x + x) * scale];
            hash *= 16777619u;
        }
    return hash;
}

static int test_hud(int scale) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z"};
    CHECK(model && rts_game_model_load(model, &config));
    app_t app = {.win = {640 * scale, 480 * scale}, .cell = {16,16}};
    V_AllocScreen(app.win.w, app.win.h);
    tileset_t tiles = {0};
    spritesheet_t unit = {0};
    CHECK(W_LoadAssets("data/Z", &level, g_game_default_sprite, &tiles, &unit));
    R_SetLevelPalette(&tiles);
    spritecache_t cache = {0};
    mobjlist_t list = P_ListMobjs();
    CHECK(R_InitSprites("data/Z", &level, list.items, list.count, &cache));
    hudview = (hudview_t){list.items, list.count, &cache, NULL, &tiles};
    mobj_t *fort = NULL, *robot = NULL, *enemy = NULL;
    for (int i = 0; i < list.count; ++i) {
        mobj_t *mo = list.items[i];
        P_MobjSetSelected(mo, false);
        if (mo->type_id == MT_FORT && mo->owner == consoleplayer) fort = mo;
        if (mo->type_id == MT_UNIT_GRUNT && mo->owner == consoleplayer) robot = mo;
        if (mo->type_id == MT_FORT && mo->owner != consoleplayer) enemy = mo;
    }
    CHECK(fort && robot && enemy);
    menu_t *hud = G_InitHUD(&app, "data/Z");
    M_CentreView(&app, fixed3_xy_to_fvec2(fort->core.position));
    P_MobjSetSelected(fort, true);
    hud->refresh(hud);
    menuitem_t *cancel = button(hud, 4702), *ok = button(hud, 4704),
               *up = button(hud, 4706), *down = button(hud, 4708);
    CHECK(cancel && ok && up && down && cancel->visible && up->enabled);
    int x = cancel->rect.x - 67, y = cancel->rect.y - 47;
    CHECK(ok->rect.x == x + 67 && ok->rect.y == y + 62);
    CHECK(up->rect.x == x + 50 && up->rect.y == y + 21);
    CHECK(down->rect.x == x + 50 && down->rect.y == y + 64);
    menuitem_t *radar = NULL;
    for (int i = 0; i < hud->numitems; ++i)
        if (hud->items[i].kind == MI_MINIMAP) radar = &hud->items[i];
    CHECK(radar && radar->rect.w <= 86 && radar->rect.h <= 104);
    CHECK(radar->rect.x >= 550 && radar->rect.y >= 288);
    CHECK(abs(radar->rect.w * level.height - radar->rect.h * level.width) <= level.width + level.height);
    V_BeginFrame(0xff0b0e10u);
    M_MenuDrawer(hud);
    if (scale == 2) shot("idle-fort");
    const StaticProductDefinition *products[32];
    int product_count = Z_ProductsFor(fort, products, 32);
    CHECK(product_count == 3 && products[0]->product_type == MT_UNIT_GATLING &&
          products[1]->product_type == MT_UNIT_GRUNT && products[2]->product_type == MT_UNIT_JEEP);
    /* Exercise every portrait/composite at maximum technology, then return
     * to the actual level-1 rating for the production/control checks. */
    fort->z.rating = 5000;
    hud->refresh(hud);
    product_count = Z_ProductsFor(fort, products, 32);
    /* Reviewed LEVEL01 preview pixels: native HEADFX portraits and complete
     * south-facing vehicles centred on authored part extents. Sample native
     * pixels so both UI scales must reproduce the same artwork/placement. */
    static const uint32_t previews[] = {
        0xfb289dbd, 0x2bd856a2, 0x9931e069, 0x722a7795,
        0xf2206ee6, 0xd5f751d3, 0x591d6d5a, 0x8b35563b, 0x96dd62ab, 0xf2206ee6,
        0x4e6120e8, 0xf412ec12, 0x39219295, 0xb4ae5ef0, 0xf3ab692b, 0x84e8c446, 0x4b51fc5d};
    CHECK(product_count == (int)(sizeof(previews) / sizeof(*previews)));
    for (int i = 0; i < product_count; ++i) {
        V_BeginFrame(0xff0b0e10u);
        M_MenuDrawer(hud);
        CHECK(picture_hash((irect_t){x + 6, y + 22, 48, 37}, scale) == previews[i]);
        char name[64];
        snprintf(name, sizeof(name), "product-%d-%dx", products[i]->product_type, scale);
        shot(name);
        click(hud, down);
    }
    fort->z.rating = 1000;
    hud->refresh(hud);
    int hp = fort->hp;
    fort->hp = fort->max_hp / 2;
    M_MenuDrawer(hud);
    CHECK(screens[0].pixels[(y + 5) * scale * screens[0].w + (x + 88) * scale] == 121);
    fort->hp = hp;
    /* Authored pressed art; its source cells are distinct, with no tint. */
    CHECK(ok->look[MS_PUSHED].cell == ok->look[MS_NORMAL].cell + 1);
    CHECK(!ok->color && !ok->fill && !ok->border);
    click(hud, down);
    click(hud, ok);
    CHECK(Z_ProductInProduction(fort) && !ok->visible);
    CHECK(!P_MobjIsSelected(fort));
    /* A second click on this same building reopens the running factory. */
    P_MobjSetSelected(fort, true);
    hud->refresh(hud);
    CHECK(ok->visible && !up->visible && !down->visible);
    G_ProductionTicker(5000);
    V_BeginFrame(0xff0b0e10u);
    M_MenuDrawer(hud);
    if (scale == 2) shot("busy-fort");
    int left = fort->production->time_left_ms;
    irect_t timer = {x + 88, y + 35, 24, 10};
    fort->production->time_left_ms = 3003; /* rate 3: 1.001 seconds truncates to 1 */
    M_MenuDrawer(hud);
    uint32_t one_second = picture_hash(timer, scale);
    fort->production->time_left_ms = 3000;
    M_MenuDrawer(hud);
    CHECK(picture_hash(timer, scale) == one_second);
    fort->production->time_left_ms = 2997;
    M_MenuDrawer(hud);
    CHECK(picture_hash(timer, scale) != one_second);
    fort->production->time_left_ms = left;
    fort->hp = 1;
    V_BeginFrame(0xff0b0e10u);
    M_MenuDrawer(hud);
    if (scale == 2) shot("stalled-fort");
    CHECK(picture_hash(timer, scale) != one_second);
    fort->hp = hp;
    click(hud, cancel);
    CHECK(!Z_ProductInProduction(fort) && up->visible && up->enabled);
    click(hud, cancel);
    CHECK(!cancel->visible && !P_MobjIsSelected(fort));
    P_MobjSetSelected(fort, true);
    hud->refresh(hud);
    click(hud, up); /* Grunt -> Gatling in the actual three-product cycle. */
    click(hud, ok);
    P_MobjSetSelected(fort, true);
    G_ProductionTicker(G_ModelProductTrainingTimeMs(
        G_ModelProductByUIId(NULL, Z_ProductId(MT_UNIT_GATLING))));
    hud->refresh(hud);
    CHECK(Z_GunReady(fort) && button(hud, 4718) == ok);
    V_BeginFrame(0xff0b0e10u);
    M_MenuDrawer(hud);
    if (scale == 2) shot("gun-ready");
    click(hud, ok);
    CHECK(hud->target == ok);
    SDL_Event escape = {.type = SDL_KEYDOWN};
    escape.key.keysym.sym = SDLK_ESCAPE;
    M_MenuResponder(hud, &app, &escape);
    CHECK(!hud->target && Z_GunReady(fort));
    click(hud, ok);
    CHECK(hud->target == ok);
    irect_t fort_cells = P_MobjCells(fort);
    bool placed = false;
    for (int gy = fort_cells.y; gy < fort_cells.y + 9 && !placed; ++gy)
        for (int gx = fort_cells.x; gx < fort_cells.x + 9 && !placed; ++gx) {
            fixed3_t position;
            if (!Z_GunPosition(fort, MT_UNIT_GATLING, (ivec2_t){gx,gy}, &position)) continue;
            fvec2_t screen;
            R_MapToScreen(&app, &level, gx + 0.5f, gy + 0.5f, &screen.x, &screen.y);
            hud->cursor = (ivec2_t){(int)screen.x,(int)screen.y};
            ok->routine(hud, ok, MA_TARGET);
            placed = !Z_GunReady(fort);
        }
    CHECK(placed && !hud->target);
    hud->refresh(hud);
    CHECK(button(hud, 4704) == ok);
    click(hud, cancel);
    CHECK(!Z_ProductInProduction(fort));
    fvec2_t camera = app.cam;
    app.cam = (fvec2_t){-10000,-10000};
    hud->refresh(hud);
    CHECK(cancel->rect.x >= 67 && cancel->rect.y >= 47);
    CHECK(ok->rect.x + ok->rect.w <= 544 && ok->rect.y + ok->rect.h <= 448);
    app.cam = camera;
    P_MobjSetSelected(fort, false);
    P_MobjSetSelected(enemy, true);
    hud->refresh(hud);
    CHECK(!ok->visible);
    P_MobjSetSelected(enemy, false);
    P_MobjSetSelected(robot, true);
    V_BeginFrame(0xff0b0e10u);
    M_MenuDrawer(hud);
    CHECK(!ok->visible);
    if (scale == 2) shot("robot-sidebar");
    /* All five authored portrait sets and the separate vehicle panel. */
    mobj_t shown = *robot;
    mobj_t *view[] = {&shown};
    hudview.units = view;
    hudview.unit_count = 1;
    const int types[] = {MT_UNIT_GRUNT, MT_UNIT_TOUGH, MT_UNIT_PYRO, MT_UNIT_SNIPER, MT_UNIT_PSYCHO, MT_UNIT_JEEP};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        shown.type_id = types[i];
        shown.z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
        V_BeginFrame(0xff0b0e10u);
        M_MenuDrawer(hud);
    }
    shown.type_id = MT_UNIT_APC;
    shown.z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    shown.core.position.z = 0;
    hud->refresh(hud);
    menuitem_t *eject = NULL;
    for (int i = 0; i < hud->numitems; ++i)
        if (hud->items[i].hotkey == SDLK_u) eject = &hud->items[i];
    CHECK(eject && eject->visible && eject->enabled && eject->routine);
    if (scale == 2) shot("vehicle-sidebar");
    G_ShutdownHUD();
    R_FreeSpriteCache(&cache);
    R_FreeSprite(&unit);
    R_FreeTileset(&tiles);
    P_FreeMobjList(&list);
    rts_game_model_destroy(model);
    V_FreeScreen();
    return 0;
}

int main(void) {
    if (test_hud(1) || test_hud(2)) return 1;
    puts("PASS: Z HUD, native level-1 options, 17 product previews at 1x/2x, production and reopening");
    return 0;
}
