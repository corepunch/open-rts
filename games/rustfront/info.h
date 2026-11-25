#ifndef __INFO__
#define __INFO__

#include "engine.h"

typedef struct mobjinfo_s {
    int doomednum;
    int spawnstate;
    int spawnhealth;
    int seestate;
    int seesound;
    int reactiontime;
    int attacksound;
    int painstate;
    int painchance;
    int painsound;
    int meleestate;
    int missilestate;
    int deathstate;
    int xdeathstate;
    int deathsound;
    int speed;
    int radius;
    int height;
    int mass;
    int damage;
    int activesound;
    int flags;
    int raisestate;
    fixed_t spawnz;
} mobjinfo_t;

/* Sprite names are the sheets' cache names: <unit>_<layer> as art/pack8.py writes them. */
typedef enum {
    SPR_HQ, SPR_POWER, SPR_REFINERY, SPR_BARRACKS, SPR_FACTORY, SPR_GUN_TURRET,
    SPR_SCOUT, SPR_TANK, SPR_ARTILLERY, SPR_HARVESTER, SPR_APC,
    SPR_RIFLEMAN, SPR_ROCKET,
    SPR_FX_HIT, SPR_FX_BOOM, SPR_FX_BIGBOOM,
    NUMSPRITES
} spritenum_t;

typedef enum {
    S_NULL = 0,
    S_HQ, S_POWER, S_REFINERY, S_BARRACKS, S_FACTORY,
    S_GUN_TURRET, S_GUN_TURRET_FIRE, S_GUN_TURRET_FIRE2,
    S_SCOUT, S_SCOUT_MOVE, S_SCOUT_FIRE, S_SCOUT_FIRE2,
    S_TANK, S_TANK_MOVE, S_TANK_FIRE, S_TANK_FIRE2,
    S_ARTILLERY, S_ARTILLERY_MOVE, S_ARTILLERY_FIRE, S_ARTILLERY_FIRE2,
    S_HARVESTER, S_HARVESTER_MOVE,
    S_APC, S_APC_MOVE, S_APC_FIRE, S_APC_FIRE2,
    S_RIFLEMAN, S_RIFLEMAN_WALK, S_RIFLEMAN_FIRE, S_RIFLEMAN_FIRE2, S_RIFLEMAN_DIE, S_RIFLEMAN_CORPSE,
    S_ROCKET, S_ROCKET_WALK, S_ROCKET_FIRE, S_ROCKET_FIRE2, S_ROCKET_DIE, S_ROCKET_CORPSE,
    S_FX_HIT, S_FX_BOOM, S_FX_BIGBOOM,
    NUMSTATES
} statenum_t;

enum {
    MT_NULL,
    MT_HQ, MT_POWER, MT_REFINERY, MT_BARRACKS, MT_FACTORY, MT_GUN_TURRET,
    MT_SCOUT, MT_TANK, MT_ARTILLERY, MT_HARVESTER, MT_APC,
    MT_RIFLEMAN, MT_ROCKET,
    /* Effects: pictures only (g_mission.c spawns them on damage and death). */
    MT_FX_HIT, MT_FX_BOOM, MT_FX_BIGBOOM,
    NUMMOBJTYPES,
};

/* Infantry logical frames, in the order of their sheet's animations (art/sprites.py). */
enum { INF_IDLE = 0, INF_WALK = 1, INF_FIRE = 7, INF_DIE = 9, INF_FRAMES = 15 };
/* Vehicle frames (art/sprites.py MOVE and MOVE_FIRE): 0 standing, 1 the running gear a step
 * on, 2 the body's own gun firing. A turret sheet is 0 idle, 1 the shot, 2 run-out. */
enum { VEH_STAND = 0, VEH_STEP = 1, VEH_SHOT = 2, TURRET_SHOT = 1 };

extern const char *const sprnames[NUMSPRITES];
extern const state_t states[NUMSTATES];
extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];
extern const gameinfo_t game_info;

#endif
