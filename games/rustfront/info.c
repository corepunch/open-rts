#include "engine.h"
#include "info.h"
#include "rustfront.h"

const char *const sprnames[NUMSPRITES] = {
    "hq_body", "power_body", "refinery_body", "barracks_body", "factory_body", "turret_base",
    "scout_body", "tank_hull", "artillery_body", "harvester_body", "apc_hull",
    "rifleman_body", "rocket_body",
    "fx_hit", "fx_boom", "fx_bigboom", "fx_shell", "fx_rocket", "fx_smoke", "fx_blast",
    "fx_plume", "fx_fire", "turret_build",
    "mastodon_hull",
    "cl_hall_body", "cl_generator_body", "cl_smelter_body", "cl_camp_body", "cl_yard_body", "cl_tower_base", "cl_tower_build",
    "cl_trike_body", "cl_truck_hull", "cl_crawler_hull", "cl_rig_body", "cl_hauler_body", "cl_raider_body", "cl_torcher_body",
    "lc_spire_body", "lc_fusion_body", "lc_extractor_body", "lc_academy_body", "lc_foundry_body", "lc_tower_base", "lc_tower_build",
    "lc_skimmer_body", "lc_glaive_hull", "lc_prism_body", "lc_harvester_body", "lc_warden_body", "lc_lancer_body",
    "fx_bolt", "fx_flame", "fx_zap",
    "uplink_body", "hammer_body", "cl_shop_body", "cl_wrecker_body", "lc_sanctum_body", "lc_aegis_body",
    "crane_body", "phantom_hull", "kestrel_body", "sentry_truck_body", "sentry_base",
    "cl_drill_body", "cl_derrick_body", "cl_sapper_body", "cl_mine_body", "cl_crusher_body", "cl_buzzard_body",
    "lc_strider_body", "lc_seraph_body", "lc_cantor_body",
};

/* {sprite, frame, count, tics, action, next, group}: group 2 plays while moving, 3 attacking.
 * An action runs on every frame of a run, so a shot is two states: the firing frame, then
 * the settle. */
/* A structure's sheet holds its animation (n frames), the scorched copies (n more) and the
 * construction stages; a site holds a stage (tics -1) and g_build.c steps it. */
#define STRUCTURE(spr, self, damaged, build, n, tics) \
    [self] = {spr, 0, n, tics, NULL, self, 0, NULL}, \
    [damaged] = {spr, n, n, tics, NULL, damaged, 0, NULL}, \
    [build] = {spr, 2 * (n), BUILD_STAGE_COUNT, -1, NULL, build, 0, NULL}
/* A hull under a turret: its treads step while moving. The turret shows TURRET_SHOT plus the
 * body's frame offset into the firing run, so FIRE is body frame 0 and FIRE2 frame 1. */
#define TURRETED(spr, stnd, move, fire, fire2, settle) \
    [stnd]  = {spr, VEH_STAND, 1, 1, A_Look, stnd, 0, NULL}, \
    [move]  = {spr, VEH_STAND, 2, 3, NULL, move, 2, NULL}, \
    [fire]  = {spr, VEH_STAND, 1, 3, A_Attack, fire2, 3, NULL}, \
    [fire2] = {spr, VEH_STEP, 1, settle, NULL, stnd, 3, NULL}
/* A vehicle whose gun is part of its body. */
#define GUNNED(spr, stnd, move, fire, fire2, settle) \
    [stnd]  = {spr, VEH_STAND, 1, 1, A_Look, stnd, 0, NULL}, \
    [move]  = {spr, VEH_STAND, 2, 3, NULL, move, 2, NULL}, \
    [fire]  = {spr, VEH_SHOT, 1, 3, A_Attack, fire2, 3, NULL}, \
    [fire2] = {spr, VEH_STAND, 1, settle, NULL, stnd, 3, NULL}
/* A defensive emplacement: it builds on its own sheet, then stands, fires and settles while its
 * head (drawn on the base sheet) shows the frames. */
#define EMPLACEMENT(spr, build_spr, stnd, fire, fire2, build, settle) \
    [build] = {build_spr, 0, BUILD_STAGE_COUNT, -1, NULL, build, 0, NULL}, \
    [stnd]  = {spr, VEH_STAND, 1, 1, A_Look, stnd, 0, NULL}, \
    [fire]  = {spr, VEH_STAND, 1, 3, A_Attack, fire2, 3, NULL}, \
    [fire2] = {spr, VEH_STEP, 1, settle, NULL, stnd, 3, NULL}
#define HAULER(spr, stnd, move) \
    [stnd] = {spr, VEH_STAND, 1, -1, NULL, stnd, 0, NULL}, \
    [move] = {spr, VEH_STAND, 2, 3, NULL, move, 2, NULL}
/* A projectile: flight, then the blast whose first frame does the damage. */
#define PROJECTILE(spr, fly, hit, hit2, blast, n, tics) \
    [fly]  = {spr, 0, 1, 1, NULL, fly, 0, NULL}, \
    [hit]  = {blast, 0, 1, tics, A_RfImpact, hit2, 0, NULL}, \
    [hit2] = {blast, 1, (n) - 1, tics, NULL, S_NULL, 0, NULL}
/* A flyer's rotor or engine never stops: it stands and moves on both frames. */
#define FLYER(spr, stnd, move, fire, fire2, settle) \
    [stnd]  = {spr, VEH_STAND, 2, 2, A_Look, stnd, 0, NULL}, \
    [move]  = {spr, VEH_STAND, 2, 2, NULL, move, 2, NULL}, \
    [fire]  = {spr, VEH_SHOT, 1, 3, A_Attack, fire2, 3, NULL}, \
    [fire2] = {spr, VEH_STEP, 1, settle, NULL, stnd, 3, NULL}
/* A vehicle with no gun of its own that rams: its running gear lurches into the blow. */
#define RAMMER(spr, stnd, move, fire, fire2, settle) \
    [stnd]  = {spr, VEH_STAND, 1, 1, A_Look, stnd, 0, NULL}, \
    [move]  = {spr, VEH_STAND, 2, 3, NULL, move, 2, NULL}, \
    [fire]  = {spr, VEH_STEP, 1, 3, A_Attack, fire2, 3, NULL}, \
    [fire2] = {spr, VEH_STAND, 1, settle, NULL, stnd, 3, NULL}
/* Idle looks around slowly (RF_MobjTicker keeps the lookout every tic), the run cycle is one
 * stride, a shot is the firing frame then the recoil, and death is the fall then the body. */
#define SOLDIER(spr, stnd, walk, fire, fire2, die, corpse) \
    [stnd]   = {spr, INF_IDLE, INF_IDLE_FRAMES, 12, NULL, stnd, 0, NULL}, \
    [walk]   = {spr, INF_WALK, INF_WALK_FRAMES, 3, NULL, walk, 2, NULL}, \
    [fire]   = {spr, INF_FIRE, 1, 3, A_Attack, fire2, 3, NULL}, \
    [fire2]  = {spr, INF_FIRE + 1, 1, 5, NULL, stnd, 3, NULL}, \
    [die]    = {spr, INF_DIE, INF_DIE_FRAMES, 3, NULL, corpse, 0, NULL}, \
    [corpse] = {spr, INF_DIE + INF_DIE_FRAMES - 1, 1, 300, NULL, S_NULL, 0, NULL}

const state_t states[NUMSTATES] = {
    [S_NULL] = {0, 0, 0, -1, NULL, S_NULL, 0, NULL},
    /* Buildings loop their sheet: the radar turns, lights blink, the reactor pulses, the flare flickers. */
    STRUCTURE(SPR_HQ, S_HQ, S_HQ_DAMAGED, S_HQ_BUILD, 8, 6),
    STRUCTURE(SPR_POWER, S_POWER, S_POWER_DAMAGED, S_POWER_BUILD, 4, 8),
    STRUCTURE(SPR_REFINERY, S_REFINERY, S_REFINERY_DAMAGED, S_REFINERY_BUILD, 4, 5),
    STRUCTURE(SPR_BARRACKS, S_BARRACKS, S_BARRACKS_DAMAGED, S_BARRACKS_BUILD, 2, 15),
    STRUCTURE(SPR_FACTORY, S_FACTORY, S_FACTORY_DAMAGED, S_FACTORY_BUILD, 2, 15),
    /* The gun turret builds on its own sheet, so its head (drawn on the base sheet) waits. */
    [S_GUN_TURRET_BUILD] = {SPR_GUN_TURRET_BUILD, 0, BUILD_STAGE_COUNT, -1, NULL, S_GUN_TURRET_BUILD, 0, NULL},
    /* The emplacement never moves: stand, the shot, the settle (its head shows the frames). */
    [S_GUN_TURRET] = {SPR_GUN_TURRET, VEH_STAND, 1, 1, A_Look, S_GUN_TURRET, 0, NULL},
    [S_GUN_TURRET_FIRE] = {SPR_GUN_TURRET, VEH_STAND, 1, 3, A_Attack, S_GUN_TURRET_FIRE2, 3, NULL},
    [S_GUN_TURRET_FIRE2] = {SPR_GUN_TURRET, VEH_STEP, 1, 12, NULL, S_GUN_TURRET, 3, NULL},
    GUNNED(SPR_SCOUT, S_SCOUT, S_SCOUT_MOVE, S_SCOUT_FIRE, S_SCOUT_FIRE2, 6),
    TURRETED(SPR_TANK, S_TANK, S_TANK_MOVE, S_TANK_FIRE, S_TANK_FIRE2, 9),
    GUNNED(SPR_ARTILLERY, S_ARTILLERY, S_ARTILLERY_MOVE, S_ARTILLERY_FIRE, S_ARTILLERY_FIRE2, 12),
    [S_HARVESTER] = {SPR_HARVESTER, VEH_STAND, 1, -1, NULL, S_HARVESTER, 0, NULL},
    [S_HARVESTER_MOVE] = {SPR_HARVESTER, VEH_STAND, 2, 3, NULL, S_HARVESTER_MOVE, 2, NULL},
    TURRETED(SPR_APC, S_APC, S_APC_MOVE, S_APC_FIRE, S_APC_FIRE2, 6),
    SOLDIER(SPR_RIFLEMAN, S_RIFLEMAN, S_RIFLEMAN_WALK, S_RIFLEMAN_FIRE, S_RIFLEMAN_FIRE2, S_RIFLEMAN_DIE, S_RIFLEMAN_CORPSE),
    SOLDIER(SPR_ROCKET, S_ROCKET, S_ROCKET_WALK, S_ROCKET_FIRE, S_ROCKET_FIRE2, S_ROCKET_DIE, S_ROCKET_CORPSE),
    [S_FX_HIT] = {SPR_FX_HIT, 0, 5, 2, NULL, S_NULL, 0, NULL},
    [S_FX_BOOM] = {SPR_FX_BOOM, 0, 10, 3, NULL, S_NULL, 0, NULL},
    [S_FX_BIGBOOM] = {SPR_FX_BIGBOOM, 0, 12, 3, NULL, S_NULL, 0, NULL},
    [S_FX_SMOKE] = {SPR_FX_SMOKE, 0, 5, 3, NULL, S_NULL, 0, NULL},
    [S_FX_PLUME] = {SPR_FX_PLUME, 0, 8, 4, NULL, S_NULL, 0, NULL},
    [S_FX_FIRE] = {SPR_FX_FIRE, 0, 6, 3, NULL, S_NULL, 0, NULL},
    /* A projectile flies in its first state; arriving, its death run is the blast, whose
     * first frame does the damage. */
    [S_SHELL] = {SPR_FX_SHELL, 0, 1, 1, NULL, S_SHELL, 0, NULL},
    [S_SHELL_HIT] = {SPR_FX_BLAST, 0, 1, 2, A_RfImpact, S_SHELL_HIT2, 0, NULL},
    [S_SHELL_HIT2] = {SPR_FX_BLAST, 1, 6, 2, NULL, S_NULL, 0, NULL},
    [S_HEAVY_SHELL] = {SPR_FX_SHELL, 0, 1, 1, NULL, S_HEAVY_SHELL, 0, NULL},
    [S_HEAVY_SHELL_HIT] = {SPR_FX_BOOM, 0, 1, 3, A_RfImpact, S_HEAVY_SHELL_HIT2, 0, NULL},
    [S_HEAVY_SHELL_HIT2] = {SPR_FX_BOOM, 1, 9, 3, NULL, S_NULL, 0, NULL},
    [S_MISSILE] = {SPR_FX_ROCKET, 0, 1, 1, NULL, S_MISSILE, 0, NULL},
    [S_MISSILE_HIT] = {SPR_FX_BLAST, 0, 1, 2, A_RfImpact, S_MISSILE_HIT2, 0, NULL},
    [S_MISSILE_HIT2] = {SPR_FX_BLAST, 1, 6, 2, NULL, S_NULL, 0, NULL},
    TURRETED(SPR_MASTODON, S_MASTODON, S_MASTODON_MOVE, S_MASTODON_FIRE, S_MASTODON_FIRE2, 12),

    /* Scrap Clans */
    STRUCTURE(SPR_CL_HALL, S_CL_HALL, S_CL_HALL_DAMAGED, S_CL_HALL_BUILD, 2, 15),
    STRUCTURE(SPR_CL_GENERATOR, S_CL_GENERATOR, S_CL_GENERATOR_DAMAGED, S_CL_GENERATOR_BUILD, 2, 15),
    STRUCTURE(SPR_CL_SMELTER, S_CL_SMELTER, S_CL_SMELTER_DAMAGED, S_CL_SMELTER_BUILD, 2, 15),
    STRUCTURE(SPR_CL_CAMP, S_CL_CAMP, S_CL_CAMP_DAMAGED, S_CL_CAMP_BUILD, 2, 15),
    STRUCTURE(SPR_CL_YARD, S_CL_YARD, S_CL_YARD_DAMAGED, S_CL_YARD_BUILD, 2, 15),
    EMPLACEMENT(SPR_CL_TOWER, SPR_CL_TOWER_BUILD, S_CL_TOWER, S_CL_TOWER_FIRE, S_CL_TOWER_FIRE2, S_CL_TOWER_BUILD, 4),
    GUNNED(SPR_CL_TRIKE, S_CL_TRIKE, S_CL_TRIKE_MOVE, S_CL_TRIKE_FIRE, S_CL_TRIKE_FIRE2, 4),
    TURRETED(SPR_CL_TRUCK, S_CL_TRUCK, S_CL_TRUCK_MOVE, S_CL_TRUCK_FIRE, S_CL_TRUCK_FIRE2, 4),
    TURRETED(SPR_CL_CRAWLER, S_CL_CRAWLER, S_CL_CRAWLER_MOVE, S_CL_CRAWLER_FIRE, S_CL_CRAWLER_FIRE2, 10),
    GUNNED(SPR_CL_RIG, S_CL_RIG, S_CL_RIG_MOVE, S_CL_RIG_FIRE, S_CL_RIG_FIRE2, 14),
    HAULER(SPR_CL_HAULER, S_CL_HAULER, S_CL_HAULER_MOVE),
    SOLDIER(SPR_CL_RAIDER, S_CL_RAIDER, S_CL_RAIDER_WALK, S_CL_RAIDER_FIRE, S_CL_RAIDER_FIRE2, S_CL_RAIDER_DIE, S_CL_RAIDER_CORPSE),
    SOLDIER(SPR_CL_TORCHER, S_CL_TORCHER, S_CL_TORCHER_WALK, S_CL_TORCHER_FIRE, S_CL_TORCHER_FIRE2, S_CL_TORCHER_DIE, S_CL_TORCHER_CORPSE),

    /* Lumen Concord: the structures' glow pulses */
    STRUCTURE(SPR_LC_SPIRE, S_LC_SPIRE, S_LC_SPIRE_DAMAGED, S_LC_SPIRE_BUILD, 8, 6),
    STRUCTURE(SPR_LC_FUSION, S_LC_FUSION, S_LC_FUSION_DAMAGED, S_LC_FUSION_BUILD, 8, 6),
    STRUCTURE(SPR_LC_EXTRACTOR, S_LC_EXTRACTOR, S_LC_EXTRACTOR_DAMAGED, S_LC_EXTRACTOR_BUILD, 2, 10),
    STRUCTURE(SPR_LC_ACADEMY, S_LC_ACADEMY, S_LC_ACADEMY_DAMAGED, S_LC_ACADEMY_BUILD, 8, 6),
    STRUCTURE(SPR_LC_FOUNDRY, S_LC_FOUNDRY, S_LC_FOUNDRY_DAMAGED, S_LC_FOUNDRY_BUILD, 8, 6),
    EMPLACEMENT(SPR_LC_TOWER, SPR_LC_TOWER_BUILD, S_LC_TOWER, S_LC_TOWER_FIRE, S_LC_TOWER_FIRE2, S_LC_TOWER_BUILD, 14),
    GUNNED(SPR_LC_SKIMMER, S_LC_SKIMMER, S_LC_SKIMMER_MOVE, S_LC_SKIMMER_FIRE, S_LC_SKIMMER_FIRE2, 6),
    TURRETED(SPR_LC_GLAIVE, S_LC_GLAIVE, S_LC_GLAIVE_MOVE, S_LC_GLAIVE_FIRE, S_LC_GLAIVE_FIRE2, 10),
    GUNNED(SPR_LC_PRISM, S_LC_PRISM, S_LC_PRISM_MOVE, S_LC_PRISM_FIRE, S_LC_PRISM_FIRE2, 14),
    HAULER(SPR_LC_HARVESTER, S_LC_HARVESTER, S_LC_HARVESTER_MOVE),
    SOLDIER(SPR_LC_WARDEN, S_LC_WARDEN, S_LC_WARDEN_WALK, S_LC_WARDEN_FIRE, S_LC_WARDEN_FIRE2, S_LC_WARDEN_DIE, S_LC_WARDEN_CORPSE),
    SOLDIER(SPR_LC_LANCER, S_LC_LANCER, S_LC_LANCER_WALK, S_LC_LANCER_FIRE, S_LC_LANCER_FIRE2, S_LC_LANCER_DIE, S_LC_LANCER_CORPSE),

    /* A bolt is a streak of light ending in a cyan flash; the heavy one in a big one. A flame
     * gout lands as a burst of fire. */
    PROJECTILE(SPR_FX_BOLT, S_BOLT, S_BOLT_HIT, S_BOLT_HIT2, SPR_FX_ZAP, 6, 2),
    PROJECTILE(SPR_FX_BOLT, S_HEAVY_BOLT, S_HEAVY_BOLT_HIT, S_HEAVY_BOLT_HIT2, SPR_FX_BLAST, 7, 2),
    PROJECTILE(SPR_FX_FLAME, S_FLAME, S_FLAME_HIT, S_FLAME_HIT2, SPR_FX_FIRE, 6, 2),

    /* The top tier: the Uplink's dish turns, the Chop Shop's fire barrel flickers, the
     * Sanctum's halo turns and pulses. The Hammer's missile lands in a big blast. */
    STRUCTURE(SPR_UPLINK, S_UPLINK, S_UPLINK_DAMAGED, S_UPLINK_BUILD, 8, 6),
    GUNNED(SPR_HAMMER, S_HAMMER, S_HAMMER_MOVE, S_HAMMER_FIRE, S_HAMMER_FIRE2, 18),
    STRUCTURE(SPR_CL_SHOP, S_CL_SHOP, S_CL_SHOP_DAMAGED, S_CL_SHOP_BUILD, 2, 15),
    GUNNED(SPR_CL_WRECKER, S_CL_WRECKER, S_CL_WRECKER_MOVE, S_CL_WRECKER_FIRE, S_CL_WRECKER_FIRE2, 4),
    STRUCTURE(SPR_LC_SANCTUM, S_LC_SANCTUM, S_LC_SANCTUM_DAMAGED, S_LC_SANCTUM_BUILD, 8, 6),
    GUNNED(SPR_LC_AEGIS, S_LC_AEGIS, S_LC_AEGIS_MOVE, S_LC_AEGIS_FIRE, S_LC_AEGIS_FIRE2, 8),
    PROJECTILE(SPR_FX_ROCKET, S_HEAVY_MISSILE, S_HEAVY_MISSILE_HIT, S_HEAVY_MISSILE_HIT2, SPR_FX_BIGBOOM, 12, 3),

    /* The special units (g_units.c). The crane welds (its body's spark frame) while it mends;
     * the sentry post fires its twin guns from the head on its base sheet; the derrick pumps. */
    [S_CRANE] = {SPR_CRANE, VEH_STAND, 1, -1, NULL, S_CRANE, 0, NULL},
    [S_CRANE_MOVE] = {SPR_CRANE, VEH_STAND, 2, 3, NULL, S_CRANE_MOVE, 2, NULL},
    [S_CRANE_WORK] = {SPR_CRANE, VEH_SHOT, 1, 3, NULL, S_CRANE_WORK2, 0, NULL},
    [S_CRANE_WORK2] = {SPR_CRANE, VEH_STAND, 1, 4, NULL, S_CRANE_WORK, 0, NULL},
    TURRETED(SPR_PHANTOM, S_PHANTOM, S_PHANTOM_MOVE, S_PHANTOM_FIRE, S_PHANTOM_FIRE2, 9),
    FLYER(SPR_KESTREL, S_KESTREL, S_KESTREL_MOVE, S_KESTREL_FIRE, S_KESTREL_FIRE2, 8),
    HAULER(SPR_SENTRY_TRUCK, S_SENTRY_TRUCK, S_SENTRY_TRUCK_MOVE),
    [S_SENTRY] = {SPR_SENTRY, VEH_STAND, 1, 1, A_Look, S_SENTRY, 0, NULL},
    [S_SENTRY_FIRE] = {SPR_SENTRY, VEH_STAND, 1, 3, A_Attack, S_SENTRY_FIRE2, 3, NULL},
    [S_SENTRY_FIRE2] = {SPR_SENTRY, VEH_STEP, 1, 4, NULL, S_SENTRY, 3, NULL},
    HAULER(SPR_CL_DRILL, S_CL_DRILL, S_CL_DRILL_MOVE),
    STRUCTURE(SPR_CL_DERRICK, S_CL_DERRICK, S_CL_DERRICK_DAMAGED, S_CL_DERRICK_BUILD, 4, 6),
    SOLDIER(SPR_CL_SAPPER, S_CL_SAPPER, S_CL_SAPPER_WALK, S_CL_SAPPER_FIRE, S_CL_SAPPER_FIRE2, S_CL_SAPPER_DIE, S_CL_SAPPER_CORPSE),
    [S_CL_MINE] = {SPR_CL_MINE, 0, 2, 20, NULL, S_CL_MINE, 0, NULL},
    RAMMER(SPR_CL_CRUSHER, S_CL_CRUSHER, S_CL_CRUSHER_MOVE, S_CL_CRUSHER_FIRE, S_CL_CRUSHER_FIRE2, 6),
    FLYER(SPR_CL_BUZZARD, S_CL_BUZZARD, S_CL_BUZZARD_MOVE, S_CL_BUZZARD_FIRE, S_CL_BUZZARD_FIRE2, 14),
    GUNNED(SPR_LC_STRIDER, S_LC_STRIDER, S_LC_STRIDER_MOVE, S_LC_STRIDER_FIRE, S_LC_STRIDER_FIRE2, 10),
    FLYER(SPR_LC_SERAPH, S_LC_SERAPH, S_LC_SERAPH_MOVE, S_LC_SERAPH_FIRE, S_LC_SERAPH_FIRE2, 8),
    /* The cantor has no weapon: its "shot" is the staff's glow as it heals (g_units.c). */
    [S_LC_CANTOR] = {SPR_LC_CANTOR, INF_IDLE, INF_IDLE_FRAMES, 12, NULL, S_LC_CANTOR, 0, NULL},
    [S_LC_CANTOR_WALK] = {SPR_LC_CANTOR, INF_WALK, INF_WALK_FRAMES, 3, NULL, S_LC_CANTOR_WALK, 2, NULL},
    [S_LC_CANTOR_FIRE] = {SPR_LC_CANTOR, INF_FIRE, 1, 6, NULL, S_LC_CANTOR_FIRE2, 0, NULL},
    [S_LC_CANTOR_FIRE2] = {SPR_LC_CANTOR, INF_FIRE + 1, 1, 6, NULL, S_LC_CANTOR, 0, NULL},
    [S_LC_CANTOR_DIE] = {SPR_LC_CANTOR, INF_DIE, INF_DIE_FRAMES, 3, NULL, S_LC_CANTOR_CORPSE, 0, NULL},
    [S_LC_CANTOR_CORPSE] = {SPR_LC_CANTOR, INF_DIE + INF_DIE_FRAMES - 1, 1, 300, NULL, S_NULL, 0, NULL},
    [S_FX_ZAP] = {SPR_FX_ZAP, 0, 6, 2, NULL, S_NULL, 0, NULL},
    /* A Buzzard's bomb drum tumbles down and goes up in a big blast. */
    PROJECTILE(SPR_FX_SHELL, S_BOMB, S_BOMB_HIT, S_BOMB_HIT2, SPR_FX_BIGBOOM, 12, 3),
};

#define BUILDING_INFO(n, st, hp) {.doomednum = n, .spawnstate = st, .spawnhealth = hp, .deathstate = S_NULL}
#define MOVER_INFO(n, st, see, fire, hp, spd, rad, dmg, die) \
    {.doomednum = n, .spawnstate = st, .seestate = see, .missilestate = fire, .spawnhealth = hp, \
     .speed = spd, .radius = rad, .height = 32, .damage = dmg, .deathstate = die}

const mobjinfo_t mobjinfo[NUMMOBJTYPES] = {
    [MT_NULL] = {0},
    [MT_HQ] = BUILDING_INFO(1, S_HQ, 3000),
    [MT_POWER] = BUILDING_INFO(2, S_POWER, 1200),
    [MT_REFINERY] = BUILDING_INFO(3, S_REFINERY, 1800),
    [MT_BARRACKS] = BUILDING_INFO(4, S_BARRACKS, 1200),
    [MT_FACTORY] = BUILDING_INFO(5, S_FACTORY, 2400),
    [MT_GUN_TURRET] = {.doomednum = 6, .spawnstate = S_GUN_TURRET, .missilestate = S_GUN_TURRET_FIRE,
                       .spawnhealth = 800, .damage = 40, .deathstate = S_NULL},
    [MT_SCOUT] = MOVER_INFO(7, S_SCOUT, S_SCOUT_MOVE, S_SCOUT_FIRE, 220, 7, 14, 12, S_NULL),
    [MT_TANK] = MOVER_INFO(8, S_TANK, S_TANK_MOVE, S_TANK_FIRE, 680, 4, 18, 60, S_NULL),
    [MT_ARTILLERY] = MOVER_INFO(9, S_ARTILLERY, S_ARTILLERY_MOVE, S_ARTILLERY_FIRE, 300, 3, 18, 100, S_NULL),
    [MT_HARVESTER] = MOVER_INFO(10, S_HARVESTER, S_HARVESTER_MOVE, 0, 900, 3, 20, 0, S_NULL),
    [MT_APC] = MOVER_INFO(11, S_APC, S_APC_MOVE, S_APC_FIRE, 450, 6, 18, 18, S_NULL),
    [MT_RIFLEMAN] = MOVER_INFO(12, S_RIFLEMAN, S_RIFLEMAN_WALK, S_RIFLEMAN_FIRE, 50, 2, 8, 8, S_RIFLEMAN_DIE),
    [MT_ROCKET] = MOVER_INFO(13, S_ROCKET, S_ROCKET_WALK, S_ROCKET_FIRE, 45, 2, 8, 40, S_ROCKET_DIE),
    [MT_MASTODON] = MOVER_INFO(14, S_MASTODON, S_MASTODON_MOVE, S_MASTODON_FIRE, 1400, 3, 22, 70, S_NULL),
    [MT_CL_HALL] = BUILDING_INFO(20, S_CL_HALL, 2600),
    [MT_CL_GENERATOR] = BUILDING_INFO(21, S_CL_GENERATOR, 900),
    [MT_CL_SMELTER] = BUILDING_INFO(22, S_CL_SMELTER, 1500),
    [MT_CL_CAMP] = BUILDING_INFO(23, S_CL_CAMP, 900),
    [MT_CL_YARD] = BUILDING_INFO(24, S_CL_YARD, 2000),
    [MT_CL_TOWER] = {.doomednum = 25, .spawnstate = S_CL_TOWER, .missilestate = S_CL_TOWER_FIRE,
                     .spawnhealth = 700, .damage = 18, .deathstate = S_NULL},
    [MT_CL_TRIKE] = MOVER_INFO(26, S_CL_TRIKE, S_CL_TRIKE_MOVE, S_CL_TRIKE_FIRE, 190, 8, 12, 10, S_NULL),
    [MT_CL_TRUCK] = MOVER_INFO(27, S_CL_TRUCK, S_CL_TRUCK_MOVE, S_CL_TRUCK_FIRE, 360, 6, 16, 14, S_NULL),
    [MT_CL_CRAWLER] = MOVER_INFO(28, S_CL_CRAWLER, S_CL_CRAWLER_MOVE, S_CL_CRAWLER_FIRE, 600, 4, 18, 80, S_NULL),
    [MT_CL_RIG] = MOVER_INFO(29, S_CL_RIG, S_CL_RIG_MOVE, S_CL_RIG_FIRE, 260, 4, 18, 30, S_NULL),
    [MT_CL_HAULER] = MOVER_INFO(30, S_CL_HAULER, S_CL_HAULER_MOVE, 0, 700, 4, 20, 0, S_NULL),
    [MT_CL_RAIDER] = MOVER_INFO(31, S_CL_RAIDER, S_CL_RAIDER_WALK, S_CL_RAIDER_FIRE, 55, 2, 8, 6, S_CL_RAIDER_DIE),
    [MT_CL_TORCHER] = MOVER_INFO(32, S_CL_TORCHER, S_CL_TORCHER_WALK, S_CL_TORCHER_FIRE, 80, 2, 8, 30, S_CL_TORCHER_DIE),
    [MT_LC_SPIRE] = BUILDING_INFO(40, S_LC_SPIRE, 3600),
    [MT_LC_FUSION] = BUILDING_INFO(41, S_LC_FUSION, 1500),
    [MT_LC_EXTRACTOR] = BUILDING_INFO(42, S_LC_EXTRACTOR, 2200),
    [MT_LC_ACADEMY] = BUILDING_INFO(43, S_LC_ACADEMY, 1500),
    [MT_LC_FOUNDRY] = BUILDING_INFO(44, S_LC_FOUNDRY, 2800),
    [MT_LC_TOWER] = {.doomednum = 45, .spawnstate = S_LC_TOWER, .missilestate = S_LC_TOWER_FIRE,
                     .spawnhealth = 900, .damage = 50, .deathstate = S_NULL},
    [MT_LC_SKIMMER] = MOVER_INFO(46, S_LC_SKIMMER, S_LC_SKIMMER_MOVE, S_LC_SKIMMER_FIRE, 260, 8, 14, 18, S_NULL),
    [MT_LC_GLAIVE] = MOVER_INFO(47, S_LC_GLAIVE, S_LC_GLAIVE_MOVE, S_LC_GLAIVE_FIRE, 700, 5, 18, 80, S_NULL),
    [MT_LC_PRISM] = MOVER_INFO(48, S_LC_PRISM, S_LC_PRISM_MOVE, S_LC_PRISM_FIRE, 380, 4, 18, 160, S_NULL),
    [MT_LC_HARVESTER] = MOVER_INFO(49, S_LC_HARVESTER, S_LC_HARVESTER_MOVE, 0, 1100, 4, 20, 0, S_NULL),
    [MT_LC_WARDEN] = MOVER_INFO(50, S_LC_WARDEN, S_LC_WARDEN_WALK, S_LC_WARDEN_FIRE, 100, 2, 8, 16, S_LC_WARDEN_DIE),
    [MT_LC_LANCER] = MOVER_INFO(51, S_LC_LANCER, S_LC_LANCER_WALK, S_LC_LANCER_FIRE, 110, 2, 9, 60, S_LC_LANCER_DIE),
    [MT_UPLINK] = BUILDING_INFO(15, S_UPLINK, 1400),
    [MT_HAMMER] = MOVER_INFO(16, S_HAMMER, S_HAMMER_MOVE, S_HAMMER_FIRE, 520, 3, 20, 110, S_NULL),
    [MT_CL_SHOP] = BUILDING_INFO(33, S_CL_SHOP, 1100),
    [MT_CL_WRECKER] = MOVER_INFO(34, S_CL_WRECKER, S_CL_WRECKER_MOVE, S_CL_WRECKER_FIRE, 320, 8, 16, 500, S_NULL),
    [MT_LC_SANCTUM] = BUILDING_INFO(52, S_LC_SANCTUM, 1600),
    [MT_LC_AEGIS] = MOVER_INFO(53, S_LC_AEGIS, S_LC_AEGIS_MOVE, S_LC_AEGIS_FIRE, 750, 5, 16, 22, S_NULL),
    [MT_CRANE] = MOVER_INFO(60, S_CRANE, S_CRANE_MOVE, 0, 420, 5, 18, 0, S_NULL),
    [MT_PHANTOM] = MOVER_INFO(61, S_PHANTOM, S_PHANTOM_MOVE, S_PHANTOM_FIRE, 540, 5, 18, 55, S_NULL),
    [MT_KESTREL] = MOVER_INFO(62, S_KESTREL, S_KESTREL_MOVE, S_KESTREL_FIRE, 300, 7, 18, 30, S_NULL),
    [MT_SENTRY_TRUCK] = MOVER_INFO(63, S_SENTRY_TRUCK, S_SENTRY_TRUCK_MOVE, 0, 380, 5, 16, 0, S_NULL),
    [MT_SENTRY] = {.doomednum = 64, .spawnstate = S_SENTRY, .missilestate = S_SENTRY_FIRE,
                   .spawnhealth = 380, .damage = 14, .deathstate = S_NULL},
    [MT_CL_DRILL] = MOVER_INFO(65, S_CL_DRILL, S_CL_DRILL_MOVE, 0, 600, 3, 20, 0, S_NULL),
    [MT_CL_DERRICK] = BUILDING_INFO(66, S_CL_DERRICK, 1000),
    [MT_CL_SAPPER] = MOVER_INFO(67, S_CL_SAPPER, S_CL_SAPPER_WALK, S_CL_SAPPER_FIRE, 60, 2, 8, 22, S_CL_SAPPER_DIE),
    [MT_CL_MINE] = {.doomednum = 68, .spawnstate = S_CL_MINE, .spawnhealth = 30, .deathstate = S_NULL},
    [MT_CL_CRUSHER] = MOVER_INFO(69, S_CL_CRUSHER, S_CL_CRUSHER_MOVE, S_CL_CRUSHER_FIRE, 700, 6, 20, 40, S_NULL),
    [MT_CL_BUZZARD] = MOVER_INFO(70, S_CL_BUZZARD, S_CL_BUZZARD_MOVE, S_CL_BUZZARD_FIRE, 260, 7, 16, 120, S_NULL),
    [MT_LC_STRIDER] = MOVER_INFO(71, S_LC_STRIDER, S_LC_STRIDER_MOVE, S_LC_STRIDER_FIRE, 650, 4, 18, 45, S_NULL),
    [MT_LC_SERAPH] = MOVER_INFO(72, S_LC_SERAPH, S_LC_SERAPH_MOVE, S_LC_SERAPH_FIRE, 380, 8, 16, 40, S_NULL),
    [MT_LC_CANTOR] = MOVER_INFO(73, S_LC_CANTOR, S_LC_CANTOR_WALK, 0, 90, 2, 8, 0, S_LC_CANTOR_DIE),
    [MT_FX_ZAP] = {.doomednum = -1, .spawnstate = S_FX_ZAP, .spawnhealth = 1, .deathstate = S_NULL},
    [MT_BOMB] = {.doomednum = -1, .spawnstate = S_BOMB, .spawnhealth = 1, .deathstate = S_BOMB_HIT},
    [MT_FX_HIT] = {.doomednum = -1, .spawnstate = S_FX_HIT, .spawnhealth = 1, .deathstate = S_NULL},
    [MT_FX_BOOM] = {.doomednum = -1, .spawnstate = S_FX_BOOM, .spawnhealth = 1, .deathstate = S_NULL},
    [MT_FX_BIGBOOM] = {.doomednum = -1, .spawnstate = S_FX_BIGBOOM, .spawnhealth = 1, .deathstate = S_NULL},
    [MT_FX_SMOKE] = {.doomednum = -1, .spawnstate = S_FX_SMOKE, .spawnhealth = 1, .deathstate = S_NULL},
    [MT_FX_PLUME] = {.doomednum = -1, .spawnstate = S_FX_PLUME, .spawnhealth = 1, .deathstate = S_NULL},
    [MT_FX_FIRE] = {.doomednum = -1, .spawnstate = S_FX_FIRE, .spawnhealth = 1, .deathstate = S_NULL},
    [MT_SHELL] = {.doomednum = -1, .spawnstate = S_SHELL, .spawnhealth = 1, .deathstate = S_SHELL_HIT},
    [MT_HEAVY_SHELL] = {.doomednum = -1, .spawnstate = S_HEAVY_SHELL, .spawnhealth = 1, .deathstate = S_HEAVY_SHELL_HIT},
    [MT_MISSILE] = {.doomednum = -1, .spawnstate = S_MISSILE, .spawnhealth = 1, .deathstate = S_MISSILE_HIT},
    [MT_BOLT] = {.doomednum = -1, .spawnstate = S_BOLT, .spawnhealth = 1, .deathstate = S_BOLT_HIT},
    [MT_HEAVY_BOLT] = {.doomednum = -1, .spawnstate = S_HEAVY_BOLT, .spawnhealth = 1, .deathstate = S_HEAVY_BOLT_HIT},
    [MT_FLAME] = {.doomednum = -1, .spawnstate = S_FLAME, .spawnhealth = 1, .deathstate = S_FLAME_HIT},
    [MT_HEAVY_MISSILE] = {.doomednum = -1, .spawnstate = S_HEAVY_MISSILE, .spawnhealth = 1, .deathstate = S_HEAVY_MISSILE_HIT},
};

const gameinfo_t game_info = {
    .sprnames = sprnames, .sprite_count = NUMSPRITES,
    .states = states, .state_count = NUMSTATES,
    .mobjinfo = mobjinfo, .mobj_type_count = NUMMOBJTYPES,
    .null_state = S_NULL,
    .state_coord_mode = RTS_STATE_COORDS_GROUND_OFFSET,
    .team_palettes = true,
    .sound = &rf_soundinfo,
    .attack = RF_Attack,
    .mobj_ticker = RF_MobjTicker,
    .draw_fog = RF_DrawFog,
    .hit_damage = RF_HitDamage,
    .draw_overlays = RF_DrawMarks,
    .can_target = RF_CanTargetHook,
    .attack_blocked = RF_AttackBlockedHook,
};
