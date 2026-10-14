#include "engine.h"
#include "z_local.h"
#include "info.h"

/* A grenade's height per flight step, by the fraction of its flight left
 * (ZED.EXE scales its own table at 0x148b9c by distance). */
static const fixed_t grenade_arc[] = {
    0, FIXED_ONE / 48, FIXED_ONE / 32, FIXED_ONE / 26, FIXED_ONE / 24,
    FIXED_ONE / 26, FIXED_ONE / 32, FIXED_ONE / 48, 0,
};
static const uint8_t grenade_blast[] = {50, 75, 50, 75, 100, 75, 50, 75, 50};

/* Objects and buildings stand still; units (units.inc) walk and fight. */
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
#define Z_UNIT(t, type, label, kind, mobile, hp, dmg, spd, reach, reload, sprite, turn, frames, idle, \
               dead, gun) \
    {.id = MT_UNIT_##type, .name = label, .sprite_name = Z_SHEET_##kind(type), \
     .traits = MF_SELECTABLE | MF_RENDERABLE | ((mobile) ? MF_MOBILE : MF_TURRET) | \
               ((dmg) ? MF_ATTACK : 0), \
     .turret = {.sprite_name = (gun) ? #type "_TURRET" : NULL, .turn_step = Z_TURRET_TURN, .frame = 0, \
                .fire_frame = 1}, \
     .speed = FIXED_ONE * (spd) / 10, .max_hp = hp, .native_type_id = t, \
     .attack = {.range = FIXED_FROM_INT(reach), .damage = dmg, .cooldown_ms = reload}},
#include "units.inc"
#undef Z_UNIT
    {.id = MT_FLAG, .name = "Flag", .sprite_name = "FLAG", .traits = MF_RENDERABLE | MF_NOBLOCKMAP,
     .max_hp = 1},
    /* Lobbed along an arc; it bursts where it lands, hurting what stands
     * within a cell (0x8476c; the radius and falloff are authored). */
    {.id = MT_GRENADE, .name = "Grenade", .sprite_name = "SPRITES.RSC",
     .traits = MF_RENDERABLE | MF_MISSILE, .max_hp = 1,
     .missile = {.step = FIXED_ONE / 6, .period_ms = RTS_TICK_MS, .timed = true,
                 .arc = grenade_arc, .arc_count = sizeof(grenade_arc) / sizeof(*grenade_arc)},
     .blast = {grenade_blast, 3, NULL, 0}},
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
const uint16_t g_debug_enemy_type = MT_UNIT_GRUNT;
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
