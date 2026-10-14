#include "engine.h"
#include "info.h"

const char *const sprnames[NUMSPRITES] = {
    [SPR_RSC] = "SPRITES.RSC",
    [SPR_FORT] = "FORT",
#define Z_BUILDING(native, type, label, w, h, ...) [SPR_##type] = #type,
#include "buildings.inc"
#undef Z_BUILDING
#define Z_UNIT(t, type, label, kind, ...) [SPR_##type] = Z_SHEET_##kind(type),
#include "units.inc"
#undef Z_UNIT
};

/* Groups the engine reads: 0 stand, 2 walk, 3 attack; 4 is our death. */
#define UNIT_STATES(type, stand, walk, walks, fire, fires, shot, die, dies, dead, dead_tics) \
    [S_##type##_STND] = { SPR_##type, stand, 1, 4, A_Look, S_##type##_STND, 0, NULL }, \
    [S_##type##_WALK] = { SPR_##type, walk, walks, 3, A_Chase, S_##type##_WALK, 2, NULL }, \
    [S_##type##_FIRE] = { SPR_##type, fire, fires, 2, NULL, S_##type##_SHOT, 3, NULL }, \
    [S_##type##_SHOT] = { SPR_##type, shot, 1, 4, A_Attack, S_##type##_STND, 3, NULL }, \
    [S_##type##_DIE] = { SPR_##type, die, dies, 3, NULL, S_##type##_DEAD, 4, NULL }, \
    [S_##type##_DEAD] = { SPR_##type, dead, 1, dead_tics, NULL, S_NULL, 4, NULL },
/* Robots walk, aim, flash on the last fire frame and fall; their bodies
 * do not stay. */
#define STATES_ROBOT(type, sprite, turn, frames, dead) \
    UNIT_STATES(type, Z_ROBOT_STAND, Z_ROBOT_WALK, 4, Z_ROBOT_FIRE, 4, Z_ROBOT_FIRE + 4, \
                Z_ROBOT_DIE, Z_ROBOT_DIE_FRAMES - 1, Z_ROBOT_FRAMES - 1, 3 * RTS_TICRATE)
/* A gun's last frame is its muzzle flash; its wreck follows the turns. */
#define STATES_GUN(type, sprite, turn, frames, dead) \
    UNIT_STATES(type, 0, 0, 1, 0, 1, (frames) - 1, frames, 1, frames, 10 * RTS_TICRATE)
/* Vehicles roll through their track frames; the wreck follows the turns. */
#define STATES_VEHICLE(type, sprite, turn, frames, dead) \
    UNIT_STATES(type, 0, 0, frames, 0, 1, 0, frames, 1, frames, 10 * RTS_TICRATE)
/* One picture for everything but the wreck; no wreck art removes it. */
#define STATES_STILL(type, sprite, turn, frames, dead) \
    UNIT_STATES(type, sprite, sprite, 1, sprite, 1, sprite, (dead) < 0 ? (sprite) : (dead), 1, \
                (dead) < 0 ? (sprite) : (dead), (dead) < 0 ? 1 : 10 * RTS_TICRATE)

/* Objects and buildings keep the first native frame; units animate. */
const state_t states[NUMSTATES] = {
    [S_NULL] = { 0, 0, 0, -1, NULL, S_NULL, 0, NULL },
    [S_FORT] = { SPR_FORT, 0, 1, -1, NULL, S_FORT, 0, NULL },
    [S_FORT_CPU] = { SPR_FORT, 1, 1, -1, NULL, S_FORT_CPU, 0, NULL },
#define Z_BUILDING(native, type, label, w, h, ...) \
    [S_##type] = { SPR_##type, 0, 1, -1, NULL, S_##type, 0, NULL },
#include "buildings.inc"
#undef Z_BUILDING
#define Z_OBJECT(native, type, label, sprite, bits, w, h, dead) \
    [S_##type] = { SPR_RSC, sprite, 1, -1, NULL, S_##type, 0, NULL },
#include "objects.inc"
#undef Z_OBJECT
#define Z_UNIT(t, type, label, kind, mobile, hp, dmg, spd, range, reload, sprite, turn, frames, dead) \
    STATES_##kind(type, sprite, turn, frames, dead)
#include "units.inc"
#undef Z_UNIT
};

const mobjinfo_t mobjinfo[NUMMOBJTYPES] = {
#define Z_OBJECT(native, type, label, sprite, bits, w, h, dead) \
    [MT_##type] = { .doomednum = native, .spawnstate = S_##type, .spawnhealth = 100, \
                    .deathstate = S_NULL, .xdeathstate = S_NULL, .flags = MF_RENDERABLE },
#include "objects.inc"
#undef Z_OBJECT
    [MT_FORT] = { .doomednum = -1, .spawnstate = S_FORT, .spawnhealth = 2000,
                  .deathstate = S_NULL, .xdeathstate = S_NULL,
                  .flags = MF_SELECTABLE | MF_RENDERABLE },
#define Z_BUILDING(native, type, label, w, h, ...) \
    [MT_##type] = { .doomednum = native, .spawnstate = S_##type, .spawnhealth = 2000, \
                    .deathstate = S_NULL, .xdeathstate = S_NULL, \
                    .flags = MF_SELECTABLE | MF_RENDERABLE },
#include "buildings.inc"
#undef Z_BUILDING
#define Z_UNIT(t, type, label, kind, mobile, hp, ...) \
    [MT_UNIT_##type] = { .doomednum = -1, .spawnstate = S_##type##_STND, .spawnhealth = hp, \
                    .seestate = (mobile) ? S_##type##_WALK : S_##type##_STND, \
                    .missilestate = S_##type##_FIRE, .deathstate = S_##type##_DIE, \
                    .xdeathstate = S_##type##_DIE, .radius = UNIT_RADIUS_##kind, \
                    .flags = MF_SELECTABLE | MF_RENDERABLE },
#define UNIT_RADIUS_ROBOT 12 /* Pixels x 2: a robot is 16 wide, a vehicle 32. */
#define UNIT_RADIUS_GUN 16
#define UNIT_RADIUS_VEHICLE 16
#define UNIT_RADIUS_STILL 16
#include "units.inc"
#undef Z_UNIT
};

const gameinfo_t game_info = {
    .sprnames = sprnames, .sprite_count = NUMSPRITES,
    .states = states, .state_count = NUMSTATES,
    .mobjinfo = mobjinfo, .mobj_type_count = NUMMOBJTYPES,
    .null_state = S_NULL, .state_coord_mode = RTS_STATE_COORDS_GROUND_OFFSET,
    .selection_marker = { .style = SELECTION_STYLE_DEFAULT },
};
