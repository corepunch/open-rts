/* Generated from the verified 7th Legion BIM actor catalog. Do not edit by hand. */
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
    "bt_base",
    "bt_power",
    "bt_barracks",
    "bt_wall",
    "bt_hospital",
    "bt_tank",
    "bt_randd",
    "bt_repair",
    "bt_robot",
};

const state_t states[NUMSTATES] = {
    { 0, 0, 0, -1, NULL, S_NULL, 0, NULL },
    [S_LTROOP_STND] = { SPR_LTROOP, 0, 1, 1, A_Look, S_LTROOP_STND, 0, NULL },
    [S_LTROOP_FIRE] = { SPR_LTROOP, 0, 1, 1, A_Attack, S_LTROOP_STND, 3, NULL },
    [S_SLAVEN1_STND] = { SPR_SLAVEN1, 0, 1, -1, NULL, S_SLAVEN1_STND, 0, NULL },
    [S_SPIDER_STND] = { SPR_SPIDER, 0, 1, 1, A_Look, S_SPIDER_STND, 0, NULL },
    [S_SPIDER_FIRE] = { SPR_SPIDER, 0, 1, 1, A_Attack, S_SPIDER_STND, 3, NULL },
    [S_TANKBASE_STND] = { SPR_TANKBASE, 0, 1, 1, A_Look, S_TANKBASE_STND, 0, NULL },
    [S_TANKBASE_FIRE] = { SPR_TANKBASE, 0, 1, 1, A_Attack, S_TANKBASE_STND, 3, NULL },
    [S_ROCKMECH_STND] = { SPR_ROCKMECH, 0, 1, 1, A_Look, S_ROCKMECH_STND, 0, NULL },
    [S_ROCKMECH_FIRE] = { SPR_ROCKMECH, 0, 1, 1, A_Attack, S_ROCKMECH_STND, 3, NULL },
    [S_TRUCK_STND] = { SPR_TRUCK, 0, 1, -1, NULL, S_TRUCK_STND, 0, NULL },
    [S_MOBBASE_STND] = { SPR_MOBBASE, 0, 1, -1, NULL, S_MOBBASE_STND, 0, NULL },
    [S_BASE_STND] = { SPR_BASE, 0, 1, -1, NULL, S_BASE_STND, 0, NULL },
    [S_POWER_STND] = { SPR_POWER, 0, 1, -1, NULL, S_POWER_STND, 0, NULL },
    [S_BARRACKS_STND] = { SPR_BARRACKS, 0, 1, -1, NULL, S_BARRACKS_STND, 0, NULL },
    [S_WALL_STND] = { SPR_WALL, 0, 1, -1, NULL, S_WALL_STND, 0, NULL },
    [S_HOSPITAL_STND] = { SPR_HOSPITAL, 0, 1, -1, NULL, S_HOSPITAL_STND, 0, NULL },
    [S_TANK_FACTORY_STND] = { SPR_TANK_FACTORY, 0, 1, -1, NULL, S_TANK_FACTORY_STND, 0, NULL },
    [S_RESEARCH_STND] = { SPR_RESEARCH, 0, 1, -1, NULL, S_RESEARCH_STND, 0, NULL },
    [S_REPAIR_STND] = { SPR_REPAIR, 0, 1, -1, NULL, S_REPAIR_STND, 0, NULL },
    [S_ROBOT_FACTORY_STND] = { SPR_ROBOT_FACTORY, 0, 1, -1, NULL, S_ROBOT_FACTORY_STND, 0, NULL },
};

const mobjinfo_t mobjinfo[NUMMOBJTYPES] = {
    { 0 },
    { // MT_TROOPER
        .doomednum = 1, .spawnstate = S_LTROOP_STND, .spawnhealth = 100,
        .seestate = S_LTROOP_STND, .speed = 4,
        .missilestate = S_LTROOP_FIRE, .damage = 15,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .radius = 16, .height = 32, .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_SLAVE
        .doomednum = 2, .spawnstate = S_SLAVEN1_STND, .spawnhealth = 60,
        .seestate = S_SLAVEN1_STND, .speed = 4,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .radius = 16, .height = 32, .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_HARVESTER,
    },
    { // MT_SPIDER_MECH
        .doomednum = 3, .spawnstate = S_SPIDER_STND, .spawnhealth = 300,
        .seestate = S_SPIDER_STND, .speed = 3,
        .missilestate = S_SPIDER_FIRE, .damage = 35,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .radius = 16, .height = 32, .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_TANK
        .doomednum = 4, .spawnstate = S_TANKBASE_STND, .spawnhealth = 500,
        .seestate = S_TANKBASE_STND, .speed = 5,
        .missilestate = S_TANKBASE_FIRE, .damage = 50,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .radius = 16, .height = 32, .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_ROCK_MECH
        .doomednum = 5, .spawnstate = S_ROCKMECH_STND, .spawnhealth = 800,
        .seestate = S_ROCKMECH_STND, .speed = 3,
        .missilestate = S_ROCKMECH_FIRE, .damage = 70,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .radius = 16, .height = 32, .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_TRUCK
        .doomednum = 6, .spawnstate = S_TRUCK_STND, .spawnhealth = 200,
        .seestate = S_TRUCK_STND, .speed = 5,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .radius = 16, .height = 32, .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_HARVESTER,
    },
    { // MT_MOBILE_BASE
        .doomednum = 7, .spawnstate = S_MOBBASE_STND, .spawnhealth = 1000,
        .seestate = S_MOBBASE_STND, .speed = 3,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .radius = 16, .height = 32, .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE,
    },
    { // MT_BASE
        .doomednum = 1000 + 0, .spawnstate = S_BASE_STND, .spawnhealth = 3000,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_POWER
        .doomednum = 1000 + 2, .spawnstate = S_POWER_STND, .spawnhealth = 750,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_BARRACKS
        .doomednum = 1000 + 5, .spawnstate = S_BARRACKS_STND, .spawnhealth = 950,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_WALL
        .doomednum = 1000 + 6, .spawnstate = S_WALL_STND, .spawnhealth = 200,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_HOSPITAL
        .doomednum = 1000 + 8, .spawnstate = S_HOSPITAL_STND, .spawnhealth = 750,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_TANK_FACTORY
        .doomednum = 1000 + 9, .spawnstate = S_TANK_FACTORY_STND, .spawnhealth = 1800,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_RESEARCH
        .doomednum = 1000 + 11, .spawnstate = S_RESEARCH_STND, .spawnhealth = 1100,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_REPAIR
        .doomednum = 1000 + 12, .spawnstate = S_REPAIR_STND, .spawnhealth = 850,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_ROBOT_FACTORY
        .doomednum = 1000 + 13, .spawnstate = S_ROBOT_FACTORY_STND, .spawnhealth = 2000,
        .deathstate = S_NULL, .xdeathstate = S_NULL,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
};

const gameinfo_t game_info = {
    sprnames, NUMSPRITES, states, NUMSTATES, mobjinfo, NUMMOBJTYPES,
    S_NULL, RTS_STATE_COORDS_GROUND_OFFSET,
    { .style = SELECTION_STYLE_DEFAULT },
    NULL,
    .right_click_orders = false,
};
