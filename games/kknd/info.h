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
    /* Survivor infantry */
    SPR_SURV_RIFLEMAN,
    SPR_SURV_FLAMER,
    SPR_SURV_SWAT,
    SPR_SURV_SAPPER,
    SPR_SURV_SABOTEUR,
    SPR_SURV_TECHNICIAN,
    SPR_SURV_RPG_LAUNCHER,
    SPR_SURV_SNIPER,
    /* Mutant infantry */
    SPR_MUTE_BERSERKER,
    SPR_MUTE_PYROMANIAC,
    SPR_MUTE_SHOTGUNNER,
    SPR_MUTE_RIOTER,
    SPR_MUTE_VANDAL,
    SPR_MUTE_MEKANIK,
    SPR_MUTE_BAZOOKA,
    SPR_MUTE_CRAZY_HARRY,
    /* Survivor vehicles */
    SPR_SURV_DIRT_BIKE,
    SPR_SURV_4X4_PICKUP,
    SPR_SURV_ATV,
    SPR_SURV_ATV_FLAMETHROWER,
    SPR_SURV_ANACONDA_TANK,
    SPR_SURV_BARRAGE_CRAFT,
    SPR_SURV_AUTOCANNON_TANK,
    SPR_SURV_MOBILE_DERRICK,
    SPR_SURV_OIL_TANKER,
    SPR_SURV_MOBILE_OUTPOST,
    /* Mutant vehicles */
    SPR_MUTE_DIRE_WOLF,
    SPR_MUTE_BIKE_SIDECAR,
    SPR_MUTE_MONSTER_TRUCK,
    SPR_MUTE_GIANT_SCORPION,
    SPR_MUTE_WAR_MASTADONT,
    SPR_MUTE_GIANT_BEETLE,
    SPR_MUTE_MISSILE_CRAB,
    SPR_MUTE_MOBILE_DERRICK,
    SPR_MUTE_OIL_TANKER,
    SPR_MUTE_CLANHALL_WAGON,
    /* Survivor buildings */
    SPR_SURV_DRILLRIG,
    SPR_SURV_POWER_STATION,
    SPR_SURV_OUTPOST,
    SPR_SURV_MACHINE_SHOP,
    SPR_SURV_REPAIR_BAY,
    SPR_SURV_RESEARCH_LAB,
    /* Mutant buildings */
    SPR_MUTE_DRILLRIG,
    SPR_MUTE_POWER_STATION,
    SPR_MUTE_CLANHALL,
    SPR_MUTE_BLACKSMITH,
    SPR_MUTE_BEAST_ENCLOSURE,
    SPR_MUTE_MENAGERIE,
    SPR_MUTE_ALCHEMY_HALL,
    /* Survivor towers */
    SPR_SURV_GUARD_TOWER,
    SPR_SURV_MISSILE_BATTERY,
    SPR_SURV_CANNON_TOWER,
    /* Mutant towers */
    SPR_MUTE_MACHINEGUN_NEST,
    SPR_MUTE_GRAPESHOT_TOWER,
    SPR_MUTE_ROTARY_CANNON,
    /* Air */
    SPR_SURV_BOMBER,
    SPR_MUTE_WASP,
    NUMSPRITES
} spritenum_t;

typedef enum {
    S_NULL = 0,
    /* Survivor infantry */
    S_SURV_RIFLEMAN_STND,
    S_SURV_FLAMER_STND,
    S_SURV_SWAT_STND,
    S_SURV_SAPPER_STND,
    S_SURV_SABOTEUR_STND,
    S_SURV_TECHNICIAN_STND,
    S_SURV_RPG_LAUNCHER_STND,
    S_SURV_SNIPER_STND,
    /* Mutant infantry */
    S_MUTE_BERSERKER_STND,
    S_MUTE_PYROMANIAC_STND,
    S_MUTE_SHOTGUNNER_STND,
    S_MUTE_RIOTER_STND,
    S_MUTE_VANDAL_STND,
    S_MUTE_MEKANIK_STND,
    S_MUTE_BAZOOKA_STND,
    S_MUTE_CRAZY_HARRY_STND,
    /* Survivor vehicles */
    S_SURV_DIRT_BIKE_STND,
    S_SURV_4X4_PICKUP_STND,
    S_SURV_ATV_STND,
    S_SURV_ATV_FLAMETHROWER_STND,
    S_SURV_ANACONDA_TANK_STND,
    S_SURV_BARRAGE_CRAFT_STND,
    S_SURV_AUTOCANNON_TANK_STND,
    S_SURV_MOBILE_DERRICK_STND,
    S_SURV_OIL_TANKER_STND,
    S_SURV_MOBILE_OUTPOST_STND,
    /* Mutant vehicles */
    S_MUTE_DIRE_WOLF_STND,
    S_MUTE_BIKE_SIDECAR_STND,
    S_MUTE_MONSTER_TRUCK_STND,
    S_MUTE_GIANT_SCORPION_STND,
    S_MUTE_WAR_MASTADONT_STND,
    S_MUTE_GIANT_BEETLE_STND,
    S_MUTE_MISSILE_CRAB_STND,
    S_MUTE_MOBILE_DERRICK_STND,
    S_MUTE_OIL_TANKER_STND,
    S_MUTE_CLANHALL_WAGON_STND,
    /* Survivor buildings */
    S_SURV_DRILLRIG_STND,
    S_SURV_POWER_STATION_STND,
    S_SURV_OUTPOST_STND,
    S_SURV_MACHINE_SHOP_STND,
    S_SURV_REPAIR_BAY_STND,
    S_SURV_RESEARCH_LAB_STND,
    /* Mutant buildings */
    S_MUTE_DRILLRIG_STND,
    S_MUTE_POWER_STATION_STND,
    S_MUTE_CLANHALL_STND,
    S_MUTE_BLACKSMITH_STND,
    S_MUTE_BEAST_ENCLOSURE_STND,
    S_MUTE_MENAGERIE_STND,
    S_MUTE_ALCHEMY_HALL_STND,
    /* Survivor towers */
    S_SURV_GUARD_TOWER_STND,
    S_SURV_MISSILE_BATTERY_STND,
    S_SURV_CANNON_TOWER_STND,
    /* Mutant towers */
    S_MUTE_MACHINEGUN_NEST_STND,
    S_MUTE_GRAPESHOT_TOWER_STND,
    S_MUTE_ROTARY_CANNON_STND,
    /* Air */
    S_SURV_BOMBER_STND,
    S_MUTE_WASP_STND,
    NUMSTATES
} statenum_t;

/* doomednum = UNIT_STATS_* index from OpenKKND UNIT_ID enum */
enum {
    MT_NULL,
    /* Survivor infantry */
    MT_SURV_RIFLEMAN,       /* 0  */
    MT_SURV_FLAMER,         /* 2  */
    MT_SURV_SWAT,           /* 4  */
    MT_SURV_SAPPER,         /* 6  */
    MT_SURV_SABOTEUR,       /* 10 */
    MT_SURV_TECHNICIAN,     /* 12 */
    MT_SURV_RPG_LAUNCHER,   /* 14 */
    MT_SURV_SNIPER,         /* 16 */
    /* Mutant infantry */
    MT_MUTE_BERSERKER,      /* 1  */
    MT_MUTE_PYROMANIAC,     /* 3  */
    MT_MUTE_SHOTGUNNER,     /* 5  */
    MT_MUTE_RIOTER,         /* 7  */
    MT_MUTE_VANDAL,         /* 11 */
    MT_MUTE_MEKANIK,        /* 13 */
    MT_MUTE_BAZOOKA,        /* 15 */
    MT_MUTE_CRAZY_HARRY,    /* 17 */
    /* Survivor vehicles */
    MT_SURV_DIRT_BIKE,      /* 26 */
    MT_SURV_4X4_PICKUP,     /* 28 */
    MT_SURV_ATV,            /* 30 */
    MT_SURV_ATV_FLAMETHROWER, /* 32 */
    MT_SURV_ANACONDA_TANK,  /* 34 */
    MT_SURV_BARRAGE_CRAFT,  /* 36 */
    MT_SURV_AUTOCANNON_TANK, /* 38 */
    MT_SURV_MOBILE_DERRICK, /* 21 */
    MT_SURV_OIL_TANKER,     /* 23 */
    MT_SURV_MOBILE_OUTPOST, /* 40 */
    /* Mutant vehicles */
    MT_MUTE_DIRE_WOLF,      /* 27 */
    MT_MUTE_BIKE_SIDECAR,   /* 29 */
    MT_MUTE_MONSTER_TRUCK,  /* 31 */
    MT_MUTE_GIANT_SCORPION, /* 33 */
    MT_MUTE_WAR_MASTADONT,  /* 35 */
    MT_MUTE_GIANT_BEETLE,   /* 37 */
    MT_MUTE_MISSILE_CRAB,   /* 39 */
    MT_MUTE_MOBILE_DERRICK, /* 22 */
    MT_MUTE_OIL_TANKER,     /* 24 */
    MT_MUTE_CLANHALL_WAGON, /* 41 */
    /* Survivor buildings */
    MT_SURV_DRILLRIG,       /* 46 */
    MT_SURV_POWER_STATION,  /* 48 */
    MT_SURV_OUTPOST,        /* 58 */
    MT_SURV_MACHINE_SHOP,   /* 60 */
    MT_SURV_REPAIR_BAY,     /* 63 */
    MT_SURV_RESEARCH_LAB,   /* 65 */
    /* Mutant buildings */
    MT_MUTE_DRILLRIG,       /* 47 */
    MT_MUTE_POWER_STATION,  /* 49 */
    MT_MUTE_CLANHALL,       /* 59 */
    MT_MUTE_BLACKSMITH,     /* 61 */
    MT_MUTE_BEAST_ENCLOSURE, /* 62 */
    MT_MUTE_MENAGERIE,      /* 64 */
    MT_MUTE_ALCHEMY_HALL,   /* 66 */
    /* Survivor towers */
    MT_SURV_GUARD_TOWER,    /* 52 */
    MT_SURV_MISSILE_BATTERY, /* 56 */
    MT_SURV_CANNON_TOWER,   /* 54 */
    /* Mutant towers */
    MT_MUTE_MACHINEGUN_NEST, /* 53 */
    MT_MUTE_GRAPESHOT_TOWER, /* 55 */
    MT_MUTE_ROTARY_CANNON,  /* 57 */
    /* Air */
    MT_SURV_BOMBER,         /* 44 */
    MT_MUTE_WASP,           /* 43 */
    NUMMOBJTYPES,
};

extern const char *const sprnames[NUMSPRITES];
extern const state_t states[NUMSTATES];
extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];
extern const gameinfo_t game_info;

#endif
