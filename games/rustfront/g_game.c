#include "engine.h"
#include "info.h"
#include "rustfront.h"

/* Gameplay rows. Speeds are cells (200 cm) per second; sprite names are the packed sheets
 * (art/pack8.py): <unit>_<layer>, with a <unit>_shadow sheet drawn under each. */
#define BUILDING(type, label, unit, hp, w, h) \
    {.id = type, .name = label, .sprite_name = unit "_body", .shadow_name = unit "_shadow", \
     .traits = MF_SELECTABLE | MF_RENDERABLE, .max_hp = hp, .footprint = {w, h}}
#define VEHICLE(type, label, unit, sheet, hp, spd, rng, dmg, cool, ...) \
    {.id = type, .name = label, .sprite_name = unit "_" sheet, .shadow_name = unit "_shadow", \
     .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK, .speed = FIXED_LIT(spd), \
     .turn_step = ANG45 / 6, .max_hp = hp, \
     .attack = {.range = FIXED_LIT(rng), .damage = dmg, .cooldown_ms = cool}, __VA_ARGS__}

static const mobjtype_t ACTOR_TYPES[] = {
    BUILDING(MT_HQ, "Command Center", "hq", 3000, 3, 3),
    BUILDING(MT_POWER, "Power Plant", "power", 1200, 2, 2),
    {.id = MT_REFINERY, .name = "Ore Refinery", .sprite_name = "refinery_body", .shadow_name = "refinery_shadow",
     .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE, .max_hp = 1800, .footprint = {3, 2}},
    BUILDING(MT_BARRACKS, "Barracks", "barracks", 1200, 2, 2),
    BUILDING(MT_FACTORY, "War Factory", "factory", 2400, 3, 3),
    {.id = MT_GUN_TURRET, .name = "Gun Turret", .sprite_name = "turret_base", .shadow_name = "turret_shadow",
     .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK | MF_TURRET, .max_hp = 800, .footprint = {1, 1},
     .turret = {.sprite_name = "turret_head", .turn_step = ANG45 / 8, .frame = 0, .fire_frame = TURRET_SHOT},
     .attack = {.range = FIXED_LIT(6), .damage = 45, .cooldown_ms = 1200}},
    VEHICLE(MT_SCOUT, "Scout Buggy", "scout", "body", 220, 4.0, 4, 12, 500, .move_class = 0),
    VEHICLE(MT_TANK, "Battle Tank", "tank", "hull", 600, 2.5, 5, 60, 1600,
            .turret = {.sprite_name = "tank_turret", .turn_step = ANG45 / 8, .frame = 0, .fire_frame = TURRET_SHOT}),
    VEHICLE(MT_ARTILLERY, "Artillery", "artillery", "body", 300, 1.6, 9, 120, 3500, .move_class = 0),
    {.id = MT_HARVESTER, .name = "Ore Harvester", .sprite_name = "harvester_body", .shadow_name = "harvester_shadow",
     .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER, .speed = FIXED_LIT(1.6),
     .turn_step = ANG45 / 8, .max_hp = 900,
     /* 50 ore a second from a field cell (w_rustfront.c), 500 to a load. */
     .harvest = {.resources = {{.capacity = 500}}}},
    VEHICLE(MT_APC, "APC", "apc", "hull", 450, 3.2, 4, 18, 700,
            .turret = {.sprite_name = "apc_turret", .turn_step = ANG45 / 6, .frame = 0, .fire_frame = TURRET_SHOT}),
    VEHICLE(MT_RIFLEMAN, "Rifleman", "rifleman", "body", 50, 1.0, 3.5, 8, 900, .move_class = 0),
    VEHICLE(MT_ROCKET, "Rocket Soldier", "rocket", "body", 45, 0.9, 5, 40, 2500, .move_class = 0),
    {.id = MT_FX_HIT, .name = "Impact", .sprite_name = "fx_hit", .traits = MF_RENDERABLE | MF_NOBLOCKMAP, .max_hp = 1},
    {.id = MT_FX_BOOM, .name = "Explosion", .sprite_name = "fx_boom", .traits = MF_RENDERABLE | MF_NOBLOCKMAP, .max_hp = 1},
    {.id = MT_FX_BIGBOOM, .name = "Large explosion", .sprite_name = "fx_bigboom",
     .traits = MF_RENDERABLE | MF_NOBLOCKMAP, .max_hp = 1},
};

/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id             = "rustfront";
const char *const g_game_name           = "Rustfront";
const char *const g_game_default_root   = "games/rustfront";
const char *const g_game_default_map    = "maps/skirmish.txt";
const char *const g_game_default_sprite = "rifleman_body";
const int g_cell_w = 48;
const int g_cell_h = 32;
const uint16_t g_debug_enemy_type = MT_TANK;
const gameinfo_t *gameinfo = &game_info;
const mobjtype_t *const actor_types = ACTOR_TYPES;
const int num_actor_types = (int)(sizeof(ACTOR_TYPES) / sizeof(ACTOR_TYPES[0]));

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

void G_InitGame(void) {
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    return RF_LoadLevel(path, out);
}

bool W_LoadAssets(const char *root, const level_t *map, const char *sprite,
                  tileset_t *tileset, spritesheet_t *unit_sprite) {
    (void)map; (void)sprite; (void)unit_sprite;
    return RF_LoadAssets(root, tileset);
}

int P_LoadThings(const char *path) {
    return RF_LoadThings(path);
}

bool R_InitSprites(const char *root, const level_t *map,
                   mobj_t *const *mobjs, int count, spritecache_t *cache) {
    (void)map; (void)mobjs; (void)count;
    return RF_LoadSprites(root, cache);
}

void G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                     hudtext_t *hud, int dt_ms) {
    (void)hud; (void)dt_ms;
    RF_MissionTicker(map, mobjs, *count);
}

menu_t *G_InitHUD(app_t *app, const char *data_root) {
    return RF_InitHUD(app, data_root);
}

void G_ShutdownHUD(void) {
    RF_ShutdownHUD();
}

bool G_UpdateProduction(level_t *map, mobj_t *const *units, int *unit_count, int dt_ms) {
    (void)map; (void)units; (void)unit_count;
    return G_ProductionTicker(dt_ms);
}

irect_t G_WorldViewport(const app_t *app) {
    /* Everything left of the 160-pixel sidebar (hu_rustfront.c), in the 640 x 480 layout. */
    return (irect_t){0, 0, RF_SIDEBAR_X * app->win.w / 640, app->win.h};
}
