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
enum { S_NULL, SC_TYPES = 228, NUMMOBJTYPES = 229, SC_STATES = 8192 };
extern state_t states[SC_STATES];
extern mobjinfo_t mobjinfo[NUMMOBJTYPES];
extern const char *sprnames[SC_TYPES];
extern gameinfo_t game_info;
#endif
