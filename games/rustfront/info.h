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
    SPR_FX_HIT, SPR_FX_BOOM, SPR_FX_BIGBOOM, SPR_FX_SHELL, SPR_FX_ROCKET, SPR_FX_SMOKE, SPR_FX_BLAST,
    SPR_FX_PLUME, SPR_FX_FIRE, SPR_GUN_TURRET_BUILD,
    NUMSPRITES
} spritenum_t;

typedef enum {
    S_NULL = 0,
    /* A structure loops its animation, or its scorched copy below half health, and holds one of
     * four construction stages while it builds (g_build.c). */
    S_HQ, S_HQ_DAMAGED, S_HQ_BUILD, S_POWER, S_POWER_DAMAGED, S_POWER_BUILD,
    S_REFINERY, S_REFINERY_DAMAGED, S_REFINERY_BUILD, S_BARRACKS, S_BARRACKS_DAMAGED, S_BARRACKS_BUILD,
    S_FACTORY, S_FACTORY_DAMAGED, S_FACTORY_BUILD,
    S_GUN_TURRET, S_GUN_TURRET_FIRE, S_GUN_TURRET_FIRE2, S_GUN_TURRET_BUILD,
    S_SCOUT, S_SCOUT_MOVE, S_SCOUT_FIRE, S_SCOUT_FIRE2,
    S_TANK, S_TANK_MOVE, S_TANK_FIRE, S_TANK_FIRE2,
    S_ARTILLERY, S_ARTILLERY_MOVE, S_ARTILLERY_FIRE, S_ARTILLERY_FIRE2,
    S_HARVESTER, S_HARVESTER_MOVE,
    S_APC, S_APC_MOVE, S_APC_FIRE, S_APC_FIRE2,
    S_RIFLEMAN, S_RIFLEMAN_WALK, S_RIFLEMAN_FIRE, S_RIFLEMAN_FIRE2, S_RIFLEMAN_DIE, S_RIFLEMAN_CORPSE,
    S_ROCKET, S_ROCKET_WALK, S_ROCKET_FIRE, S_ROCKET_FIRE2, S_ROCKET_DIE, S_ROCKET_CORPSE,
    S_FX_HIT, S_FX_BOOM, S_FX_BIGBOOM, S_FX_SMOKE, S_FX_PLUME, S_FX_FIRE,
    S_SHELL, S_SHELL_HIT, S_SHELL_HIT2,
    S_HEAVY_SHELL, S_HEAVY_SHELL_HIT, S_HEAVY_SHELL_HIT2,
    S_MISSILE, S_MISSILE_HIT, S_MISSILE_HIT2,
    NUMSTATES
} statenum_t;

enum {
    MT_NULL,
    MT_HQ, MT_POWER, MT_REFINERY, MT_BARRACKS, MT_FACTORY, MT_GUN_TURRET,
    MT_SCOUT, MT_TANK, MT_ARTILLERY, MT_HARVESTER, MT_APC,
    MT_RIFLEMAN, MT_ROCKET,
    /* Effects: pictures only (g_mission.c spawns them on damage and death). */
    MT_FX_HIT, MT_FX_BOOM, MT_FX_BIGBOOM, MT_FX_SMOKE, MT_FX_PLUME, MT_FX_FIRE,
    /* Projectiles (g_combat.c): they fly to where their target stood and hit it if it is still there. */
    MT_SHELL, MT_HEAVY_SHELL, MT_MISSILE,
    NUMMOBJTYPES,
};

/* Infantry logical frames, in the order of their sheet's animations (art/sprites.py). */
enum { INF_IDLE = 0, INF_IDLE_FRAMES = 4, INF_WALK = 4, INF_WALK_FRAMES = 6, INF_FIRE = 10,
       INF_DIE = 12, INF_DIE_FRAMES = 7, INF_FRAMES = 19 };
/* Construction stages on a structure's sheet (art/sprites.py BUILD_STAGES). */
enum { BUILD_STAGE_COUNT = 4 };

/* Vehicle frames (art/sprites.py MOVE and MOVE_FIRE): 0 standing, 1 the running gear a step
 * on, 2 the body's own gun firing. A turret sheet is 0 idle, 1 the shot, 2 run-out. */
enum { VEH_STAND = 0, VEH_STEP = 1, VEH_SHOT = 2, TURRET_SHOT = 1 };

extern const char *const sprnames[NUMSPRITES];
extern const state_t states[NUMSTATES];
extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];
extern const gameinfo_t game_info;

#endif
