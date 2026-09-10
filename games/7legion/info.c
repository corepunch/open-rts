#include "engine.h"
#include "info.h"

const char *const sprnames[NUMSPRITES] = {
    "GFX/LTROOP.BIM",
    "GFX/SLAVEN1.BIM",
    "GFX/SPIDER.BIM",
    "GFX/TANKBASE.BIM",
    "GFX/ROCKMECH.BIM",
    "GFX/TRUCK.BIM",
    "GFX/MOBBASE.BIM",
};

const state_t states[NUMSTATES] = {
    { 0, 0, -1, NULL, S_NULL, 0 },                              /* S_NULL         */
    { SPR_LTROOP,   0, -1, NULL, S_LTROOP_STND,   0 },          /* S_LTROOP_STND  */
    { SPR_SLAVEN1,  0, -1, NULL, S_SLAVEN1_STND,  0 },          /* S_SLAVEN1_STND */
    { SPR_SPIDER,   0, -1, NULL, S_SPIDER_STND,   0 },          /* S_SPIDER_STND  */
    { SPR_TANKBASE, 0, -1, NULL, S_TANKBASE_STND, 0 },          /* S_TANKBASE_STND */
    { SPR_ROCKMECH, 0, -1, NULL, S_ROCKMECH_STND, 0 },          /* S_ROCKMECH_STND */
    { SPR_TRUCK,    0, -1, NULL, S_TRUCK_STND,    0 },          /* S_TRUCK_STND   */
    { SPR_MOBBASE,  0, -1, NULL, S_MOBBASE_STND,  0 },          /* S_MOBBASE_STND */
};

const mobjinfo_t mobjinfo[NUMMOBJTYPES] = {
    { // MT_NULL
        0,
    },
    { // MT_TROOPER
        .doomednum    = 1,
        .spawnstate   = S_LTROOP_STND,
        .spawnhealth  = 100,
        .seestate     = S_LTROOP_STND,
        .missilestate = S_LTROOP_STND,
        .deathstate   = S_NULL,
        .xdeathstate  = S_NULL,
        .speed = 4,
        .radius = 16,
        .height = 32,
        .mass   = 100,
        .damage = 15,
        .flags  = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_SLAVE
        .doomednum    = 2,
        .spawnstate   = S_SLAVEN1_STND,
        .spawnhealth  = 60,
        .seestate     = S_SLAVEN1_STND,
        .deathstate   = S_NULL,
        .xdeathstate  = S_NULL,
        .speed = 4,
        .radius = 16,
        .height = 32,
        .mass   = 100,
        .flags  = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_HARVESTER,
    },
    { // MT_SPIDER_MECH
        .doomednum    = 3,
        .spawnstate   = S_SPIDER_STND,
        .spawnhealth  = 300,
        .seestate     = S_SPIDER_STND,
        .missilestate = S_SPIDER_STND,
        .deathstate   = S_NULL,
        .xdeathstate  = S_NULL,
        .speed = 3,
        .radius = 16,
        .height = 32,
        .mass   = 100,
        .damage = 35,
        .flags  = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_TANK
        .doomednum    = 4,
        .spawnstate   = S_TANKBASE_STND,
        .spawnhealth  = 500,
        .seestate     = S_TANKBASE_STND,
        .missilestate = S_TANKBASE_STND,
        .deathstate   = S_NULL,
        .xdeathstate  = S_NULL,
        .speed = 5,
        .radius = 16,
        .height = 32,
        .mass   = 100,
        .damage = 50,
        .flags  = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_ROCK_MECH
        .doomednum    = 5,
        .spawnstate   = S_ROCKMECH_STND,
        .spawnhealth  = 800,
        .seestate     = S_ROCKMECH_STND,
        .missilestate = S_ROCKMECH_STND,
        .deathstate   = S_NULL,
        .xdeathstate  = S_NULL,
        .speed = 3,
        .radius = 16,
        .height = 32,
        .mass   = 100,
        .damage = 70,
        .flags  = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_TRUCK
        .doomednum    = 6,
        .spawnstate   = S_TRUCK_STND,
        .spawnhealth  = 200,
        .seestate     = S_TRUCK_STND,
        .deathstate   = S_NULL,
        .xdeathstate  = S_NULL,
        .speed = 5,
        .radius = 16,
        .height = 32,
        .mass   = 100,
        .flags  = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_HARVESTER,
    },
    { // MT_MOBILE_BASE
        .doomednum    = 7,
        .spawnstate   = S_MOBBASE_STND,
        .spawnhealth  = 1000,
        .seestate     = S_MOBBASE_STND,
        .deathstate   = S_NULL,
        .xdeathstate  = S_NULL,
        .speed = 3,
        .radius = 16,
        .height = 32,
        .mass   = 100,
        .flags  = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE,
    },
};

const gameinfo_t game_info = {
    sprnames,
    NUMSPRITES,
    states,
    NUMSTATES,
    mobjinfo,
    NUMMOBJTYPES,
    S_NULL,
    RTS_STATE_COORDS_GROUND_OFFSET,
    { .style = SELECTION_STYLE_CIRCLE },
    NULL,
};
