#include "engine.h"
#include "z_local.h"
#include "info.h"

/* Static for now: the native sizes and still pictures, no behaviour yet. */
static const mobjtype_t ACTOR_TYPES[] = {
#define Z_OBJECT(native, type, label, sprite, flags, w, h, dead) \
    {.id = MT_##type, .name = label, .sprite_name = "SPRITES.RSC", .traits = MF_RENDERABLE, \
     .max_hp = 100, .native_type_id = native},
#include "objects.inc"
#undef Z_OBJECT
    {.id = MT_FORT, .name = "Fort", .sprite_name = "FORT",
     .traits = MF_SELECTABLE | MF_RENDERABLE, .max_hp = 2000, .footprint = {10, 11}},
#define Z_BUILDING(native, type, label, w, h, ...) \
    {.id = MT_##type, .name = label, .sprite_name = #type, \
     .traits = MF_SELECTABLE | MF_RENDERABLE, .max_hp = 2000, .footprint = {w, h}, \
     .native_type_id = native},
#include "buildings.inc"
#undef Z_BUILDING
};

/* No sidebar yet: the world takes the window. */
static menu_t hud = {.itemOn = -1};

/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id            = "z";
const char *const g_game_name          = "Z";
const char *const g_game_default_root  = "data/Z";
const char *const g_game_default_map   = "LEVEL01/LEVEL01.MAP";
const char *const g_game_default_sprite = "SPRITES.RSC";
const int g_cell_w = Z_TILE;
const int g_cell_h = Z_TILE;
const uint16_t g_debug_enemy_type = MT_JEEP;
const gameinfo_t *gameinfo = &game_info;
const mobjtype_t *const actor_types = ACTOR_TYPES;
const int num_actor_types = (int)(sizeof(ACTOR_TYPES) / sizeof(ACTOR_TYPES[0]));

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

void G_InitGame(void) {
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    return Z_LoadLevel(path, out);
}

bool W_LoadAssets(const char *root, const level_t *map, const char *sprite,
                  tileset_t *tileset, spritesheet_t *unit_sprite) {
    (void)sprite;
    return Z_LoadAssets(root, map, tileset, unit_sprite);
}

int P_LoadThings(const char *path) {
    (void)path;
    return Z_LoadThings();
}

bool R_InitSprites(const char *root, const level_t *map,
                   mobj_t *const *mobjs, int count, spritecache_t *cache) {
    (void)root; (void)mobjs; (void)count;
    return Z_InitSprites(map, cache);
}

bool HU_LoadFont(const char *root, bitmapfont_t *font) {
    (void)root; (void)font;
    return false;
}

void G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                     hudtext_t *text, int dt_ms) {
    (void)map; (void)mobjs; (void)count; (void)text; (void)dt_ms;
}

menu_t *G_InitHUD(app_t *app, const char *data_root) {
    (void)data_root;
    hud.app = app;
    return &hud;
}

void G_ShutdownHUD(void) {
    hudview = (hudview_t){0};
}

bool G_UpdateProduction(level_t *map, mobj_t *const *units, int *unit_count, int dt_ms) {
    (void)map; (void)units; (void)unit_count;
    return G_ProductionTicker(dt_ms);
}

irect_t G_WorldViewport(const app_t *app) {
    return (irect_t){0, 0, app->win.w, app->win.h};
}
