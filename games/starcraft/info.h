#ifndef __INFO__
#define __INFO__
#include "engine.h"
/* Named gameplay types use native units.dat/CHK IDs plus one, as mobjinfo does.
 * Other DAT slots remain loadable through the same numeric range. */
typedef enum {
    MT_NONE = 0,
    MT_MARINE = 1,
    MT_GHOST = 2,
    MT_VULTURE = 3,
    MT_GOLIATH = 4,
    MT_SIEGE_TANK = 6,
    MT_SCV = 8,
    MT_WRAITH = 9,
    MT_SCIENCE_VESSEL = 10,
    MT_DROPSHIP = 12,
    MT_BATTLECRUISER = 13,
    MT_NUCLEAR_MISSILE = 15,
    MT_SIEGE_MODE = 31,
    MT_FIREBAT = 33,
    MT_BROODLING = 41,
    MT_LARVA = 36,
    MT_EGG = 37,
    MT_ZERGLING = 38,
    MT_HYDRALISK = 39,
    MT_ULTRALISK = 40,
    MT_DRONE = 42,
    MT_OVERLORD = 43,
    MT_MUTALISK = 44,
    MT_GUARDIAN = 45,
    MT_QUEEN = 46,
    MT_DEFILER = 47,
    MT_SCOURGE = 48,
    MT_COCOON = 60,
    MT_PROBE = 65,
    MT_ZEALOT = 66,
    MT_DRAGOON = 67,
    MT_HIGH_TEMPLAR = 68,
    MT_ARCHON = 69,
    MT_SHUTTLE = 70,
    MT_SCOUT = 71,
    MT_ARBITER = 72,
    MT_CARRIER = 73,
    MT_INTERCEPTOR = 74,
    MT_REAVER = 84,
    MT_OBSERVER = 85,
    MT_SCARAB = 86,
    MT_MAP_REVEALER = 102,
    MT_COMMAND_CENTER = 107,
    MT_COMSAT_STATION = 108,
    MT_NUCLEAR_SILO = 109,
    MT_SUPPLY_DEPOT = 110,
    MT_REFINERY = 111,
    MT_BARRACKS = 112,
    MT_ACADEMY = 113,
    MT_FACTORY = 114,
    MT_STARPORT = 115,
    MT_CONTROL_TOWER = 116,
    MT_SCIENCE_FACILITY = 117,
    MT_COVERT_OPS = 118,
    MT_PHYSICS_LAB = 119,
    MT_MACHINE_SHOP = 121,
    MT_ENGINEERING_BAY = 123,
    MT_ARMORY = 124,
    MT_MISSILE_TURRET = 125,
    MT_BUNKER = 126,
    MT_HATCHERY = 132,
    MT_LAIR = 133,
    MT_HIVE = 134,
    MT_NYDUS_CANAL = 135,
    MT_HYDRALISK_DEN = 136,
    MT_DEFILER_MOUND = 137,
    MT_GREATER_SPIRE = 138,
    MT_QUEENS_NEST = 139,
    MT_EVOLUTION_CHAMBER = 140,
    MT_ULTRALISK_CAVERN = 141,
    MT_SPIRE = 142,
    MT_SPAWNING_POOL = 143,
    MT_CREEP_COLONY = 144,
    MT_SPORE_COLONY = 145,
    MT_SUNKEN_COLONY = 147,
    MT_EXTRACTOR = 150,
    MT_NEXUS = 155,
    MT_ROBOTICS_FACILITY = 156,
    MT_PYLON = 157,
    MT_ASSIMILATOR = 158,
    MT_OBSERVATORY = 160,
    MT_GATEWAY = 161,
    MT_PHOTON_CANNON = 163,
    MT_CITADEL_OF_ADUN = 164,
    MT_CYBERNETICS_CORE = 165,
    MT_TEMPLAR_ARCHIVES = 166,
    MT_FORGE = 167,
    MT_STARGATE = 168,
    MT_FLEET_BEACON = 170,
    MT_ARBITER_TRIBUNAL = 171,
    MT_ROBOTICS_SUPPORT_BAY = 172,
    MT_SHIELD_BATTERY = 173,
    MT_MINERAL_FIELD1 = 177,
    MT_MINERAL_FIELD2 = 178,
    MT_MINERAL_FIELD3 = 179,
    MT_VESPENE_GEYSER = 189,
    MT_DARK_SWARM = 203,
    MT_START_LOCATION = 215,
} mobjtype_id_t;
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
/* Sprites past the unit types are death overlays and remnants (images.dat). */
enum { S_NULL, SC_TYPES = 228, NUMMOBJTYPES = 229, SC_STATES = 16384, SC_EXTRA_SPRITES = 96,
       SC_SPRITES = SC_TYPES + SC_EXTRA_SPRITES };
/* Per type: idle and walk (1 + 2i), attack (1 + 2 SC_TYPES + i); then the
 * siege tank's two transforms, then what the native scripts compile. */
enum { SC_SIEGE_STATE = 1 + SC_TYPES * 3, SC_UNSIEGE_STATE = SC_SIEGE_STATE + 2, SC_SCRIPT_STATES = SC_SIEGE_STATE + 4 };
extern state_t states[SC_STATES];
extern mobjinfo_t mobjinfo[NUMMOBJTYPES];
extern const char *sprnames[SC_SPRITES];
extern gameinfo_t game_info;
#endif
