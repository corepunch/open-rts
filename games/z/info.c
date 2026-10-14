#include "engine.h"
#include "z_local.h"
#include "info.h"

const char *const sprnames[NUMSPRITES] = {
    [SPR_RSC] = "SPRITES.RSC",
    [SPR_FORT] = "FORT",
    [SPR_FLAG] = "FLAG",
#define Z_BUILDING(native, type, label, w, h, ...) [SPR_##type] = #type,
#include "buildings.inc"
#undef Z_BUILDING
#define Z_UNIT(t, type, label, kind, ...) [SPR_##type] = Z_SHEET_##kind(type),
#include "units.inc"
#undef Z_UNIT
};

/* Groups the engine reads: 0 stand, 2 walk, 3 attack; 4 is our death.
 * Throwing a grenade is an attack of its own pictures (p_capture.c). A
 * unit that stands in several pictures steps through them every tic. */
#define UNIT_STATES(type, stand, stands, walk, walks, fire, fires, shot, die, dies, dead, dead_tics, \
                    throw, throws, toss, cheer, cheers) \
    [S_##type##_STND] = { SPR_##type, stand, stands, (stands) > 1 ? 1 : 4, A_Look, S_##type##_STND, 0, NULL }, \
    [S_##type##_WALK] = { SPR_##type, walk, walks, 3, A_Chase, S_##type##_WALK, 2, NULL }, \
    [S_##type##_FIRE] = { SPR_##type, fire, fires, 2, NULL, S_##type##_SHOT, 3, NULL }, \
    [S_##type##_SHOT] = { SPR_##type, shot, 1, 4, A_Attack, S_##type##_STND, 3, NULL }, \
    [S_##type##_DIE] = { SPR_##type, die, dies, 3, NULL, S_##type##_DEAD, 4, NULL }, \
    [S_##type##_DEAD] = { SPR_##type, dead, 1, dead_tics, NULL, S_NULL, 4, NULL }, \
    [S_##type##_THROW] = { SPR_##type, throw, throws, 2, NULL, S_##type##_TOSS, 3, NULL }, \
    [S_##type##_TOSS] = { SPR_##type, toss, 1, 2, A_Attack, S_##type##_STND, 3, NULL }, \
    [S_##type##_CHEER] = { SPR_##type, cheer, cheers, 4, NULL, S_##type##_STND, 0, NULL },
/* Robots walk, aim, flash on the last fire frame and fall; their bodies
 * do not stay. A grenade leaves on the throw's last frame (0x636a0). */
#define STATES_ROBOT(type, sprite, turn, frames, idle, dead) \
    UNIT_STATES(type, Z_ROBOT_STAND, 1, Z_ROBOT_WALK, 4, Z_ROBOT_FIRE, 4, Z_ROBOT_FIRE + 4, \
                Z_ROBOT_DIE, Z_ROBOT_DIE_FRAMES - 1, Z_ROBOT_DIE + Z_ROBOT_DIE_FRAMES - 1, \
                3 * RTS_TICRATE, Z_ROBOT_THROW, Z_ROBOT_THROW_FRAMES - 1, \
                Z_ROBOT_THROW + Z_ROBOT_THROW_FRAMES - 1, Z_ROBOT_CHEER, Z_ROBOT_CHEER_FRAMES)
/* A gun's last frame is its muzzle flash; its wreck follows the turns. */
#define STATES_GUN(type, sprite, turn, frames, idle, dead) \
    UNIT_STATES(type, 0, 1, 0, 1, 0, 1, (frames) - 1, frames, 1, frames, 10 * RTS_TICRATE, \
                0, 1, (frames) - 1, 0, 1)
/* Vehicles roll through their track frames and stand in the `idle`
 * pictures after them (w_rsc.c); the wreck follows. */
#define STATES_VEHICLE(type, sprite, turn, frames, idle, dead) \
    UNIT_STATES(type, frames, idle, 0, frames, 0, 1, 0, (frames) + (idle), 1, \
                (frames) + (idle), 10 * RTS_TICRATE, 0, 1, 0, 0, 1)
/* One picture for everything but the wreck; no wreck art removes it. */
#define STATES_STILL(type, sprite, turn, frames, idle, dead) \
    UNIT_STATES(type, sprite, 1, sprite, 1, sprite, 1, sprite, (dead) < 0 ? (sprite) : (dead), 1, \
                (dead) < 0 ? (sprite) : (dead), (dead) < 0 ? 1 : 10 * RTS_TICRATE, \
                sprite, 1, sprite, sprite, 1)

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
#define Z_UNIT(t, type, label, kind, mobile, hp, dmg, spd, range, reload, sprite, turn, frames, idle, dead) \
    STATES_##kind(type, sprite, turn, frames, idle, dead)
#include "units.inc"
#undef Z_UNIT
    /* The flag waves through four pictures, a step every second tick of
     * ZED.EXE's 70 Hz clock (0x19e81); that is drawn a little slower. */
    [S_FLAG] = { SPR_FLAG, 0, 4, 2, NULL, S_FLAG, 0, NULL },
    [S_GRENADE] = { SPR_RSC, Z_GRENADE_SPRITE, Z_GRENADE_FRAMES, 2, NULL, S_GRENADE, 0, NULL },
    /* It bursts on the first picture and burns out through the rest. */
    [S_GRENADE_BLAST] = { SPR_RSC, Z_BLAST_SPRITE, 1, 2, A_Explode, S_GRENADE_SMOKE, 4, NULL },
    [S_GRENADE_SMOKE] = { SPR_RSC, Z_BLAST_SPRITE + 1, Z_BLAST_FRAMES - 1, 2, NULL, S_NULL, 4, NULL },
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
    [MT_FLAG] = { .doomednum = -1, .spawnstate = S_FLAG, .deathstate = S_NULL,
                  .xdeathstate = S_NULL, .flags = MF_RENDERABLE },
    [MT_GRENADE] = { .doomednum = -1, .spawnstate = S_GRENADE, .deathstate = S_GRENADE_BLAST,
                     .xdeathstate = S_GRENADE_BLAST, .damage = Z_GRENADE_DAMAGE,
                     .flags = MF_RENDERABLE | MF_MISSILE },
};

const gameinfo_t game_info = {
    .sprnames = sprnames, .sprite_count = NUMSPRITES,
    .states = states, .state_count = NUMSTATES,
    .mobjinfo = mobjinfo, .mobj_type_count = NUMMOBJTYPES,
    .null_state = S_NULL, .state_coord_mode = RTS_STATE_COORDS_GROUND_OFFSET,
    .selection_marker = { .style = SELECTION_STYLE_DEFAULT },
    .team_palettes = true,
    .mobj_ticker = Z_MobjTicker,
    .attack = Z_Attack,
    .can_target = Z_CanTarget,
};
