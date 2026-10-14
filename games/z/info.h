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

/* SPRITES.RSC is one sheet whose frame n is native sprite n. Forts and
 * buildings change with the world, so they get sheets of their own. */
typedef enum {
    SPR_RSC,
    SPR_FORT,
#define Z_BUILDING(native, type, label, w, h, ...) SPR_##type,
#include "buildings.inc"
#undef Z_BUILDING
    NUMSPRITES
} spritenum_t;

typedef enum {
    S_NULL,
    S_FORT,
    S_FORT_CPU,
#define Z_BUILDING(native, type, label, w, h, ...) S_##type,
#include "buildings.inc"
#undef Z_BUILDING
#define Z_OBJECT(native, type, label, sprite, flags, w, h, dead) S_##type,
#include "objects.inc"
#undef Z_OBJECT
    NUMSTATES
} statenum_t;

/* Object types come first, so native OBJECTnn.DAT type n is type n + 1. */
enum {
    MT_NULL,
#define Z_OBJECT(native, type, label, sprite, flags, w, h, dead) MT_##type,
#include "objects.inc"
#undef Z_OBJECT
    MT_FORT,
#define Z_BUILDING(native, type, label, w, h, ...) MT_##type,
#include "buildings.inc"
#undef Z_BUILDING
    NUMMOBJTYPES
};

extern const char *const sprnames[NUMSPRITES];
extern const state_t states[NUMSTATES];
extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];
extern const gameinfo_t game_info;

#endif
