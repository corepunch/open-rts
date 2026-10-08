/* Generated from the verified 7th Legion BIM actor catalog. Do not edit by hand. */
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

typedef enum {
    SPR_LTROOP,
    SPR_SLAVEN1,
    SPR_SPIDER,
    SPR_TANKBASE,
    SPR_ROCKMECH,
    SPR_TRUCK,
    SPR_MOBBASE,
    SPR_BASE,
    SPR_POWER,
    SPR_BARRACKS,
    SPR_WALL,
    SPR_HOSPITAL,
    SPR_TANK_FACTORY,
    SPR_RESEARCH,
    SPR_REPAIR,
    SPR_ROBOT_FACTORY,
    NUMSPRITES
} spritenum_t;

typedef enum {
    S_NULL = 0,
    S_LTROOP_STND,
    S_LTROOP_FIRE,
    S_SLAVEN1_STND,
    S_SPIDER_STND,
    S_SPIDER_FIRE,
    S_TANKBASE_STND,
    S_TANKBASE_FIRE,
    S_ROCKMECH_STND,
    S_ROCKMECH_FIRE,
    S_TRUCK_STND,
    S_MOBBASE_STND,
    S_BASE_STND,
    S_POWER_STND,
    S_BARRACKS_STND,
    S_WALL_STND,
    S_HOSPITAL_STND,
    S_TANK_FACTORY_STND,
    S_RESEARCH_STND,
    S_REPAIR_STND,
    S_ROBOT_FACTORY_STND,
    NUMSTATES
} statenum_t;

enum {
    MT_NULL,
    MT_TROOPER,
    MT_SLAVE,
    MT_SPIDER_MECH,
    MT_TANK,
    MT_ROCK_MECH,
    MT_TRUCK,
    MT_MOBILE_BASE,
    MT_BASE,
    MT_POWER,
    MT_BARRACKS,
    MT_WALL,
    MT_HOSPITAL,
    MT_TANK_FACTORY,
    MT_RESEARCH,
    MT_REPAIR,
    MT_ROBOT_FACTORY,
    NUMMOBJTYPES,
};

extern const char *const sprnames[NUMSPRITES];
extern const state_t states[NUMSTATES];
extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];
extern const gameinfo_t game_info;

#endif
