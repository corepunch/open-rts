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
};

#endif
