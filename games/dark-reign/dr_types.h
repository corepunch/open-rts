#ifndef __DR_TYPES__
#define __DR_TYPES__

#include <stdbool.h>

typedef struct {
    const char *name;
    int recompute_strategy_period;
    int ground_unit_threat;
    int threat_priority;
    int distance_priority;
    int defend_buildings_priority;
    int attack_enemy_base_priority;
    int exploration_priority;
    int perimeter_priority;
    int resource_priority;
    int danger_priority;
    double min_matching_force_ratio;
    double max_matching_force_ratio;
    int min_building_defense_force;
    int max_building_defense_force;
    int min_exploration_force;
    int max_exploration_force;
    int min_perimeter_force;
    int max_perimeter_force;
    int min_resource_force;
    int max_resource_force;
    bool repair_buildings;
} ai_profile_t;

extern const ai_profile_t g_dark_reign_ai_profiles[];
extern const int g_dark_reign_ai_profile_count;

typedef struct {
    int tech_level;
    struct { int type, tech_level; } products[64];
    int product_count;
} dr_mission_t;

bool DR_ProductInTech(int type);

enum {
    /* Freedom Guard mobile units (UNITS.TXT SetType values). */
    ACTOR_FG_SPYDER_BIKE = 1,
    ACTOR_FG_MECHANIC = 2,
    ACTOR_FG_SABOTEUR = 3,
    ACTOR_FG_SPY = 4,
    ACTOR_FG_SUICIDE_NUKER = 5,
    ACTOR_FG_SCOUT = 6,
    ACTOR_FG_MEDIC = 7,
    ACTOR_FG_SNIPER = 8,
    ACTOR_FG_RAIDER = 9,
    ACTOR_FG_MERCENARY = 10,
    ACTOR_FG_CONSTRUCTION_CREW = 11,
    ACTOR_FG_MAD = 12,
    ACTOR_FG_GROUND_TRANSPORTER = 13,
    ACTOR_FG_HOVER_TRANSPORTER = 14,
    ACTOR_FG_IFV = 15,
    ACTOR_FG_TRIPLE_RAIL_TANK = 16,
    ACTOR_FG_TANK_HUNTER = 17,
    ACTOR_FG_SHOCKWAVE = 18,
    ACTOR_FG_SPA = 19,
    ACTOR_FG_MEDIUM_TANK = 20,
    ACTOR_FG_PHASE_TANK = 21,
    ACTOR_FG_UNDERGROUND_TUNNEL = 22,
    ACTOR_FG_SKY_BIKE = 23,
    ACTOR_FG_OUTRIDER = 24,
    ACTOR_FG_BASE_MOVER = 25,
    ACTOR_FG_CONTAMINATOR = 30,
    /* Freedom Guard building types (BUILD.TXT SetType values). */
    ACTOR_FG_HEADQUARTERS_2 = 10002,
    ACTOR_FG_HEADQUARTERS_3 = 10003,
    ACTOR_FG_TRAINING_FACILITY_1 = 10004,
    ACTOR_FG_TRAINING_FACILITY_2 = 10005,
    ACTOR_FG_VEHICLE_FACTORY_1 = 10006,
    ACTOR_FG_VEHICLE_FACTORY_2 = 10007,
    ACTOR_FG_HOVER_FACTORY = 10008,
    ACTOR_FG_REPAIR_BAY = 10009,
    ACTOR_FG_CAMERA_TOWER = 10010,
    ACTOR_FG_PHASE_FACTORY_1 = 10015,
    ACTOR_FG_PHASE_FACTORY_2 = 10016,
    ACTOR_FG_GUARD_TOWER = 10013,
    ACTOR_FG_ADVANCED_GUARD_TOWER = 10014,
    ACTOR_FG_AA_SITE = 10012,
    ACTOR_FG_POWER_PLANT = 10020,
    ACTOR_FG_LIFE_PLANT = 10019,
    ACTOR_FG_HOVER_OUTPOST = 10008,
    ACTOR_FG_REFINERY = 10011,
    ACTOR_FG_SMALL_HORIZONTAL_BRIDGE = 10040,
    ACTOR_FG_SMALL_VERTICAL_BRIDGE = 10041,
    ACTOR_FG_SMALL_CENTRE_BRIDGE = 10042,
    ACTOR_FG_HEADQUARTERS_1 = 10001,
    /* Imperium mobile units (UNITS.TXT SetType values). */
    ACTOR_IMP_SPY = 1001,
    ACTOR_IMP_STRIKE_MARINE = 1002,
    ACTOR_IMP_FIRE_SUPPORT_MARINE = 1003,
    ACTOR_IMP_HOVER_MARINE = 1004,
    ACTOR_IMP_CONSTRUCTION_CREW = 1005,
    ACTOR_IMP_GROUND_TRANSPORTER = 1006,
    ACTOR_IMP_HOVER_TRANSPORTER = 1007,
    ACTOR_IMP_AMPER = 1008,
    ACTOR_IMP_ASSAULT_VEHICLE = 1009,
    ACTOR_IMP_SCOUT_TANK = 1010,
    ACTOR_IMP_PLASMA_TANK = 1011,
    ACTOR_IMP_TACHYON_TANK = 1012,
    ACTOR_IMP_MAD = 1013,
    ACTOR_IMP_HOSTAGE_TAKER = 1014,
    ACTOR_IMP_SHREDDER = 1015,
    ACTOR_IMP_CONTAMINATOR = 1016,
    ACTOR_IMP_SPA = 1017,
    ACTOR_IMP_SKY_FORTRESS = 1018,
    ACTOR_IMP_RECON_SAUCER = 1019,
    ACTOR_IMP_VTOL = 1020,
    ACTOR_IMP_SHIELDED_SPA = 1026,
    ACTOR_IMP_SUICIDE_ZOMBIE = 1099,
    ACTOR_IMP_GUARD_TOWER = 5101,
    ACTOR_IMP_ADVANCED_GUARD_TOWER = 5102,
    ACTOR_IMP_AA_SITE = 5100,
    ACTOR_IMP_RIFT_CREATOR_UNIT = 11103,
    /* Civilian and neutral types (UNITS.TXT SetType values). */
    ACTOR_CIV_MALE = 3002,
    ACTOR_CIV_ROWDY = 3004,
    ACTOR_CIV_SPY = 3010,
    ACTOR_CIV_HOVER_TRANSPORTER = 3012,
    ACTOR_CIV_PRISONER = 3018,
    ACTOR_CIV_WHEEL_TRANSPORTER = 3019,
    ACTOR_CIV_JEB_RAD = 3116,
    ACTOR_CIV_KAROCH = 3117,
    ACTOR_CIV_COLONEL_MARTEL = 3120,
    /* Imperium building types (BUILD.TXT SetType values). */
    ACTOR_IMP_HEADQUARTERS_1 = 11001,
    ACTOR_IMP_HEADQUARTERS_2 = 11002,
    ACTOR_IMP_HEADQUARTERS_3 = 11003,
    ACTOR_IMP_TRAINING_FACILITY_1 = 11004,
    ACTOR_IMP_TRAINING_FACILITY_2 = 11005,
    ACTOR_IMP_VEHICLE_FACTORY_1 = 11006,
    ACTOR_IMP_VEHICLE_FACTORY_2 = 11007,
    ACTOR_IMP_TACHYON_PLANT = 11008,
    ACTOR_IMP_HOVER_FACTORY = 11009,
    ACTOR_IMP_REPAIR_BAY = 11010,
    ACTOR_IMP_CAMERA_TOWER = 11011,
    ACTOR_IMP_REFINERY = 11012,
    ACTOR_IMP_AA_BUILDING = 11013,
    ACTOR_IMP_GUARD_TOWER_BUILDING = 11014,
    ACTOR_IMP_ADVANCED_GUARD_TOWER_BUILDING = 11015,
    ACTOR_IMP_LIFE_PLANT = 11019,
    ACTOR_IMP_POWER_PLANT = 11020,
    ACTOR_IMP_RIFT_CREATOR = 11021,
    ACTOR_IMP_SMALL_HORIZONTAL_BRIDGE = 11040,
    ACTOR_IMP_SMALL_VERTICAL_BRIDGE = 11041,
    ACTOR_IMP_SMALL_CENTRE_BRIDGE = 11042,
};

#endif
