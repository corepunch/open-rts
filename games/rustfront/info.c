#include "engine.h"
#include "info.h"
#include "rustfront.h"

const char *const sprnames[NUMSPRITES] = {
    "hq_body", "power_body", "refinery_body", "barracks_body", "factory_body", "turret_base",
    "scout_body", "tank_hull", "artillery_body", "harvester_body", "apc_hull",
    "rifleman_body", "rocket_body",
    "fx_hit", "fx_boom", "fx_bigboom", "fx_shell", "fx_rocket", "fx_smoke", "fx_blast",
};

/* {sprite, frame, count, tics, action, next, group}: group 2 plays while moving, 3 attacking.
 * An action runs on every frame of a run, so a shot is two states: the firing frame, then
 * the settle. */
#define ANIMATED(spr, self, frames, tics) {spr, 0, frames, tics, NULL, self, 0, NULL}
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
#define SOLDIER(spr, stnd, walk, fire, fire2, die, corpse) \
    [stnd]   = {spr, INF_IDLE, 1, 1, A_Look, stnd, 0, NULL}, \
    [walk]   = {spr, INF_WALK, 6, 4, NULL, walk, 2, NULL}, \
    [fire]   = {spr, INF_FIRE, 1, 3, A_Attack, fire2, 3, NULL}, \
    [fire2]  = {spr, INF_FIRE + 1, 1, 4, NULL, stnd, 3, NULL}, \
    [die]    = {spr, INF_DIE, 6, 4, NULL, corpse, 0, NULL}, \
    [corpse] = {spr, INF_DIE + 5, 1, 300, NULL, S_NULL, 0, NULL}

const state_t states[NUMSTATES] = {
    [S_NULL] = {0, 0, 0, -1, NULL, S_NULL, 0, NULL},
    /* Buildings loop their sheet: the radar turns, lights blink, the reactor pulses, the flare flickers. */
    [S_HQ] = ANIMATED(SPR_HQ, S_HQ, 8, 6),
    [S_POWER] = ANIMATED(SPR_POWER, S_POWER, 4, 8),
    [S_REFINERY] = ANIMATED(SPR_REFINERY, S_REFINERY, 4, 5),
    [S_BARRACKS] = ANIMATED(SPR_BARRACKS, S_BARRACKS, 2, 15),
    [S_FACTORY] = ANIMATED(SPR_FACTORY, S_FACTORY, 2, 15),
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
                       .spawnhealth = 800, .damage = 45, .deathstate = S_NULL},
    [MT_SCOUT] = MOVER_INFO(7, S_SCOUT, S_SCOUT_MOVE, S_SCOUT_FIRE, 220, 7, 14, 12, S_NULL),
    [MT_TANK] = MOVER_INFO(8, S_TANK, S_TANK_MOVE, S_TANK_FIRE, 600, 4, 18, 60, S_NULL),
    [MT_ARTILLERY] = MOVER_INFO(9, S_ARTILLERY, S_ARTILLERY_MOVE, S_ARTILLERY_FIRE, 300, 3, 18, 120, S_NULL),
    [MT_HARVESTER] = MOVER_INFO(10, S_HARVESTER, S_HARVESTER_MOVE, 0, 900, 3, 20, 0, S_NULL),
    [MT_APC] = MOVER_INFO(11, S_APC, S_APC_MOVE, S_APC_FIRE, 450, 6, 18, 18, S_NULL),
    [MT_RIFLEMAN] = MOVER_INFO(12, S_RIFLEMAN, S_RIFLEMAN_WALK, S_RIFLEMAN_FIRE, 50, 2, 8, 8, S_RIFLEMAN_DIE),
    [MT_ROCKET] = MOVER_INFO(13, S_ROCKET, S_ROCKET_WALK, S_ROCKET_FIRE, 45, 2, 8, 40, S_ROCKET_DIE),
    [MT_FX_HIT] = {.doomednum = -1, .spawnstate = S_FX_HIT, .spawnhealth = 1, .deathstate = S_NULL},
    [MT_FX_BOOM] = {.doomednum = -1, .spawnstate = S_FX_BOOM, .spawnhealth = 1, .deathstate = S_NULL},
    [MT_FX_BIGBOOM] = {.doomednum = -1, .spawnstate = S_FX_BIGBOOM, .spawnhealth = 1, .deathstate = S_NULL},
    [MT_FX_SMOKE] = {.doomednum = -1, .spawnstate = S_FX_SMOKE, .spawnhealth = 1, .deathstate = S_NULL},
    [MT_SHELL] = {.doomednum = -1, .spawnstate = S_SHELL, .spawnhealth = 1, .deathstate = S_SHELL_HIT},
    [MT_HEAVY_SHELL] = {.doomednum = -1, .spawnstate = S_HEAVY_SHELL, .spawnhealth = 1, .deathstate = S_HEAVY_SHELL_HIT},
    [MT_MISSILE] = {.doomednum = -1, .spawnstate = S_MISSILE, .spawnhealth = 1, .deathstate = S_MISSILE_HIT},
};

const gameinfo_t game_info = {
    .sprnames = sprnames, .sprite_count = NUMSPRITES,
    .states = states, .state_count = NUMSTATES,
    .mobjinfo = mobjinfo, .mobj_type_count = NUMMOBJTYPES,
    .null_state = S_NULL,
    .state_coord_mode = RTS_STATE_COORDS_GROUND_OFFSET,
    .team_palettes = true,
    .attack = RF_Attack,
};
