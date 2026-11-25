#include "engine.h"
#include "info.h"

const char *const sprnames[NUMSPRITES] = {
    "hq_body", "power_body", "refinery_body", "barracks_body", "factory_body", "turret_base",
    "scout_body", "tank_hull", "artillery_body", "harvester_body", "apc_hull",
    "rifleman_body", "rocket_body",
};

/* {sprite, frame, count, tics, action, next, group}: group 2 plays while moving, 3 attacking. */
#define STILL(spr, self) {spr, 0, 1, -1, NULL, self, 0, NULL}
#define VEHICLE(spr, stnd, move, fire) \
    [stnd] = {spr, 0, 1, 1, A_Look, stnd, 0, NULL}, \
    [move] = {spr, 0, 1, 1, NULL, move, 2, NULL}, \
    [fire] = {spr, 0, 1, 12, A_Attack, stnd, 3, NULL}
#define SOLDIER(spr, stnd, walk, fire, fire2, die, corpse) \
    [stnd]   = {spr, INF_IDLE, 1, 1, A_Look, stnd, 0, NULL}, \
    [walk]   = {spr, INF_WALK, 6, 4, NULL, walk, 2, NULL}, \
    [fire]   = {spr, INF_FIRE, 1, 3, A_Attack, fire2, 3, NULL}, \
    [fire2]  = {spr, INF_FIRE + 1, 1, 4, NULL, stnd, 3, NULL}, \
    [die]    = {spr, INF_DIE, 6, 4, NULL, corpse, 0, NULL}, \
    [corpse] = {spr, INF_DIE + 5, 1, 300, NULL, S_NULL, 0, NULL}

const state_t states[NUMSTATES] = {
    [S_NULL] = {0, 0, 0, -1, NULL, S_NULL, 0, NULL},
    [S_HQ] = STILL(SPR_HQ, S_HQ),
    [S_POWER] = STILL(SPR_POWER, S_POWER),
    [S_REFINERY] = STILL(SPR_REFINERY, S_REFINERY),
    [S_BARRACKS] = STILL(SPR_BARRACKS, S_BARRACKS),
    [S_FACTORY] = STILL(SPR_FACTORY, S_FACTORY),
    [S_GUN_TURRET] = {SPR_GUN_TURRET, 0, 1, 1, A_Look, S_GUN_TURRET, 0, NULL},
    [S_GUN_TURRET_FIRE] = {SPR_GUN_TURRET, 0, 1, 15, A_Attack, S_GUN_TURRET, 3, NULL},
    VEHICLE(SPR_SCOUT, S_SCOUT, S_SCOUT_MOVE, S_SCOUT_FIRE),
    VEHICLE(SPR_TANK, S_TANK, S_TANK_MOVE, S_TANK_FIRE),
    VEHICLE(SPR_ARTILLERY, S_ARTILLERY, S_ARTILLERY_MOVE, S_ARTILLERY_FIRE),
    [S_HARVESTER] = STILL(SPR_HARVESTER, S_HARVESTER),
    [S_HARVESTER_MOVE] = {SPR_HARVESTER, 0, 1, 1, NULL, S_HARVESTER_MOVE, 2, NULL},
    VEHICLE(SPR_APC, S_APC, S_APC_MOVE, S_APC_FIRE),
    SOLDIER(SPR_RIFLEMAN, S_RIFLEMAN, S_RIFLEMAN_WALK, S_RIFLEMAN_FIRE, S_RIFLEMAN_FIRE2, S_RIFLEMAN_DIE, S_RIFLEMAN_CORPSE),
    SOLDIER(SPR_ROCKET, S_ROCKET, S_ROCKET_WALK, S_ROCKET_FIRE, S_ROCKET_FIRE2, S_ROCKET_DIE, S_ROCKET_CORPSE),
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
};

const gameinfo_t game_info = {
    .sprnames = sprnames, .sprite_count = NUMSPRITES,
    .states = states, .state_count = NUMSTATES,
    .mobjinfo = mobjinfo, .mobj_type_count = NUMMOBJTYPES,
    .null_state = S_NULL,
    .state_coord_mode = RTS_STATE_COORDS_GROUND_OFFSET,
    .team_palettes = true,
};
