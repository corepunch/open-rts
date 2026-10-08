#include "engine.h"
#include "7legion.h"
#include "info.h"

bool sl_load_map(const char *map_path, level_t *out);
bool sl_load_assets(const char *data_root, const level_t *map,
                    const char *sprite_name, tileset_t *tileset, spritesheet_t *unit_sprite);
int  sl_load_initial_units(const char *map_path);
bool sl_load_runtime_sprites(const char *data_root, const level_t *map,
                             mobj_t *const *units, int unit_count, spritecache_t *cache);

/* mobj_t types defined in 7th Legion based on sprites present in data/7LEGION/GFX/ */
static const mobjtype_t ACTOR_TYPES[] = {
    {
        .id          = 1,
        .name        = "Trooper",
        .sprite_name = "GFX/LTROOP.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_ATTACK,
        .speed       = 4.0f,
        .max_hp      = 100,
        .attack = { .range = 5.0f, .damage = 15, .cooldown_ms = 800 },
    },
    {
        .id          = 2,
        .name        = "Slave",
        .sprite_name = "GFX/SLAVEN1.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_HARVESTER,
        .speed       = 3.5f,
        .max_hp      = 60,
        .harvest     = { .resources = { { .capacity = 50 } } },
    },
    {
        .id          = 3,
        .name        = "Spider Mech",
        .sprite_name = "GFX/SPIDER.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_ATTACK,
        .speed       = 3.0f,
        .max_hp      = 300,
        .attack = { .range = 7.0f, .damage = 35, .cooldown_ms = 1200 },
    },
    {
        .id          = 4,
        .name        = "Tank",
        .sprite_name = "GFX/TANKBASE.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_ATTACK,
        .speed       = 4.5f,
        .max_hp      = 500,
        .attack = { .range = 8.0f, .damage = 50, .cooldown_ms = 1500 },
    },
    {
        .id          = 5,
        .name        = "Rock Mech",
        .sprite_name = "GFX/ROCKMECH.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_ATTACK,
        .speed       = 2.5f,
        .max_hp      = 800,
        .attack = { .range = 6.0f, .damage = 70, .cooldown_ms = 2000 },
    },
    {
        .id          = 6,
        .name        = "Truck",
        .sprite_name = "GFX/TRUCK.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_HARVESTER,
        .speed       = 5.0f,
        .max_hp      = 200,
        .harvest     = { .resources = { { .capacity = 100 } } },
    },
    {
        .id          = 7,
        .name        = "Mobile Base",
        .sprite_name = "GFX/MOBBASE.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_RESOURCE_BASE,
        .speed       = 2.5f,
        .max_hp      = 1000,
    },
#define SL_BUILDING(native, type, asset, label, hp, cost, ticks, w, h) \
    {.id = MT_##type, .name = label, .sprite_name = asset, \
     .traits = MF_SELECTABLE | MF_RENDERABLE, .max_hp = hp, \
     .footprint = {w, h}, .corner_anchor = true},
#include "buildings.inc"
#undef SL_BUILDING
};


/* The sidebar: money in the status strip, and the list of what the
 * selected building makes. */
enum { HUD_STATUS, HUD_MONEY, HUD_PRODUCTS, HUD_PAGE, NUMHUD };
static menuitem_t hud_items[NUMHUD] = {
    [HUD_STATUS] = {.visible = true, .rect = {480, 0, 160, 28},
                    .fill = 0xff080b0fu, .border = 0xff7e847eu},
    [HUD_MONEY] = {.visible = true, .rect = {630, 3, 0, 22}, .ink = 0xffe6d750u,
                   .align = MALIGN_RIGHT, .ownerdraw = HU_DrawCounter},
    [HUD_PRODUCTS] = {.kind = MI_LIST, .visible = true, .enabled = true, .rect = {480, 28, 160, 416},
                      .row_height = 32, .value = -1, .fill = 0xff0c1216u,
                      .routine = HU_ProductList, .ownerdraw = HU_DrawProducts,
                      .drawtarget = HU_DrawProductPlacement},
    [HUD_PAGE] = {.kind = MI_BUTTON, .visible = true, .enabled = true, .rect = {480, 444, 160, 36},
                  .fill = 0xff0c1216u, .link = HUD_PRODUCTS, .routine = HU_ProductPage,
                  .ownerdraw = HU_DrawProductPage},
};
static menu_t hud = {.items = hud_items, .numitems = NUMHUD, .itemOn = -1,
                     .refresh = HU_RefreshProducts};

/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id            = "7legion";
const char *const g_game_name          = "7th Legion";
const char *const g_game_default_root  = "data/7LEGION";
const char *const g_game_default_map   = "DATA/MAPT.000";
const char *const g_game_default_sprite = "GFX/LTROOP.BIM";
const int g_cell_w = TILE_W;
const int g_cell_h = TILE_H;
const uint16_t g_debug_enemy_type = 1;
const gameinfo_t *gameinfo = &game_info;
const mobjtype_t *const actor_types =
    (const mobjtype_t *)ACTOR_TYPES;
const int num_actor_types =
    (int)(sizeof(ACTOR_TYPES) / sizeof(ACTOR_TYPES[0]));

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

void G_InitGame(void) {
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    return sl_load_map(path, out);
}

bool W_LoadAssets(const char *root, const level_t *map, const char *sprite,
                  tileset_t *tileset, spritesheet_t *unit_sprite) {
    return sl_load_assets(root, map, sprite, tileset, unit_sprite);
}

int P_LoadThings(const char *path) {
    return sl_load_initial_units(path);
}

bool R_InitSprites(const char *root, const level_t *map,
                   mobj_t *const *mobjs, int count, spritecache_t *cache) {
    return sl_load_runtime_sprites(root, map, mobjs, count, cache);
}

bool HU_LoadFont(const char *root, bitmapfont_t *font) {
    (void)root; (void)font;
    return false;
}

void  G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                      hudtext_t *hud, float dt) {
    (void)map; (void)mobjs; (void)count;
    (void)hud; (void)dt;
}

menu_t *G_InitHUD(app_t *app, const char *data_root) {
    HU_InitProducts(&hud, data_root);
    hud.app = app;
    return &hud;
}

void G_ShutdownHUD(void) {
    hudview = (hudview_t){0};
}

bool G_UpdateProduction(level_t *map, mobj_t *const *units, int *unit_count, float dt) {
    (void)map; (void)units; (void)unit_count;
    return G_ProductionTicker(dt);
}

irect_t G_WorldViewport(const app_t *app) {
    return (irect_t){0, 28 * app->win.h / 480, 480 * app->win.w / 640, 452 * app->win.h / 480};
}
