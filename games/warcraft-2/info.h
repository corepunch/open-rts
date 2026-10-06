#ifndef __INFO__
#define __INFO__

#include "engine.h"

enum {
    W2_SKIP = 1 << 0, W2_MOBILE = 1 << 1, W2_SEA = 1 << 2,
    W2_AIR = 1 << 3, W2_STRUCTURE = 1 << 4, W2_HALL = 1 << 5,
    W2_HARVEST = 1 << 6, W2_COMBAT = 1 << 7, W2_CRITTER = 1 << 8,
};

enum { W2_TARGET_LAND = 1, W2_TARGET_SEA = 2, W2_TARGET_AIR = 4 };
enum { W2_DOMAIN_LAND, W2_DOMAIN_SEA, W2_DOMAIN_AIR };
enum {
    W2_ORGANIC = 1 << 0, W2_UNDEAD = 1 << 1, W2_HERO = 1 << 2,
    W2_VOLATILE = 1 << 3, W2_DETECT_CLOAK = 1 << 4,
    W2_PERMANENT_CLOAK = 1 << 5, W2_INDESTRUCTIBLE = 1 << 6,
    W2_COWARD = 1 << 7, W2_GROUND_ATTACK = 1 << 8,
    W2_RECT_SELECT = 1 << 9, W2_VISIBLE_UNDER_FOG = 1 << 10,
    W2_SHORE_BUILDING = 1 << 11, W2_BUILDER_OUTSIDE = 1 << 12,
    W2_ELEVATED = 1 << 13, W2_SIDE_ATTACK = 1 << 14,
    W2_CAN_ATTACK = 1 << 15,
    W2_NEUTRAL = 1 << 16, W2_TELEPORTER = 1 << 17,
    W2_CAN_HARVEST = 1 << 18,
};

typedef struct { int time; int resources[3]; } w2_cost_t;
typedef struct {
    int capacity, step, resource_wait, depot_wait;
    bool terrain, outside, lose_loaded, refinery;
} w2_gather_t;

/* Native/reference values stay here even when the engine does not yet
 * simulate the rule. Doom's spawnhealth/damage own HP and maximum damage. */
typedef struct {
    uint16_t flags;
    uint32_t attributes;
    isize2_t footprint, box;
    /* MAINDAT forest, winter, wasteland, swamp. Missing later eras reuse forest. */
    uint16_t grp[4];
    int speed, armor, basic_damage, piercing_damage, damage_min;
    int sight, attack_range, min_attack_range;
    struct { int computer, person; } reaction_range;
    w2_cost_t costs;
    struct { int hp, range, auto_range; int costs[3]; } repair;
    struct { int supply, demand; } food;
    struct { int max, initial, increase; } mana;
    int points, priority, annoyance, level, decay, transport_capacity;
    int domain, target_mask, store_mask, gives_mask, income[3];
    w2_gather_t gather[3];
    const char *projectile;
    const char *spells[6];
} w2_stats_t;

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
    const char *name, *label;
    w2_stats_t w2;
} mobjinfo_t;

extern gameinfo_t game_info;
extern state_t states[];
extern mobjinfo_t mobjinfo[];
extern const char *sprnames[];

/* Actor IDs are native PUD types plus one; zero is S_NULL. */
typedef enum {
    MT_NONE = 0,
    MT_FOOTMAN = 1,
    MT_GRUNT = 2,
    MT_PEASANT = 3,
    MT_PEON = 4,
    MT_BALLISTA = 5,
    MT_CATAPULT = 6,
    MT_KNIGHT = 7,
    MT_OGRE = 8,
    MT_ARCHER = 9,
    MT_AXETHROWER = 10,
    MT_MAGE = 11,
    MT_DEATH_KNIGHT = 12,
    MT_PALADIN = 13,
    MT_OGRE_MAGE = 14,
    MT_DEMOLITION_SQUAD = 15,
    MT_GOBLIN_SAPPERS = 16,
    MT_ATTACK_PEASANT = 17,
    MT_ATTACK_PEON = 18,
    MT_RANGER = 19,
    MT_BERSERKER = 20,
    MT_ALLERIA = 21,
    MT_TERON_GOREFIEND = 22,
    MT_KURDRAN = 23,
    MT_DENTARG = 24,
    MT_KHADGAR = 25,
    MT_GROM_HELLSCREAM = 26,
    MT_HUMAN_OIL_TANKER = 27,
    MT_ORC_OIL_TANKER = 28,
    MT_HUMAN_TRANSPORT = 29,
    MT_ORC_TRANSPORT = 30,
    MT_HUMAN_DESTROYER = 31,
    MT_ORC_DESTROYER = 32,
    MT_BATTLESHIP = 33,
    MT_OGRE_JUGGERNAUGHT = 34,
    MT_RESERVED_34 = 35,
    MT_DEATHWING = 36,
    MT_RESERVED_36 = 37,
    MT_RESERVED_37 = 38,
    MT_GNOMISH_SUBMARINE = 39,
    MT_GIANT_TURTLE = 40,
    MT_FLYING_MACHINE = 41,
    MT_ZEPPELIN = 42,
    MT_GRYPHON_RIDER = 43,
    MT_DRAGON = 44,
    MT_TURALYON = 45,
    MT_EYE_OF_KILROGG = 46,
    MT_DANATH = 47,
    MT_KARGATH_BLADEFIST = 48,
    MT_RESERVED_48 = 49,
    MT_CHOGALL = 50,
    MT_LOTHAR = 51,
    MT_GULDAN = 52,
    MT_UTHER_LIGHTBRINGER = 53,
    MT_ZULJIN = 54,
    MT_RESERVED_54 = 55,
    MT_SKELETON = 56,
    MT_DAEMON = 57,
    MT_CRITTER = 58,
    MT_FARM = 59,
    MT_PIG_FARM = 60,
    MT_HUMAN_BARRACKS = 61,
    MT_ORC_BARRACKS = 62,
    MT_CHURCH = 63,
    MT_ALTAR_OF_STORMS = 64,
    MT_HUMAN_WATCH_TOWER = 65,
    MT_ORC_WATCH_TOWER = 66,
    MT_STABLES = 67,
    MT_OGRE_MOUND = 68,
    MT_INVENTOR = 69,
    MT_ALCHEMIST = 70,
    MT_GRYPHON_AVIARY = 71,
    MT_DRAGON_ROOST = 72,
    MT_HUMAN_SHIPYARD = 73,
    MT_ORC_SHIPYARD = 74,
    MT_TOWN_HALL = 75,
    MT_GREAT_HALL = 76,
    MT_ELVEN_LUMBER_MILL = 77,
    MT_TROLL_LUMBER_MILL = 78,
    MT_HUMAN_FOUNDRY = 79,
    MT_ORC_FOUNDRY = 80,
    MT_MAGE_TOWER = 81,
    MT_TEMPLE_OF_THE_DAMNED = 82,
    MT_HUMAN_BLACKSMITH = 83,
    MT_ORC_BLACKSMITH = 84,
    MT_HUMAN_REFINERY = 85,
    MT_ORC_REFINERY = 86,
    MT_HUMAN_OIL_PLATFORM = 87,
    MT_ORC_OIL_PLATFORM = 88,
    MT_KEEP = 89,
    MT_STRONGHOLD = 90,
    MT_CASTLE = 91,
    MT_FORTRESS = 92,
    MT_GOLD_MINE = 93,
    MT_OIL_PATCH = 94,
    MT_HUMAN_START_LOCATION = 95,
    MT_ORC_START_LOCATION = 96,
    MT_HUMAN_GUARD_TOWER = 97,
    MT_ORC_GUARD_TOWER = 98,
    MT_HUMAN_CANNON_TOWER = 99,
    MT_ORC_CANNON_TOWER = 100,
    MT_CIRCLE_OF_POWER = 101,
    MT_DARK_PORTAL = 102,
    MT_RUNESTONE = 103,
    MT_HUMAN_WALL = 104,
    MT_ORC_WALL = 105,
    NUMMOBJTYPES
} mobjtype_id_t;

#define W2_TYPE_COUNT (NUMMOBJTYPES - 1)
#define W2_MOBJ_COUNT NUMMOBJTYPES
#define W2_SPRITE_COUNT (W2_TYPE_COUNT + 4)
#define W2_WORK_STATE(pud) (1 + W2_TYPE_COUNT * 2 + ((pud) - 2) * 8)
#define W2_WAIT_STATE(pud) (W2_WORK_STATE(pud) + 7)
#define W2_CARRY_STATE(variant) (1 + W2_TYPE_COUNT * 2 + 16 + (variant) * 2)
#define W2_STATE_COUNT (1 + W2_TYPE_COUNT * 2 + 16 + 8)

#endif
