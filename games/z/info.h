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
 * buildings change with the world, so they get sheets of their own. Each
 * unit names a sheet: robots share "ROBOT", guns and vehicles turn on
 * their own and still units draw from SPRITES.RSC. */
typedef enum {
    SPR_RSC,
    SPR_FORT,
#define Z_BUILDING(native, type, label, w, h, ...) SPR_##type,
#include "buildings.inc"
#undef Z_BUILDING
#define Z_UNIT(t, type, ...) SPR_##type,
#include "units.inc"
#undef Z_UNIT
    NUMSPRITES
} spritenum_t;

#define Z_SHEET_ROBOT(type) "ROBOT"
#define Z_SHEET_GUN(type) #type
#define Z_SHEET_VEHICLE(type) #type
#define Z_SHEET_STILL(type) "SPRITES.RSC"

/* Frames of the robot sheet: eight-way stand, walk and fire runs from the
 * ZED.EXE action table at 0x14b10c (actions 0, 2 and 5), then the death. */
enum {
    Z_ROBOT_STAND = 0, Z_ROBOT_WALK = 1, Z_ROBOT_FIRE = 5, Z_ROBOT_DIE = 10,
    Z_ROBOT_DIE_FRAMES = 11, Z_ROBOT_FRAMES = Z_ROBOT_DIE + Z_ROBOT_DIE_FRAMES,
};

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
#define Z_UNIT(t, type, ...) S_##type##_STND, S_##type##_WALK, S_##type##_FIRE, \
    S_##type##_SHOT, S_##type##_DIE, S_##type##_DEAD,
#include "units.inc"
#undef Z_UNIT
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
    MT_UNITS, /* Units follow in ROBOTS.DAT weapon order: T is MT_UNITS + T. */
#define Z_UNIT(t, type, ...) MT_UNIT_##type = MT_UNITS + t,
#include "units.inc"
#undef Z_UNIT
    NUMMOBJTYPES
};

extern const char *const sprnames[NUMSPRITES];
extern const state_t states[NUMSTATES];
extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];
extern const gameinfo_t game_info;

#endif
