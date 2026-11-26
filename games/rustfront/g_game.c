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

#define REFINERY(type, label, unit, hp) \
    {.id = type, .name = label, .sprite_name = unit "_body", .shadow_name = unit "_shadow", \
     .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE, .max_hp = hp, .footprint = {3, 2}}
#define EMPLACEMENT(type, label, unit, hp, rng, dmg, cool) \
    {.id = type, .name = label, .sprite_name = unit "_base", .shadow_name = unit "_shadow", \
     .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK | MF_TURRET, .max_hp = hp, .footprint = {1, 1}, \
     .turret = {.sprite_name = unit "_head", .turn_step = ANG45 / 8, .frame = 0, .fire_frame = TURRET_SHOT}, \
     .attack = {.range = FIXED_LIT(rng), .damage = dmg, .cooldown_ms = cool}}
#define TURRET_ON(unit, turn) \
    .turret = {.sprite_name = unit "_turret", .turn_step = ANG45 / (turn), .frame = 0, .fire_frame = TURRET_SHOT}
/* 50 ore a second from a field cell (w_rustfront.c); a load is `load` ore. */
#define HARVESTER(type, label, unit, hp, spd, load) \
    {.id = type, .name = label, .sprite_name = unit "_body", .shadow_name = unit "_shadow", \
     .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER, .speed = FIXED_LIT(spd), \
     .turn_step = ANG45 / 8, .max_hp = hp, .harvest = {.resources = {{.capacity = load}}}}

/* The heavy shell's height per flight step, by remaining fraction: a parabola, so a longer
 * shot climbs higher (the engine multiplies by the flight's step count). */
#define ARC(f) FIXED_LIT(0.2 * (f) * (1 - (f)))
static const fixed_t HEAVY_ARC[] = {ARC(0.0), ARC(0.125), ARC(0.25), ARC(0.375), ARC(0.5),
                                    ARC(0.625), ARC(0.75), ARC(0.875), ARC(1.0)};

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
     .attack = {.range = FIXED_LIT(6), .damage = 40, .cooldown_ms = 1200}},
    VEHICLE(MT_SCOUT, "Scout Buggy", "scout", "body", 220, 4.0, 4, 12, 500, .move_class = 0),
    VEHICLE(MT_TANK, "Battle Tank", "tank", "hull", 680, 2.5, 5, 60, 1600,
            .turret = {.sprite_name = "tank_turret", .turn_step = ANG45 / 8, .frame = 0, .fire_frame = TURRET_SHOT}),
    VEHICLE(MT_ARTILLERY, "Artillery", "artillery", "body", 300, 1.6, 9, 100, 3500, .move_class = 0),
    {.id = MT_HARVESTER, .name = "Ore Harvester", .sprite_name = "harvester_body", .shadow_name = "harvester_shadow",
     .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER, .speed = FIXED_LIT(1.6),
     .turn_step = ANG45 / 8, .max_hp = 900,
     /* 50 ore a second from a field cell (w_rustfront.c), 500 to a load. */
     .harvest = {.resources = {{.capacity = 500}}}},
    VEHICLE(MT_APC, "APC", "apc", "hull", 450, 3.2, 4, 18, 700,
            .turret = {.sprite_name = "apc_turret", .turn_step = ANG45 / 6, .frame = 0, .fire_frame = TURRET_SHOT}),
    VEHICLE(MT_RIFLEMAN, "Rifleman", "rifleman", "body", 50, 1.0, 3.5, 8, 900, .move_class = 0),
    VEHICLE(MT_ROCKET, "Rocket Soldier", "rocket", "body", 45, 0.9, 5, 40, 2500, .move_class = 0),
    /* Two heavy guns: each shot is a pair of shells (g_combat.c). */
    VEHICLE(MT_MASTODON, "Mastodon", "mastodon", "hull", 1400, 1.6, 5.5, 70, 2200, TURRET_ON("mastodon", 10)),
    /* The Uplink Centre's long arm: a pair of heavy missiles that splash, from well beyond
     * anything's sight; it cannot fire on what is close. */
    BUILDING(MT_UPLINK, "Uplink Centre", "uplink", 1400, 2, 2),
    {.id = MT_HAMMER, .name = "Hammer", .sprite_name = "hammer_body", .shadow_name = "hammer_shadow",
     .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK, .speed = FIXED_LIT(1.5), .turn_step = ANG45 / 6,
     .max_hp = 520, .attack = {.range = FIXED_LIT(11), .min_range = FIXED_LIT(2.5), .damage = 110, .cooldown_ms = 6500}},

    /* Scrap Clans: cheap, fast, fragile; a flamethrower and a rocket rig for punch. */
    BUILDING(MT_CL_HALL, "Clan Hall", "cl_hall", 2600, 3, 3),
    BUILDING(MT_CL_GENERATOR, "Generator", "cl_generator", 900, 2, 2),
    REFINERY(MT_CL_SMELTER, "Smelter", "cl_smelter", 1500),
    BUILDING(MT_CL_CAMP, "War Camp", "cl_camp", 900, 2, 2),
    BUILDING(MT_CL_YARD, "Scrap Yard", "cl_yard", 2000, 3, 3),
    EMPLACEMENT(MT_CL_TOWER, "Spike Tower", "cl_tower", 700, 5, 18, 350),
    VEHICLE(MT_CL_TRIKE, "Trike", "cl_trike", "body", 190, 4.8, 3.8, 10, 400, .move_class = 0),
    VEHICLE(MT_CL_TRUCK, "Gun Truck", "cl_truck", "hull", 360, 3.6, 4.5, 14, 400, TURRET_ON("cl_truck", 4)),
    VEHICLE(MT_CL_CRAWLER, "Scrap Crawler", "cl_crawler", "hull", 600, 2.4, 4.5, 80, 2000, TURRET_ON("cl_crawler", 6)),
    VEHICLE(MT_CL_RIG, "Rocket Rig", "cl_rig", "body", 260, 2.4, 8, 30, 4000, .move_class = 0),
    HARVESTER(MT_CL_HAULER, "Scrap Hauler", "cl_hauler", 700, 2.2, 450),
    VEHICLE(MT_CL_RAIDER, "Raider", "cl_raider", "body", 55, 1.3, 3.8, 6, 600, .move_class = 0),
    VEHICLE(MT_CL_TORCHER, "Torcher", "cl_torcher", "body", 80, 1.1, 2.6, 30, 1200, .move_class = 0),
    /* The Chop Shop's Wrecker drives its drums into the enemy and goes up with them. */
    BUILDING(MT_CL_SHOP, "Chop Shop", "cl_shop", 1100, 2, 2),
    VEHICLE(MT_CL_WRECKER, "Wrecker", "cl_wrecker", "body", 320, 4.2, 1.2, 500, 1000, .move_class = 0),

    /* Lumen Concord: few and expensive, tough, long beams; hover craft are quick. */
    BUILDING(MT_LC_SPIRE, "Spire", "lc_spire", 3600, 3, 3),
    BUILDING(MT_LC_FUSION, "Fusion Core", "lc_fusion", 1500, 2, 2),
    REFINERY(MT_LC_EXTRACTOR, "Extractor", "lc_extractor", 2200),
    BUILDING(MT_LC_ACADEMY, "Academy", "lc_academy", 1500, 2, 2),
    BUILDING(MT_LC_FOUNDRY, "Foundry", "lc_foundry", 2800, 3, 3),
    EMPLACEMENT(MT_LC_TOWER, "Beam Tower", "lc_tower", 900, 6, 50, 1800),
    VEHICLE(MT_LC_SKIMMER, "Skimmer", "lc_skimmer", "body", 260, 5.0, 4, 18, 700, .move_class = 0),
    VEHICLE(MT_LC_GLAIVE, "Glaive", "lc_glaive", "hull", 700, 3.0, 5.5, 80, 1900, TURRET_ON("lc_glaive", 6)),
    VEHICLE(MT_LC_PRISM, "Prism", "lc_prism", "body", 380, 2.2, 10, 160, 4200, .move_class = 0),
    HARVESTER(MT_LC_HARVESTER, "Hover Harvester", "lc_harvester", 1100, 2.0, 500),
    VEHICLE(MT_LC_WARDEN, "Warden", "lc_warden", "body", 100, 1.0, 4, 16, 1100, .move_class = 0),
    VEHICLE(MT_LC_LANCER, "Lancer", "lc_lancer", "body", 110, 0.8, 5.5, 60, 2600, .move_class = 0),
    /* The Sanctum's Aegis: its field takes a third off every hit on the Concord's units near it
     * and mends their vehicles; its own beam is light. */
    BUILDING(MT_LC_SANCTUM, "Sanctum", "lc_sanctum", 1600, 2, 2),
    VEHICLE(MT_LC_AEGIS, "Aegis", "lc_aegis", "body", 750, 2.6, 5, 22, 1000, .move_class = 0),
    {.id = MT_FX_HIT, .name = "Impact", .sprite_name = "fx_hit", .traits = MF_RENDERABLE | MF_NOBLOCKMAP, .max_hp = 1},
    {.id = MT_FX_BOOM, .name = "Explosion", .sprite_name = "fx_boom", .traits = MF_RENDERABLE | MF_NOBLOCKMAP, .max_hp = 1},
    {.id = MT_FX_BIGBOOM, .name = "Large explosion", .sprite_name = "fx_bigboom",
     .traits = MF_RENDERABLE | MF_NOBLOCKMAP, .max_hp = 1},
    {.id = MT_FX_SMOKE, .name = "Smoke", .sprite_name = "fx_smoke", .traits = MF_RENDERABLE | MF_NOBLOCKMAP, .max_hp = 1},
    {.id = MT_FX_PLUME, .name = "Smoke plume", .sprite_name = "fx_plume", .traits = MF_RENDERABLE | MF_NOBLOCKMAP, .max_hp = 1},
    {.id = MT_FX_FIRE, .name = "Fire", .sprite_name = "fx_fire", .traits = MF_RENDERABLE | MF_NOBLOCKMAP, .max_hp = 1},
    /* Timed flight: `step` cells a tic toward where the target stood. The heavy shell arcs. */
    {.id = MT_SHELL, .name = "Shell", .sprite_name = "fx_shell", .traits = MF_RENDERABLE | MF_MISSILE, .max_hp = 1,
     .missile = {.step = FIXED_LIT(0.6), .period_ms = RTS_TICK_MS, .timed = true}},
    {.id = MT_HEAVY_SHELL, .name = "Heavy shell", .sprite_name = "fx_shell", .traits = MF_RENDERABLE | MF_MISSILE,
     .max_hp = 1, .missile = {.step = FIXED_LIT(0.4), .period_ms = RTS_TICK_MS, .timed = true,
                              .arc = HEAVY_ARC, .arc_count = (int)(sizeof(HEAVY_ARC) / sizeof(*HEAVY_ARC))}},
    {.id = MT_MISSILE, .name = "Rocket", .sprite_name = "fx_rocket", .traits = MF_RENDERABLE | MF_MISSILE, .max_hp = 1,
     .missile = {.step = FIXED_LIT(0.3), .period_ms = RTS_TICK_MS, .timed = true}},
    /* A bolt of light is quick; a heavy one splashes like a heavy shell. A flame gout is slow. */
    {.id = MT_BOLT, .name = "Bolt", .sprite_name = "fx_bolt", .traits = MF_RENDERABLE | MF_MISSILE, .max_hp = 1,
     .missile = {.step = FIXED_LIT(0.9), .period_ms = RTS_TICK_MS, .timed = true}},
    {.id = MT_HEAVY_BOLT, .name = "Heavy bolt", .sprite_name = "fx_bolt", .traits = MF_RENDERABLE | MF_MISSILE,
     .max_hp = 1, .missile = {.step = FIXED_LIT(0.7), .period_ms = RTS_TICK_MS, .timed = true}},
    {.id = MT_HEAVY_MISSILE, .name = "Heavy missile", .sprite_name = "fx_rocket", .traits = MF_RENDERABLE | MF_MISSILE,
     .max_hp = 1, .missile = {.step = FIXED_LIT(0.35), .period_ms = RTS_TICK_MS, .timed = true,
                              .arc = HEAVY_ARC, .arc_count = (int)(sizeof(HEAVY_ARC) / sizeof(*HEAVY_ARC))}},
    {.id = MT_FLAME, .name = "Flame", .sprite_name = "fx_flame", .traits = MF_RENDERABLE | MF_MISSILE, .max_hp = 1,
     .missile = {.step = FIXED_LIT(0.25), .period_ms = RTS_TICK_MS, .timed = true}},
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
