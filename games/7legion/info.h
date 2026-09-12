/* Generated from the verified 7th Legion BIM actor catalog. Do not edit by hand. */
#ifndef __INFO__
#define __INFO__

#include "actor.h"

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
    NUMMOBJTYPES,
};

extern const char *const sprnames[NUMSPRITES];
extern const state_t states[NUMSTATES];
extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];
extern const gameinfo_t game_info;

#endif
