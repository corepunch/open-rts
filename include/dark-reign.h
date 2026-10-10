#ifndef __DARK_REIGN__
#define __DARK_REIGN__

#include <stdbool.h>
#include "engine.h"


/* TRNEFF.TXT effect types: one terrain speed table per movement class. Class 0 is
 * "plain" (blocked[] only) and is used by anything without UseEffects. */
enum {
    DR_MOVE_PLAIN = 0,
    DR_MOVE_WHEEL, DR_MOVE_WHEELF, DR_MOVE_WHEELA, DR_MOVE_TRACK, DR_MOVE_FOOT,
    DR_MOVE_HOVER, DR_MOVE_HOVERS, DR_MOVE_FLYING, DR_MOVE_LEGGEDDROID,
    DR_MOVE_COUNT
};

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
    fixed_t min_matching_force_ratio; /* 16.16 */
    fixed_t max_matching_force_ratio; /* 16.16 */
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
    ivec2_t bays[]; /* Native SetBay by mobj type; (-1,-1) means no bay. */
} dr_mission_t;

bool DR_ProductInTech(int type);
bool DR_PrerequisiteMet(int owner, int id);
/* A game-setup ("Chat") screen choice for one map team, as its native
 * ChatPlayerType and ChatPlayerSide rows. */
typedef enum { DR_SLOT_AVAILABLE, DR_SLOT_HUMAN, DR_SLOT_EASY, DR_SLOT_MEDIUM, DR_SLOT_HARD,
               DR_SLOT_CLOSED } dr_slottype_t;
typedef enum { DR_SIDE_DEFAULT, DR_SIDE_FG, DR_SIDE_IMPERIUM } dr_side_t;
typedef struct {
    uint8_t type, side, team; /* team 0 is "No Team", 1..8 Team A..H */
    char name[24];
} dr_slot_t;
typedef struct {
    dr_slot_t slots[8];
    int count;   /* the map's player count */
    int credits; /* ChatCreditsEd; 0 keeps each team's SetCredit */
} dr_skirmish_t;
/* The menu names the next map's setup; its loader takes it once. */
void DR_RequestSkirmish(const char *map, const dr_skirmish_t *setup);
bool DR_TakeSkirmish(const char *map, dr_skirmish_t *setup);
const dr_skirmish_t *DR_LevelSkirmish(void);

/* The shell's options screen over a running level. */
void DR_OpenOptions(app_t *app);
const char *DR_String(const char *name);

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
    /* Freedom Guard decoy buildings (BUILD.TXT SetType values). */
    ACTOR_FG_VEHICLE_FACTORY_1_DECOY = 10021,
    ACTOR_FG_VEHICLE_FACTORY_2_DECOY = 10022,
    ACTOR_FG_PHASE_FACTORY_1_DECOY = 10023,
    ACTOR_FG_PHASE_FACTORY_2_DECOY = 10024,
    ACTOR_FG_HEADQUARTERS_1_DECOY = 10101,
    ACTOR_FG_HEADQUARTERS_2_DECOY = 10102,
    ACTOR_FG_HEADQUARTERS_3_DECOY = 10103,
    ACTOR_FG_TRAINING_FACILITY_2_DECOY = 10105,
    ACTOR_FG_HOVER_FACTORY_DECOY = 10108,
    ACTOR_FG_REPAIR_BAY_DECOY = 10109,
    ACTOR_FG_REFINERY_DECOY = 10111,
    ACTOR_FG_POWER_PLANT_DECOY = 10120,
    ACTOR_FG_TRAINING_FACILITY_1_DECOY = 10121,
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
    ACTOR_IMP_ASSAULT_VEHICLE_DECOY = 1109,
    ACTOR_IMP_PLASMA_TANK_DECOY = 1111,
    ACTOR_IMP_TACHYON_TANK_DECOY = 1112,
    ACTOR_IMP_MAD_DECOY = 1113,
    ACTOR_IMP_SHREDDER_DECOY = 1115,
    ACTOR_IMP_SPA_DECOY = 1117,
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
    ACTOR_IMP_SMALL_WALL_1 = 11044,
    ACTOR_IMP_SMALL_WALL_2 = 11045,
    ACTOR_IMP_LARGE_WALL_1 = 11046,
    ACTOR_IMP_LARGE_WALL_2 = 11047,
    /* Civilian and neutral building types (BUILD.TXT SetType values). */
    ACTOR_CIV_ENTERTAINMENT = 13004,
    ACTOR_WATER_EXTRACTOR = 14005,
    ACTOR_TAELON_EXTRACTOR = 14006,
    ACTOR_IMP_WATER_RESEARCH = 50005,
    ACTOR_IMP_HOVER_RESEARCH = 50007,
    ACTOR_IMP_DESICATOR_RESEARCH = 50009,
    ACTOR_IMP_GENETIC_RESEARCH = 50011,
    ACTOR_CIV_SHELTER = 50026,
    ACTOR_CIV_SUB_TRANSIT = 50028,
    ACTOR_CIV_TRANSIT_CENTRE = 50030,
    ACTOR_FG_TREATY_HALL = 50042,
    ACTOR_TOGRAN_LANDING_VESSEL = 50050,
    ACTOR_TOGRAN_MONOLITH = 50052,
    ACTOR_TOGRAN_LABORATORY = 50053,
    ACTOR_RENDEZVOUS_POINT = 50054,
    ACTOR_FG_PLANETARY_DEFENSE = 50055,
    ACTOR_CIV_COMMERCIAL = 50056,
    ACTOR_CIV_FACTORY = 50058,
    ACTOR_IMP_PRISON = 50059,
    ACTOR_CIV_RURAL = 50060,
    ACTOR_CIV_GRAIN_FARM = 50062,
    ACTOR_CIV_HYDRO_FARM = 50064,
    ACTOR_CIV_FARMHOUSE = 50066,
    ACTOR_CIVILIAN_BRIDGE = 50070,
    ACTOR_CIVILIAN_VERTICAL_BRIDGE = 50071,
    /* Togran bridge types (BUILD.TXT SetType values). */
    ACTOR_TOGRAN_SMALL_HORIZONTAL_BRIDGE = 55500,
    ACTOR_TOGRAN_SMALL_VERTICAL_BRIDGE = 55502,
    ACTOR_TOGRAN_SMALL_CENTRE_BRIDGE = 55504,
};


bool DR_HarvestDropoffMatches(const mobj_t *unit,
                              int resource_type, const mobj_t *base,
                              fixed2_t *position);


enum { DR_PAGE_BUILD, DR_PAGE_ORDERS, DR_PAGE_PATHS };
/* HUD items named for the shell and the tests. */
enum { DR_HUD_MOVE = 1, DR_HUD_ATTACK, DR_HUD_WAYPOINT };

/* The production icons of the native menu sprites, by product UI id. */
typedef struct {
    int id;
    const char *image;
} dr_menuproduct_t;
extern const dr_menuproduct_t dr_menu_products[];
extern const int dr_menu_product_count;

/* What the HUD's pages show. */
typedef struct {
    spritesheet_t images[15];
    spritesheet_t *icons; /* one per dr_menu_products entry */
    bitmapfont_t fonts[4];
    int page;
    int production_page;
    uint32_t production_selection;
    pathbook_t paths;
    bool path_advanced;
} dr_hud_t;
extern dr_hud_t drhud;

irect_t DR_MinimapRect(const level_t *map);
bool DR_LoadMenuSprite(const char *root, const char *name, spritesheet_t *out);


#endif
