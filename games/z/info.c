#include "engine.h"
#include "info.h"

const char *const sprnames[NUMSPRITES] = {
    [SPR_RSC] = "SPRITES.RSC",
    [SPR_FORT] = "FORT",
#define Z_BUILDING(native, type, label, w, h, ...) [SPR_##type] = #type,
#include "buildings.inc"
#undef Z_BUILDING
};

/* Still pictures for now: the first native frame of each object. */
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
};

const gameinfo_t game_info = {
    .sprnames = sprnames, .sprite_count = NUMSPRITES,
    .states = states, .state_count = NUMSTATES,
    .mobjinfo = mobjinfo, .mobj_type_count = NUMMOBJTYPES,
    .null_state = S_NULL, .state_coord_mode = RTS_STATE_COORDS_GROUND_OFFSET,
    .selection_marker = { .style = SELECTION_STYLE_DEFAULT },
};
