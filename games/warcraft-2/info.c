#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <string.h>

const char *sprnames[W2_SPRITE_COUNT];
state_t states[W2_STATE_COUNT];
/* Authoritative base roster: native PUD order and MAINDAT GRPs; numeric
 * rules from the pinned Wargus definitions, with documented retail overrides.
 * Actor/state runtime views are derived from these C literals, never Lua. */
#define LAND (W2_MOBILE | W2_COMBAT)
#define HARV (W2_MOBILE | W2_HARVEST | W2_COMBAT)
#define SHIP (W2_MOBILE | W2_SEA | W2_COMBAT)
#define TANK (W2_MOBILE | W2_SEA)
#define FLY (W2_MOBILE | W2_AIR | W2_COMBAT)
#define BALLOON (W2_MOBILE | W2_AIR)
#define BLD W2_STRUCTURE
#define HALL (W2_STRUCTURE | W2_HALL)
#define CRIT (W2_MOBILE | W2_CRITTER)

mobjinfo_t mobjinfo[NUMMOBJTYPES] = {
    [MT_FOOTMAN] = { /* PUD 0: unit-footman */
        .doomednum = 1, .spawnhealth = 60, .speed = 1, .radius = 16, .damage = 9,
        .name = "footman", .label = "Footman",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {31, 31},
            .grp = {45, 0, 0, 0}, .speed = 10, .armor = 2,
            .basic_damage = 6, .piercing_damage = 3, .damage_min = 2,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 60, .resources = {600, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 50, .priority = 60, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_GRUNT] = { /* PUD 1: unit-grunt */
        .doomednum = 2, .spawnhealth = 60, .speed = 1, .radius = 16, .damage = 9,
        .name = "grunt", .label = "Grunt",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {31, 31},
            .grp = {46, 0, 0, 0}, .speed = 10, .armor = 2,
            .basic_damage = 6, .piercing_damage = 3, .damage_min = 2,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 60, .resources = {600, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 50, .priority = 60, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_PEASANT] = { /* PUD 2: unit-peasant */
        .doomednum = 3, .spawnhealth = 30, .speed = 1, .radius = 16, .damage = 5,
        .name = "peasant", .label = "Peasant",
        .w2 = {.projectile = "missile-none", .flags = HARV, .footprint = {1, 1}, .box = {31, 31},
            .grp = {47, 0, 0, 0}, .speed = 10, .armor = 0,
            .basic_damage = 3, .piercing_damage = 2, .damage_min = 1,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 45, .resources = {400, 0, 0}},
            .repair = {.hp = 0, .range = 1, .auto_range = 4, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 30, .priority = 50, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_COWARD | W2_RECT_SELECT | W2_CAN_ATTACK,
            .gather = {
                [0] = {100, 0, 150, 150, false, false, false, false},
                [1] = {100, 2, 24, 150, true, false, false, false},
            },
        },
    },
    [MT_PEON] = { /* PUD 3: unit-peon */
        .doomednum = 4, .spawnhealth = 30, .speed = 1, .radius = 16, .damage = 5,
        .name = "peon", .label = "Peon",
        .w2 = {.projectile = "missile-none", .flags = HARV, .footprint = {1, 1}, .box = {31, 31},
            .grp = {48, 0, 0, 0}, .speed = 10, .armor = 0,
            .basic_damage = 3, .piercing_damage = 2, .damage_min = 1,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 45, .resources = {400, 0, 0}},
            .repair = {.hp = 0, .range = 1, .auto_range = 4, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 30, .priority = 50, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_COWARD | W2_RECT_SELECT | W2_CAN_ATTACK,
            .gather = {
                [0] = {100, 0, 150, 150, false, false, false, false},
                [1] = {100, 2, 24, 150, true, false, false, false},
            },
        },
    },
    [MT_BALLISTA] = { /* PUD 4: unit-ballista */
        .doomednum = 5, .spawnhealth = 110, .speed = 0, .radius = 16, .damage = 80,
        .name = "ballista", .label = "Ballista",
        .w2 = {.projectile = "missile-ballista-bolt", .flags = LAND, .footprint = {1, 1}, .box = {63, 63},
            .grp = {49, 0, 0, 0}, .speed = 5, .armor = 0,
            .basic_damage = 80, .piercing_damage = 0, .damage_min = 25,
            .sight = 9, .attack_range = 8, .min_attack_range = 2,
            .reaction_range = {11, 9},
            .costs = {.time = 250, .resources = {900, 300, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 100, .priority = 70, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 3, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_GROUND_ATTACK | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_CATAPULT] = { /* PUD 5: unit-catapult */
        .doomednum = 6, .spawnhealth = 110, .speed = 0, .radius = 16, .damage = 80,
        .name = "catapult", .label = "Catapult",
        .w2 = {.projectile = "missile-catapult-rock", .flags = LAND, .footprint = {1, 1}, .box = {63, 63},
            .grp = {50, 0, 0, 0}, .speed = 5, .armor = 0,
            .basic_damage = 80, .piercing_damage = 0, .damage_min = 25,
            .sight = 9, .attack_range = 8, .min_attack_range = 2,
            .reaction_range = {11, 9},
            .costs = {.time = 250, .resources = {900, 300, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 100, .priority = 70, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 3, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_GROUND_ATTACK | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_KNIGHT] = { /* PUD 6: unit-knight */
        .doomednum = 7, .spawnhealth = 90, .speed = 1, .radius = 16, .damage = 12,
        .name = "knight", .label = "Knight",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {42, 42},
            .grp = {51, 0, 0, 0}, .speed = 13, .armor = 4,
            .basic_damage = 8, .piercing_damage = 4, .damage_min = 2,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 90, .resources = {800, 100, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 100, .priority = 63, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_OGRE] = { /* PUD 7: unit-ogre */
        .doomednum = 8, .spawnhealth = 90, .speed = 1, .radius = 16, .damage = 12,
        .name = "ogre", .label = "Ogre",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {42, 42},
            .grp = {52, 0, 0, 0}, .speed = 13, .armor = 4,
            .basic_damage = 8, .piercing_damage = 4, .damage_min = 2,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 90, .resources = {800, 100, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 100, .priority = 63, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_ARCHER] = { /* PUD 8: unit-archer */
        .doomednum = 9, .spawnhealth = 40, .speed = 1, .radius = 16, .damage = 9,
        .name = "archer", .label = "Archer",
        .w2 = {.projectile = "missile-arrow", .flags = LAND, .footprint = {1, 1}, .box = {33, 33},
            .grp = {53, 0, 0, 0}, .speed = 10, .armor = 0,
            .basic_damage = 3, .piercing_damage = 6, .damage_min = 3,
            .sight = 5, .attack_range = 4, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 70, .resources = {500, 50, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 60, .priority = 55, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_AXETHROWER] = { /* PUD 9: unit-axethrower */
        .doomednum = 10, .spawnhealth = 40, .speed = 1, .radius = 16, .damage = 9,
        .name = "axethrower", .label = "Troll Axethrower",
        .w2 = {.projectile = "missile-axe", .flags = LAND, .footprint = {1, 1}, .box = {36, 36},
            .grp = {54, 0, 0, 0}, .speed = 10, .armor = 0,
            .basic_damage = 3, .piercing_damage = 6, .damage_min = 3,
            .sight = 5, .attack_range = 4, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 70, .resources = {500, 50, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 60, .priority = 50, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_MAGE] = { /* PUD 10: unit-mage */
        .doomednum = 11, .spawnhealth = 60, .speed = 1, .radius = 16, .damage = 9,
        .name = "mage", .label = "Mage",
        .w2 = {.projectile = "missile-lightning", .flags = LAND, .footprint = {1, 1}, .box = {33, 33},
            .grp = {55, 0, 0, 0}, .speed = 8, .armor = 0,
            .basic_damage = 0, .piercing_damage = 9, .damage_min = 5,
            .sight = 9, .attack_range = 2, .min_attack_range = 0,
            .reaction_range = {11, 9},
            .costs = {.time = 120, .resources = {1200, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .mana = {255, 85, 1},
            .points = 100, .priority = 70, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_COWARD | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {
      "spell-fireball",
      "spell-slow",
      "spell-flame-shield",
      "spell-invisibility",
      "spell-polymorph",
      "spell-blizzard"},
        },
    },
    [MT_DEATH_KNIGHT] = { /* PUD 11: unit-death-knight */
        .doomednum = 12, .spawnhealth = 60, .speed = 1, .radius = 16, .damage = 9,
        .name = "death-knight", .label = "Death Knight",
        .w2 = {.projectile = "missile-touch-of-death", .flags = LAND, .footprint = {1, 1}, .box = {39, 39},
            .grp = {58, 0, 0, 0}, .speed = 8, .armor = 0,
            .basic_damage = 0, .piercing_damage = 9, .damage_min = 5,
            .sight = 9, .attack_range = 3, .min_attack_range = 0,
            .reaction_range = {11, 9},
            .costs = {.time = 120, .resources = {1200, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .mana = {255, 85, 1},
            .points = 100, .priority = 70, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_UNDEAD | W2_COWARD | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {
      "spell-death-coil",
      "spell-haste",
      "spell-raise-dead",
      "spell-whirlwind",
      "spell-unholy-armor",
      "spell-death-and-decay"},
        },
    },
    [MT_PALADIN] = { /* PUD 12: unit-paladin */
        .doomednum = 13, .spawnhealth = 90, .speed = 1, .radius = 16, .damage = 12,
        .name = "paladin", .label = "Paladin",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {42, 42},
            .grp = {51, 0, 0, 0}, .speed = 13, .armor = 4,
            .basic_damage = 8, .piercing_damage = 4, .damage_min = 2,
            .sight = 5, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 90, .resources = {800, 100, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .mana = {255, 85, 1},
            .points = 110, .priority = 65, .annoyance = 0, .level = 2,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {"spell-holy-vision", "spell-healing", "spell-exorcism"},
        },
    },
    [MT_OGRE_MAGE] = { /* PUD 13: unit-ogre-mage */
        .doomednum = 14, .spawnhealth = 90, .speed = 1, .radius = 16, .damage = 12,
        .name = "ogre-mage", .label = "Ogre Mage",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {42, 42},
            .grp = {52, 0, 0, 0}, .speed = 13, .armor = 4,
            .basic_damage = 8, .piercing_damage = 4, .damage_min = 2,
            .sight = 5, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 90, .resources = {800, 100, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .mana = {255, 85, 1},
            .points = 110, .priority = 65, .annoyance = 0, .level = 2,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {"spell-eye-of-vision", "spell-runes", "spell-bloodlust"},
        },
    },
    [MT_DEMOLITION_SQUAD] = { /* PUD 14: unit-dwarves */
        .doomednum = 15, .spawnhealth = 40, .speed = 1, .radius = 16, .damage = 6,
        .name = "dwarves", .label = "Demolition Squad",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {32, 32},
            .grp = {33, 0, 0, 0}, .speed = 11, .armor = 0,
            .basic_damage = 4, .piercing_damage = 2, .damage_min = 1,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {4, 2},
            .costs = {.time = 200, .resources = {700, 250, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 100, .priority = 55, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_VOLATILE | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {"spell-suicide-bomber"},
        },
    },
    [MT_GOBLIN_SAPPERS] = { /* PUD 15: unit-goblin-sappers */
        .doomednum = 16, .spawnhealth = 40, .speed = 1, .radius = 16, .damage = 6,
        .name = "goblin-sappers", .label = "Goblin Sappers",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {37, 37},
            .grp = {34, 0, 0, 0}, .speed = 11, .armor = 0,
            .basic_damage = 4, .piercing_damage = 2, .damage_min = 1,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {4, 2},
            .costs = {.time = 200, .resources = {700, 250, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 100, .priority = 55, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_VOLATILE | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {"spell-suicide-bomber"},
        },
    },
    [MT_ATTACK_PEASANT] = { /* PUD 16: unit-attack-peasant */
        .doomednum = 17, .spawnhealth = 30, .speed = 1, .radius = 16, .damage = 5,
        .name = "attack-peasant", .label = "Peasant",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {31, 31},
            .grp = {47, 0, 0, 0}, .speed = 10, .armor = 0,
            .basic_damage = 3, .piercing_damage = 2, .damage_min = 1,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 0, .resources = {0, 0, 0}},
            .repair = {.hp = 0, .range = 1, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 30, .priority = 50, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_ATTACK_PEON] = { /* PUD 17: unit-attack-peon */
        .doomednum = 18, .spawnhealth = 30, .speed = 1, .radius = 16, .damage = 5,
        .name = "attack-peon", .label = "Peon",
        .w2 = {.projectile = "missile-none", .flags = HARV, .footprint = {1, 1}, .box = {31, 31},
            .grp = {48, 0, 0, 0}, .speed = 10, .armor = 0,
            .basic_damage = 3, .piercing_damage = 2, .damage_min = 1,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 45, .resources = {400, 0, 0}},
            .repair = {.hp = 0, .range = 1, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 30, .priority = 50, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
            .gather = {
                [0] = {100, 0, 150, 150, false, false, false, false},
                [1] = {100, 2, 24, 150, true, false, false, false},
            },
        },
    },
    [MT_RANGER] = { /* PUD 18: unit-ranger */
        .doomednum = 19, .spawnhealth = 50, .speed = 1, .radius = 16, .damage = 9,
        .name = "ranger", .label = "Ranger",
        .w2 = {.projectile = "missile-arrow", .flags = LAND, .footprint = {1, 1}, .box = {33, 33},
            .grp = {53, 0, 0, 0}, .speed = 10, .armor = 0,
            .basic_damage = 3, .piercing_damage = 6, .damage_min = 3,
            .sight = 6, .attack_range = 4, .min_attack_range = 0,
            .reaction_range = {9, 6},
            .costs = {.time = 70, .resources = {500, 50, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 70, .priority = 57, .annoyance = 0, .level = 2,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_BERSERKER] = { /* PUD 19: unit-berserker */
        .doomednum = 20, .spawnhealth = 50, .speed = 1, .radius = 16, .damage = 9,
        .name = "berserker", .label = "Berserker",
        .w2 = {.projectile = "missile-axe", .flags = LAND, .footprint = {1, 1}, .box = {36, 36},
            .grp = {54, 0, 0, 0}, .speed = 10, .armor = 0,
            .basic_damage = 3, .piercing_damage = 6, .damage_min = 3,
            .sight = 6, .attack_range = 4, .min_attack_range = 0,
            .reaction_range = {9, 6},
            .costs = {.time = 70, .resources = {500, 50, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 70, .priority = 57, .annoyance = 0, .level = 2,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_ALLERIA] = { /* PUD 20: unit-female-hero */
        .doomednum = 21, .spawnhealth = 120, .speed = 1, .radius = 16, .damage = 28,
        .name = "female-hero", .label = "Alleria",
        .w2 = {.projectile = "missile-arrow", .flags = LAND, .footprint = {1, 1}, .box = {33, 33},
            .grp = {53, 0, 0, 0}, .speed = 10, .armor = 5,
            .basic_damage = 10, .piercing_damage = 18, .damage_min = 9,
            .sight = 9, .attack_range = 7, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 70, .resources = {500, 50, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 60, .priority = 55, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_TERON_GOREFIEND] = { /* PUD 21: unit-evil-knight */
        .doomednum = 22, .spawnhealth = 180, .speed = 1, .radius = 16, .damage = 16,
        .name = "evil-knight", .label = "Teron Gorefiend",
        .w2 = {.projectile = "missile-touch-of-death", .flags = LAND, .footprint = {1, 1}, .box = {39, 39},
            .grp = {58, 0, 0, 0}, .speed = 8, .armor = 2,
            .basic_damage = 0, .piercing_damage = 16, .damage_min = 8,
            .sight = 9, .attack_range = 4, .min_attack_range = 0,
            .reaction_range = {11, 9},
            .costs = {.time = 120, .resources = {1200, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .mana = {255, 85, 1},
            .points = 100, .priority = 70, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_UNDEAD | W2_COWARD | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {
      "spell-death-coil",
      "spell-haste",
      "spell-raise-dead",
      "spell-whirlwind",
      "spell-unholy-armor",
      "spell-death-and-decay"},
        },
    },
    [MT_KURDRAN] = { /* PUD 22: unit-flying-angel */
        .doomednum = 23, .spawnhealth = 250, .speed = 1, .radius = 16, .damage = 25,
        .name = "flying-angel", .label = "Kurdran and Sky'ree",
        .w2 = {.projectile = "missile-griffon-hammer", .flags = FLY, .footprint = {2, 2}, .box = {63, 63},
            .grp = {35, 0, 0, 0}, .speed = 14, .armor = 6,
            .basic_damage = 0, .piercing_damage = 25, .damage_min = 13,
            .sight = 9, .attack_range = 5, .min_attack_range = 0,
            .reaction_range = {8, 6},
            .costs = {.time = 250, .resources = {2500, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 150, .priority = 65, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_AIR, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_DETECT_CLOAK | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_DENTARG] = { /* PUD 23: unit-fad-man */
        .doomednum = 24, .spawnhealth = 300, .speed = 1, .radius = 16, .damage = 24,
        .name = "fad-man", .label = "Dentarg",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {42, 42},
            .grp = {52, 0, 0, 0}, .speed = 13, .armor = 8,
            .basic_damage = 18, .piercing_damage = 6, .damage_min = 3,
            .sight = 6, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 90, .resources = {800, 100, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .mana = {255, 85, 1},
            .points = 100, .priority = 63, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {"spell-eye-of-vision", "spell-runes", "spell-bloodlust"},
        },
    },
    [MT_KHADGAR] = { /* PUD 24: unit-white-mage */
        .doomednum = 25, .spawnhealth = 120, .speed = 1, .radius = 16, .damage = 16,
        .name = "white-mage", .label = "Khadgar",
        .w2 = {.projectile = "missile-lightning", .flags = LAND, .footprint = {1, 1}, .box = {33, 33},
            .grp = {55, 0, 0, 0}, .speed = 8, .armor = 3,
            .basic_damage = 0, .piercing_damage = 16, .damage_min = 8,
            .sight = 9, .attack_range = 6, .min_attack_range = 0,
            .reaction_range = {11, 9},
            .costs = {.time = 120, .resources = {1200, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .mana = {255, 85, 1},
            .points = 100, .priority = 70, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_COWARD | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {
      "spell-fireball",
      "spell-slow",
      "spell-flame-shield",
      "spell-invisibility",
      "spell-polymorph",
      "spell-blizzard"},
        },
    },
    [MT_GROM_HELLSCREAM] = { /* PUD 25: unit-beast-cry */
        .doomednum = 26, .spawnhealth = 240, .speed = 1, .radius = 16, .damage = 22,
        .name = "beast-cry", .label = "Grom Hellscream",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {31, 31},
            .grp = {54, 0, 0, 0}, .speed = 10, .armor = 8,
            .basic_damage = 16, .piercing_damage = 6, .damage_min = 3,
            .sight = 5, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 60, .resources = {600, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 50, .priority = 60, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_HUMAN_OIL_TANKER] = { /* PUD 26: unit-human-oil-tanker */
        .doomednum = 27, .spawnhealth = 90, .speed = 1, .radius = 16, .damage = 0,
        .name = "human-tanker", .label = "Oil Tanker",
        .w2 = {.projectile = "missile-none", .flags = TANK | W2_HARVEST, .footprint = {2, 2}, .box = {63, 63},
            .grp = {59, 0, 0, 0}, .speed = 10, .armor = 10,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 50, .resources = {400, 200, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 40, .priority = 50, .annoyance = 10, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 0,
            .attributes = W2_COWARD | W2_RECT_SELECT,
            .gather = {
                [2] = {100, 0, 100, 100, false, false, false, true},
            },
        },
    },
    [MT_ORC_OIL_TANKER] = { /* PUD 27: unit-orc-oil-tanker */
        .doomednum = 28, .spawnhealth = 90, .speed = 1, .radius = 16, .damage = 0,
        .name = "orc-tanker", .label = "Oil Tanker",
        .w2 = {.projectile = "missile-none", .flags = TANK | W2_HARVEST, .footprint = {2, 2}, .box = {63, 63},
            .grp = {60, 0, 0, 0}, .speed = 10, .armor = 10,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 50, .resources = {400, 200, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 40, .priority = 50, .annoyance = 10, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 0,
            .attributes = W2_COWARD | W2_RECT_SELECT,
            .gather = {
                [2] = {100, 0, 100, 100, false, false, false, true},
            },
        },
    },
    [MT_HUMAN_TRANSPORT] = { /* PUD 28: unit-human-transport */
        .doomednum = 29, .spawnhealth = 150, .speed = 1, .radius = 16, .damage = 0,
        .name = "human-transport", .label = "Transport",
        .w2 = {.projectile = "missile-none", .flags = TANK, .footprint = {2, 2}, .box = {63, 63},
            .grp = {39, 0, 0, 0}, .speed = 10, .armor = 0,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 70, .resources = {600, 200, 500}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {0, 1},
            .points = 50, .priority = 70, .annoyance = 15, .level = 1,
            .decay = 0, .transport_capacity = 6, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 0,
            .attributes = W2_RECT_SELECT,
        },
    },
    [MT_ORC_TRANSPORT] = { /* PUD 29: unit-orc-transport */
        .doomednum = 30, .spawnhealth = 150, .speed = 1, .radius = 16, .damage = 0,
        .name = "orc-transport", .label = "Transport",
        .w2 = {.projectile = "missile-none", .flags = TANK, .footprint = {2, 2}, .box = {63, 63},
            .grp = {40, 0, 0, 0}, .speed = 10, .armor = 0,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 4, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 70, .resources = {600, 200, 500}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {0, 1},
            .points = 50, .priority = 70, .annoyance = 15, .level = 1,
            .decay = 0, .transport_capacity = 6, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 0,
            .attributes = W2_RECT_SELECT,
        },
    },
    [MT_HUMAN_DESTROYER] = { /* PUD 30: unit-human-destroyer */
        .doomednum = 31, .spawnhealth = 100, .speed = 1, .radius = 16, .damage = 35,
        .name = "human-destroyer", .label = "Elven Destroyer",
        .w2 = {.projectile = "missile-small-cannon", .flags = SHIP, .footprint = {2, 2}, .box = {63, 63},
            .grp = {61, 0, 0, 0}, .speed = 10, .armor = 10,
            .basic_damage = 35, .piercing_damage = 0, .damage_min = 2,
            .sight = 8, .attack_range = 4, .min_attack_range = 0,
            .reaction_range = {10, 8},
            .costs = {.time = 90, .resources = {700, 350, 700}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 150, .priority = 65, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 0,
            .attributes = W2_RECT_SELECT | W2_SIDE_ATTACK | W2_CAN_ATTACK,
        },
    },
    [MT_ORC_DESTROYER] = { /* PUD 31: unit-orc-destroyer */
        .doomednum = 32, .spawnhealth = 100, .speed = 1, .radius = 16, .damage = 35,
        .name = "orc-destroyer", .label = "Troll Destroyer",
        .w2 = {.projectile = "missile-small-cannon", .flags = SHIP, .footprint = {2, 2}, .box = {63, 63},
            .grp = {62, 0, 0, 0}, .speed = 10, .armor = 10,
            .basic_damage = 35, .piercing_damage = 0, .damage_min = 2,
            .sight = 8, .attack_range = 4, .min_attack_range = 0,
            .reaction_range = {10, 8},
            .costs = {.time = 90, .resources = {700, 350, 700}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 150, .priority = 65, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 0,
            .attributes = W2_RECT_SELECT | W2_SIDE_ATTACK | W2_CAN_ATTACK,
        },
    },
    [MT_BATTLESHIP] = { /* PUD 32: unit-battleship */
        .doomednum = 33, .spawnhealth = 150, .speed = 0, .radius = 16, .damage = 130,
        .name = "battleship", .label = "Battleship",
        .w2 = {.projectile = "missile-big-cannon", .flags = SHIP, .footprint = {2, 2}, .box = {70, 70},
            .grp = {41, 0, 0, 0}, .speed = 6, .armor = 15,
            .basic_damage = 130, .piercing_damage = 0, .damage_min = 50,
            .sight = 8, .attack_range = 6, .min_attack_range = 0,
            .reaction_range = {10, 8},
            .costs = {.time = 140, .resources = {1000, 500, 1000}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 300, .priority = 63, .annoyance = 25, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 3, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 0,
            .attributes = W2_GROUND_ATTACK | W2_RECT_SELECT | W2_SIDE_ATTACK | W2_CAN_ATTACK,
        },
    },
    [MT_OGRE_JUGGERNAUGHT] = { /* PUD 33: unit-ogre-juggernaught */
        .doomednum = 34, .spawnhealth = 150, .speed = 0, .radius = 16, .damage = 130,
        .name = "juggernaught", .label = "Ogre Juggernaught",
        .w2 = {.projectile = "missile-big-cannon", .flags = SHIP, .footprint = {2, 2}, .box = {70, 70},
            .grp = {42, 0, 0, 0}, .speed = 6, .armor = 15,
            .basic_damage = 130, .piercing_damage = 0, .damage_min = 50,
            .sight = 8, .attack_range = 6, .min_attack_range = 0,
            .reaction_range = {10, 8},
            .costs = {.time = 140, .resources = {1000, 500, 1000}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 300, .priority = 63, .annoyance = 25, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 3, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 0,
            .attributes = W2_GROUND_ATTACK | W2_RECT_SELECT | W2_SIDE_ATTACK | W2_CAN_ATTACK,
        },
    },
    [MT_RESERVED_34] = {.doomednum = 35, .w2.flags = W2_SKIP},
    [MT_DEATHWING] = { /* PUD 35: unit-fire-breeze */
        .doomednum = 36, .spawnhealth = 800, .speed = 1, .radius = 16, .damage = 35,
        .name = "fire-breeze", .label = "Deathwing",
        .w2 = {.projectile = "missile-dragon-breath", .flags = FLY, .footprint = {2, 2}, .box = {71, 71},
            .grp = {0, 0, 0, 0}, .speed = 14, .armor = 10,
            .basic_damage = 10, .piercing_damage = 25, .damage_min = 13,
            .sight = 9, .attack_range = 5, .min_attack_range = 0,
            .reaction_range = {8, 6},
            .costs = {.time = 250, .resources = {2500, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 150, .priority = 65, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_AIR, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_DETECT_CLOAK | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_RESERVED_36] = {.doomednum = 37, .w2.flags = W2_SKIP},
    [MT_RESERVED_37] = {.doomednum = 38, .w2.flags = W2_SKIP},
    [MT_GNOMISH_SUBMARINE] = { /* PUD 38: unit-human-submarine */
        .doomednum = 39, .spawnhealth = 60, .speed = 0, .radius = 16, .damage = 50,
        .name = "gnome-submarine", .label = "Gnomish Submarine",
        .w2 = {.projectile = "missile-submarine-missile", .flags = SHIP, .footprint = {2, 2}, .box = {63, 63},
            .grp = {43, 0, 182, 526}, .speed = 7, .armor = 0,
            .basic_damage = 50, .piercing_damage = 0, .damage_min = 10,
            .sight = 5, .attack_range = 4, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 100, .resources = {800, 150, 900}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 120, .priority = 60, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 2, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 0,
            .attributes = W2_DETECT_CLOAK | W2_PERMANENT_CLOAK | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_GIANT_TURTLE] = { /* PUD 39: unit-orc-submarine */
        .doomednum = 40, .spawnhealth = 60, .speed = 0, .radius = 16, .damage = 50,
        .name = "giant-turtle", .label = "Giant Turtle",
        .w2 = {.projectile = "missile-turtle-missile", .flags = SHIP, .footprint = {2, 2}, .box = {63, 63},
            .grp = {44, 0, 183, 527}, .speed = 7, .armor = 0,
            .basic_damage = 50, .piercing_damage = 0, .damage_min = 10,
            .sight = 5, .attack_range = 4, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 100, .resources = {800, 150, 900}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 120, .priority = 60, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 2, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 0,
            .attributes = W2_DETECT_CLOAK | W2_PERMANENT_CLOAK | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_FLYING_MACHINE] = { /* PUD 40: unit-balloon */
        .doomednum = 41, .spawnhealth = 150, .speed = 2, .radius = 16, .damage = 0,
        .name = "balloon", .label = "Gnomish Flying Machine",
        .w2 = {.projectile = "missile-none", .flags = BALLOON, .footprint = {2, 2}, .box = {63, 63},
            .grp = {38, 0, 0, 0}, .speed = 17, .armor = 2,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 9, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {19, 15},
            .costs = {.time = 65, .resources = {500, 100, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 40, .priority = 40, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_AIR, .gives_mask = 0,
            .attributes = W2_DETECT_CLOAK | W2_COWARD | W2_RECT_SELECT,
        },
    },
    [MT_ZEPPELIN] = { /* PUD 41: unit-zeppelin */
        .doomednum = 42, .spawnhealth = 150, .speed = 2, .radius = 16, .damage = 0,
        .name = "zeppelin", .label = "Goblin Zeppelin",
        .w2 = {.projectile = "missile-none", .flags = BALLOON, .footprint = {2, 2}, .box = {63, 63},
            .grp = {63, 0, 0, 0}, .speed = 17, .armor = 2,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 9, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {19, 15},
            .costs = {.time = 65, .resources = {500, 100, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 40, .priority = 40, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_AIR, .gives_mask = 0,
            .attributes = W2_DETECT_CLOAK | W2_COWARD | W2_RECT_SELECT,
        },
    },
    [MT_GRYPHON_RIDER] = { /* PUD 42: unit-gryphon-rider */
        .doomednum = 43, .spawnhealth = 100, .speed = 1, .radius = 16, .damage = 16,
        .name = "gryphon-rider", .label = "Gryphon Rider",
        .w2 = {.projectile = "missile-griffon-hammer", .flags = FLY, .footprint = {2, 2}, .box = {63, 63},
            .grp = {35, 0, 0, 0}, .speed = 14, .armor = 5,
            .basic_damage = 0, .piercing_damage = 16, .damage_min = 8,
            .sight = 6, .attack_range = 4, .min_attack_range = 0,
            .reaction_range = {8, 6},
            .costs = {.time = 250, .resources = {2500, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 150, .priority = 65, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_AIR, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_DETECT_CLOAK | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_DRAGON] = { /* PUD 43: unit-dragon */
        .doomednum = 44, .spawnhealth = 100, .speed = 1, .radius = 16, .damage = 16,
        .name = "dragon", .label = "Dragon",
        .w2 = {.projectile = "missile-dragon-breath", .flags = FLY, .footprint = {2, 2}, .box = {71, 71},
            .grp = {36, 0, 0, 0}, .speed = 14, .armor = 5,
            .basic_damage = 0, .piercing_damage = 16, .damage_min = 8,
            .sight = 6, .attack_range = 4, .min_attack_range = 0,
            .reaction_range = {8, 6},
            .costs = {.time = 250, .resources = {2500, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 150, .priority = 65, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_AIR, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_DETECT_CLOAK | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_TURALYON] = { /* PUD 44: unit-knight-rider */
        .doomednum = 45, .spawnhealth = 180, .speed = 1, .radius = 16, .damage = 19,
        .name = "knight-rider", .label = "Turalyon",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {42, 42},
            .grp = {51, 0, 0, 0}, .speed = 13, .armor = 10,
            .basic_damage = 14, .piercing_damage = 5, .damage_min = 3,
            .sight = 6, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 90, .resources = {800, 100, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .mana = {255, 85, 1},
            .points = 110, .priority = 65, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {"spell-holy-vision", "spell-healing", "spell-exorcism"},
        },
    },
    [MT_EYE_OF_KILROGG] = { /* PUD 45: unit-eye-of-vision */
        .doomednum = 46, .spawnhealth = 100, .speed = 5, .radius = 16, .damage = 1,
        .name = "eye-of-kilrogg", .label = "Eye of Kilrogg",
        .w2 = {.projectile = "missile-none", .flags = FLY, .footprint = {1, 1}, .box = {31, 31},
            .grp = {37, 0, 0, 0}, .speed = 42, .armor = 0,
            .basic_damage = 1, .piercing_damage = 0, .damage_min = 1,
            .sight = 3, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {20, 10},
            .costs = {.time = 0, .resources = {0, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 0},
            .points = 0, .priority = 0, .annoyance = 0, .level = 1,
            .decay = 3, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_AIR, .gives_mask = 0,
            .attributes = W2_DETECT_CLOAK | W2_RECT_SELECT,
        },
    },
    [MT_DANATH] = { /* PUD 46: unit-arthor-literios */
        .doomednum = 47, .spawnhealth = 220, .speed = 1, .radius = 16, .damage = 23,
        .name = "arthor-literios", .label = "Danath",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {31, 31},
            .grp = {45, 0, 0, 0}, .speed = 10, .armor = 8,
            .basic_damage = 15, .piercing_damage = 8, .damage_min = 4,
            .sight = 6, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 60, .resources = {600, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 50, .priority = 60, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_KARGATH_BLADEFIST] = { /* PUD 47: unit-quick-blade */
        .doomednum = 48, .spawnhealth = 240, .speed = 1, .radius = 16, .damage = 22,
        .name = "quick-blade", .label = "Korgath Bladefist",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {31, 31},
            .grp = {46, 0, 0, 0}, .speed = 10, .armor = 8,
            .basic_damage = 16, .piercing_damage = 6, .damage_min = 3,
            .sight = 5, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {6, 4},
            .costs = {.time = 60, .resources = {600, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 50, .priority = 60, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_RESERVED_48] = {.doomednum = 49, .w2.flags = W2_SKIP},
    [MT_CHOGALL] = { /* PUD 49: unit-double-head */
        .doomednum = 50, .spawnhealth = 100, .speed = 1, .radius = 16, .damage = 15,
        .name = "double-head", .label = "Cho'gall",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {42, 42},
            .grp = {52, 0, 0, 0}, .speed = 13, .armor = 0,
            .basic_damage = 10, .piercing_damage = 5, .damage_min = 3,
            .sight = 5, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 100, .resources = {1100, 50, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .mana = {255, 85, 1},
            .points = 120, .priority = 65, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_HERO | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {"spell-eye-of-vision-double-head", "spell-runes-double-head", "spell-bloodlust-double-head"},
        },
    },
    [MT_LOTHAR] = { /* PUD 50: unit-wise-man */
        .doomednum = 51, .spawnhealth = 90, .speed = 1, .radius = 16, .damage = 12,
        .name = "wise-man", .label = "Lothar",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {42, 42},
            .grp = {51, 0, 0, 0}, .speed = 13, .armor = 4,
            .basic_damage = 8, .piercing_damage = 4, .damage_min = 2,
            .sight = 5, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 100, .resources = {900, 100, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 120, .priority = 65, .annoyance = 70, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_HERO | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_GULDAN] = { /* PUD 51: unit-ice-bringer */
        .doomednum = 52, .spawnhealth = 40, .speed = 1, .radius = 16, .damage = 3,
        .name = "ice-bringer", .label = "Gul'dan",
        .w2 = {.projectile = "missile-touch-of-death", .flags = LAND, .footprint = {1, 1}, .box = {33, 33},
            .grp = {58, 0, 0, 0}, .speed = 8, .armor = 0,
            .basic_damage = 0, .piercing_damage = 3, .damage_min = 2,
            .sight = 8, .attack_range = 3, .min_attack_range = 0,
            .reaction_range = {10, 8},
            .costs = {.time = 120, .resources = {1200, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .mana = {255, 85, 1},
            .points = 120, .priority = 70, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_UNDEAD | W2_HERO | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {
      "spell-death-coil",
      "spell-haste",
      "spell-raise-dead",
      "spell-whirlwind",
      "spell-unholy-armor",
      "spell-death-and-decay"},
        },
    },
    [MT_UTHER_LIGHTBRINGER] = { /* PUD 52: unit-man-of-light */
        .doomednum = 53, .spawnhealth = 90, .speed = 1, .radius = 16, .damage = 12,
        .name = "man-of-light", .label = "Uther Lightbringer",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {42, 42},
            .grp = {51, 0, 0, 0}, .speed = 13, .armor = 4,
            .basic_damage = 8, .piercing_damage = 4, .damage_min = 2,
            .sight = 5, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 100, .resources = {900, 100, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .mana = {255, 85, 1},
            .points = 120, .priority = 65, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_HERO | W2_RECT_SELECT | W2_CAN_ATTACK,
            .spells = {"spell-holy-vision", "spell-healing", "spell-exorcism"},
        },
    },
    [MT_ZULJIN] = { /* PUD 53: unit-sharp-axe */
        .doomednum = 54, .spawnhealth = 120, .speed = 1, .radius = 16, .damage = 28,
        .name = "sharp-axe", .label = "Zuljin",
        .w2 = {.projectile = "missile-axe", .flags = LAND, .footprint = {1, 1}, .box = {36, 36},
            .grp = {54, 0, 0, 0}, .speed = 10, .armor = 5,
            .basic_damage = 10, .piercing_damage = 18, .damage_min = 9,
            .sight = 9, .attack_range = 5, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 70, .resources = {500, 50, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 120, .priority = 55, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_HERO | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_RESERVED_54] = {.doomednum = 55, .w2.flags = W2_SKIP},
    [MT_SKELETON] = { /* PUD 55: unit-skeleton */
        .doomednum = 56, .spawnhealth = 40, .speed = 1, .radius = 16, .damage = 9,
        .name = "skeleton", .label = "Skeleton",
        .w2 = {.projectile = "missile-none", .flags = LAND, .footprint = {1, 1}, .box = {31, 31},
            .grp = {69, 0, 0, 0}, .speed = 8, .armor = 0,
            .basic_damage = 6, .piercing_damage = 3, .damage_min = 2,
            .sight = 3, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {4, 2},
            .costs = {.time = 0, .resources = {0, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 0, .priority = 55, .annoyance = 0, .level = 1,
            .decay = 100, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_UNDEAD | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_DAEMON] = { /* PUD 56: unit-daemon */
        .doomednum = 57, .spawnhealth = 60, .speed = 1, .radius = 16, .damage = 12,
        .name = "daemon", .label = "Daemon",
        .w2 = {.projectile = "missile-daemon-fire", .flags = FLY, .footprint = {1, 1}, .box = {31, 31},
            .grp = {70, 0, 0, 0}, .speed = 14, .armor = 3,
            .basic_damage = 10, .piercing_damage = 2, .damage_min = 1,
            .sight = 5, .attack_range = 3, .min_attack_range = 0,
            .reaction_range = {7, 5},
            .costs = {.time = 70, .resources = {500, 0, 50}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 1},
            .points = 100, .priority = 63, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_AIR, .gives_mask = 0,
            .attributes = W2_ORGANIC | W2_DETECT_CLOAK | W2_RECT_SELECT | W2_CAN_ATTACK,
        },
    },
    [MT_CRITTER] = { /* PUD 57: unit-critter */
        .doomednum = 58, .spawnhealth = 5, .speed = 0, .radius = 16, .damage = 0,
        .name = "critter", .label = "Critter",
        .w2 = {.projectile = "missile-critter-explosion", .flags = CRIT, .footprint = {1, 1}, .box = {31, 31},
            .grp = {64, 66, 65, 65}, .speed = 3, .armor = 0,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 2, .attack_range = 1, .min_attack_range = 0,
            .reaction_range = {20, 10},
            .costs = {.time = 0, .resources = {0, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 0},
            .points = 1, .priority = 37, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 1, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_ORGANIC,
        },
    },
    [MT_FARM] = { /* PUD 58: unit-farm */
        .doomednum = 59, .spawnhealth = 400, .speed = 0, .radius = 16, .damage = 0,
        .name = "farm", .label = "Farm",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {2, 2}, .box = {63, 63},
            .grp = {92, 134, 173, 479}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 100, .resources = {500, 250, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {4, 0},
            .points = 100, .priority = 20, .annoyance = 45, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_PIG_FARM] = { /* PUD 59: unit-pig-farm */
        .doomednum = 60, .spawnhealth = 400, .speed = 0, .radius = 16, .damage = 0,
        .name = "pig-farm", .label = "Pig Farm",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {2, 2}, .box = {63, 63},
            .grp = {93, 135, 174, 480}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 2, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 100, .resources = {500, 250, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {4, 0},
            .points = 100, .priority = 20, .annoyance = 45, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_HUMAN_BARRACKS] = { /* PUD 60: unit-human-barracks */
        .doomednum = 61, .spawnhealth = 800, .speed = 0, .radius = 16, .damage = 0,
        .name = "human-barracks", .label = "Barracks",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {94, 136, 94, 481}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {700, 450, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 160, .priority = 30, .annoyance = 35, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_RECT_SELECT | W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_ORC_BARRACKS] = { /* PUD 61: unit-orc-barracks */
        .doomednum = 62, .spawnhealth = 800, .speed = 0, .radius = 16, .damage = 0,
        .name = "orc-barracks", .label = "Barracks",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {95, 137, 95, 482}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {700, 450, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 160, .priority = 30, .annoyance = 35, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_RECT_SELECT | W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_CHURCH] = { /* PUD 62: unit-church */
        .doomednum = 63, .spawnhealth = 700, .speed = 0, .radius = 16, .damage = 0,
        .name = "church", .label = "Church",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {96, 138, 96, 483}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 175, .resources = {900, 500, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 240, .priority = 15, .annoyance = 35, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_ALTAR_OF_STORMS] = { /* PUD 63: unit-altar-of-storms */
        .doomednum = 64, .spawnhealth = 700, .speed = 0, .radius = 16, .damage = 0,
        .name = "altar-of-storms", .label = "Altar of Storms",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {97, 139, 97, 484}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 175, .resources = {900, 500, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 240, .priority = 15, .annoyance = 35, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_HUMAN_WATCH_TOWER] = { /* PUD 64: unit-human-watch-tower */
        .doomednum = 65, .spawnhealth = 100, .speed = 0, .radius = 16, .damage = 0,
        .name = "human-watch-tower", .label = "Scout Tower",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {2, 2}, .box = {63, 63},
            .grp = {98, 140, 98, 485}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 9, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 60, .resources = {550, 200, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 95, .priority = 55, .annoyance = 50, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_DETECT_CLOAK | W2_VISIBLE_UNDER_FOG | W2_ELEVATED,
        },
    },
    [MT_ORC_WATCH_TOWER] = { /* PUD 65: unit-orc-watch-tower */
        .doomednum = 66, .spawnhealth = 100, .speed = 0, .radius = 16, .damage = 0,
        .name = "orc-watch-tower", .label = "Watch Tower",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {2, 2}, .box = {63, 63},
            .grp = {99, 141, 99, 486}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 9, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 60, .resources = {550, 200, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 95, .priority = 55, .annoyance = 50, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_DETECT_CLOAK | W2_VISIBLE_UNDER_FOG | W2_ELEVATED,
        },
    },
    [MT_STABLES] = { /* PUD 66: unit-stables */
        .doomednum = 67, .spawnhealth = 500, .speed = 0, .radius = 16, .damage = 0,
        .name = "stables", .label = "Stables",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {104, 146, 104, 491}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 150, .resources = {1000, 300, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 210, .priority = 15, .annoyance = 15, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_OGRE_MOUND] = { /* PUD 67: unit-ogre-mound */
        .doomednum = 68, .spawnhealth = 500, .speed = 0, .radius = 16, .damage = 0,
        .name = "ogre-mound", .label = "Ogre Mound",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {105, 147, 105, 492}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 150, .resources = {1000, 300, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 210, .priority = 15, .annoyance = 15, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_INVENTOR] = { /* PUD 68: unit-inventor */
        .doomednum = 69, .spawnhealth = 500, .speed = 0, .radius = 16, .damage = 0,
        .name = "inventor", .label = "Gnomish Inventor",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {90, 132, 90, 477}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 150, .resources = {1000, 400, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 230, .priority = 15, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_ALCHEMIST] = { /* PUD 69: unit-alchemist */
        .doomednum = 70, .spawnhealth = 500, .speed = 0, .radius = 16, .damage = 0,
        .name = "alchemist", .label = "Goblin Alchemist",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {91, 133, 91, 478}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 150, .resources = {1000, 400, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 230, .priority = 15, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_GRYPHON_AVIARY] = { /* PUD 70: unit-gryphon-aviary */
        .doomednum = 71, .spawnhealth = 500, .speed = 0, .radius = 16, .damage = 0,
        .name = "gryphon-aviary", .label = "Gryphon Aviary",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {88, 130, 88, 475}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 150, .resources = {1000, 400, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 280, .priority = 15, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_DRAGON_ROOST] = { /* PUD 71: unit-dragon-roost */
        .doomednum = 72, .spawnhealth = 500, .speed = 0, .radius = 16, .damage = 0,
        .name = "dragon-roost", .label = "Dragon Roost",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {89, 131, 89, 476}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 150, .resources = {1000, 400, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 280, .priority = 15, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_HUMAN_SHIPYARD] = { /* PUD 72: unit-human-shipyard */
        .doomednum = 73, .spawnhealth = 1100, .speed = 0, .radius = 16, .damage = 0,
        .name = "human-shipyard", .label = "Shipyard",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {108, 150, 108, 495}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {800, 450, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 170, .priority = 30, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 4,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_SHORE_BUILDING,
        },
    },
    [MT_ORC_SHIPYARD] = { /* PUD 73: unit-orc-shipyard */
        .doomednum = 74, .spawnhealth = 1100, .speed = 0, .radius = 16, .damage = 0,
        .name = "orc-shipyard", .label = "Shipyard",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {109, 151, 109, 496}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {800, 450, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 170, .priority = 30, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 4,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_SHORE_BUILDING,
        },
    },
    [MT_TOWN_HALL] = { /* PUD 74: unit-town-hall */
        .doomednum = 75, .spawnhealth = 1200, .speed = 0, .radius = 16, .damage = 0,
        .name = "town-hall", .label = "Town Hall",
        .w2 = {.projectile = "missile-none", .flags = HALL, .footprint = {4, 4}, .box = {126, 126},
            .grp = {100, 142, 100, 487}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 255, .resources = {1200, 800, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {1, 0},
            .points = 200, .priority = 35, .annoyance = 45, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 3,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_GREAT_HALL] = { /* PUD 75: unit-great-hall */
        .doomednum = 76, .spawnhealth = 1200, .speed = 0, .radius = 16, .damage = 0,
        .name = "great-hall", .label = "Great Hall",
        .w2 = {.projectile = "missile-none", .flags = HALL, .footprint = {4, 4}, .box = {127, 127},
            .grp = {101, 143, 101, 488}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 255, .resources = {1200, 800, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {1, 0},
            .points = 200, .priority = 35, .annoyance = 45, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 3,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_ELVEN_LUMBER_MILL] = { /* PUD 76: unit-elven-lumber-mill */
        .doomednum = 77, .spawnhealth = 600, .speed = 0, .radius = 16, .damage = 0,
        .name = "elven-lumber-mill", .label = "Elven Lumber Mill",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {102, 144, 175, 489}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 150, .resources = {600, 450, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 150, .priority = 25, .annoyance = 15, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 2,
            .income = {0, 25, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_TROLL_LUMBER_MILL] = { /* PUD 77: unit-troll-lumber-mill */
        .doomednum = 78, .spawnhealth = 600, .speed = 0, .radius = 16, .damage = 0,
        .name = "troll-lumber-mill", .label = "Troll Lumber Mill",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {103, 145, 176, 490}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 150, .resources = {600, 450, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 150, .priority = 25, .annoyance = 15, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 2,
            .income = {0, 25, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_HUMAN_FOUNDRY] = { /* PUD 78: unit-human-foundry */
        .doomednum = 79, .spawnhealth = 750, .speed = 0, .radius = 16, .damage = 0,
        .name = "human-foundry", .label = "Foundry",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {110, 152, 110, 497}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 175, .resources = {700, 400, 400}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {0, 0},
            .points = 200, .priority = 15, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_SHORE_BUILDING,
        },
    },
    [MT_ORC_FOUNDRY] = { /* PUD 79: unit-orc-foundry */
        .doomednum = 80, .spawnhealth = 750, .speed = 0, .radius = 16, .damage = 0,
        .name = "orc-foundry", .label = "Foundry",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {111, 153, 111, 498}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 175, .resources = {700, 400, 400}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {0, 0},
            .points = 200, .priority = 15, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_SHORE_BUILDING,
        },
    },
    [MT_MAGE_TOWER] = { /* PUD 80: unit-mage-tower */
        .doomednum = 81, .spawnhealth = 500, .speed = 0, .radius = 16, .damage = 0,
        .name = "mage-tower", .label = "Mage Tower",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {84, 160, 84, 505}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 125, .resources = {1000, 200, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 240, .priority = 35, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_ELEVATED,
        },
    },
    [MT_TEMPLE_OF_THE_DAMNED] = { /* PUD 81: unit-temple-of-the-damned */
        .doomednum = 82, .spawnhealth = 500, .speed = 0, .radius = 16, .damage = 0,
        .name = "temple-of-the-damned", .label = "Temple of the Damned",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {85, 161, 85, 506}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 125, .resources = {1000, 200, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 240, .priority = 35, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_HUMAN_BLACKSMITH] = { /* PUD 82: unit-human-blacksmith */
        .doomednum = 83, .spawnhealth = 775, .speed = 0, .radius = 16, .damage = 0,
        .name = "human-blacksmith", .label = "Blacksmith",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {106, 148, 106, 493}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {800, 450, 100}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {0, 0},
            .points = 170, .priority = 15, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_ORC_BLACKSMITH] = { /* PUD 83: unit-orc-blacksmith */
        .doomednum = 84, .spawnhealth = 775, .speed = 0, .radius = 16, .damage = 0,
        .name = "orc-blacksmith", .label = "Blacksmith",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {107, 149, 107, 494}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {800, 450, 100}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {0, 0},
            .points = 170, .priority = 15, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_HUMAN_REFINERY] = { /* PUD 84: unit-human-refinery */
        .doomednum = 85, .spawnhealth = 600, .speed = 0, .radius = 16, .damage = 0,
        .name = "human-refinery", .label = "Oil Refinery",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {112, 154, 112, 499}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 225, .resources = {800, 350, 200}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {0, 0},
            .points = 200, .priority = 25, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 4,
            .income = {0, 0, 25},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_SHORE_BUILDING,
        },
    },
    [MT_ORC_REFINERY] = { /* PUD 85: unit-orc-refinery */
        .doomednum = 86, .spawnhealth = 600, .speed = 0, .radius = 16, .damage = 0,
        .name = "orc-refinery", .label = "Oil Refinery",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {113, 155, 113, 500}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 225, .resources = {800, 350, 200}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {0, 0},
            .points = 200, .priority = 25, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 4,
            .income = {0, 0, 25},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_SHORE_BUILDING,
        },
    },
    [MT_HUMAN_OIL_PLATFORM] = { /* PUD 86: unit-human-oil-platform */
        .doomednum = 87, .spawnhealth = 650, .speed = 0, .radius = 16, .damage = 0,
        .name = "human-oil-platform", .label = "Oil Platform",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {114, 156, 177, 501}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {700, 450, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 0},
            .points = 160, .priority = 20, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 4,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_CAN_HARVEST,
        },
    },
    [MT_ORC_OIL_PLATFORM] = { /* PUD 87: unit-orc-oil-platform */
        .doomednum = 88, .spawnhealth = 650, .speed = 0, .radius = 16, .damage = 0,
        .name = "orc-oil-platform", .label = "Oil Platform",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {115, 157, 178, 502}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {700, 450, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 0},
            .points = 160, .priority = 20, .annoyance = 20, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 4,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_CAN_HARVEST,
        },
    },
    [MT_KEEP] = { /* PUD 88: unit-keep */
        .doomednum = 89, .spawnhealth = 1400, .speed = 0, .radius = 16, .damage = 0,
        .name = "keep", .label = "Keep",
        .w2 = {.projectile = "missile-none", .flags = HALL, .footprint = {4, 4}, .box = {127, 127},
            .grp = {86, 128, 86, 473}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 3, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {2000, 1000, 200}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {1, 0},
            .points = 600, .priority = 37, .annoyance = 40, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 3,
            .income = {10, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_STRONGHOLD] = { /* PUD 89: unit-stronghold */
        .doomednum = 90, .spawnhealth = 1400, .speed = 0, .radius = 16, .damage = 0,
        .name = "stronghold", .label = "Stronghold",
        .w2 = {.projectile = "missile-none", .flags = HALL, .footprint = {4, 4}, .box = {127, 127},
            .grp = {87, 129, 87, 474}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 2, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {2000, 1000, 200}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {1, 0},
            .points = 600, .priority = 37, .annoyance = 40, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 3,
            .income = {10, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_CASTLE] = { /* PUD 90: unit-castle */
        .doomednum = 91, .spawnhealth = 1600, .speed = 0, .radius = 16, .damage = 0,
        .name = "castle", .label = "Castle",
        .w2 = {.projectile = "missile-none", .flags = HALL, .footprint = {4, 4}, .box = {127, 127},
            .grp = {116, 158, 116, 503}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 6, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {2500, 1200, 500}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {1, 0},
            .points = 1500, .priority = 40, .annoyance = 50, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 3,
            .income = {20, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_FORTRESS] = { /* PUD 91: unit-fortress */
        .doomednum = 92, .spawnhealth = 1600, .speed = 0, .radius = 16, .damage = 0,
        .name = "fortress", .label = "Fortress",
        .w2 = {.projectile = "missile-none", .flags = HALL, .footprint = {4, 4}, .box = {127, 127},
            .grp = {117, 159, 117, 504}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 6, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 200, .resources = {2500, 1200, 500}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {1, 0},
            .points = 1500, .priority = 40, .annoyance = 50, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 3,
            .income = {20, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_GOLD_MINE] = { /* PUD 92: unit-gold-mine */
        .doomednum = 93, .spawnhealth = 25500, .speed = 0, .radius = 16, .damage = 0,
        .name = "gold-mine", .label = "Gold Mine",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {119, 162, 179, 511}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 150, .resources = {0, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 0},
            .points = 0, .priority = 0, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 1,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_NEUTRAL | W2_CAN_HARVEST,
        },
    },
    [MT_OIL_PATCH] = { /* PUD 93: unit-oil-patch */
        .doomednum = 94, .spawnhealth = 0, .speed = 0, .radius = 16, .damage = 0,
        .name = "oil-patch", .label = "Oil Patch",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {3, 3}, .box = {95, 95},
            .grp = {118, 118, 180, 515}, .speed = 0, .armor = 0,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 0, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 0, .resources = {0, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 0},
            .points = 0, .priority = 0, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_SEA, .gives_mask = 4,
            .attributes = W2_INDESTRUCTIBLE | W2_VISIBLE_UNDER_FOG | W2_NEUTRAL,
        },
    },
    [MT_HUMAN_START_LOCATION] = { /* PUD 94: unit-human-start-location */
        .doomednum = 95, .spawnhealth = 0, .speed = 0, .radius = 16, .damage = 0,
        .name = "human-start", .label = "Start Location",
        .w2 = {.projectile = "missile-none", .flags = W2_SKIP, .footprint = {1, 1}, .box = {31, 31},
            .grp = {164, 0, 0, 0}, .speed = 0, .armor = 0,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 0, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 0, .resources = {0, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 0},
            .points = 0, .priority = 0, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_INDESTRUCTIBLE,
        },
    },
    [MT_ORC_START_LOCATION] = { /* PUD 95: unit-orc-start-location */
        .doomednum = 96, .spawnhealth = 0, .speed = 0, .radius = 16, .damage = 0,
        .name = "orc-start", .label = "Start Location",
        .w2 = {.projectile = "missile-none", .flags = W2_SKIP, .footprint = {1, 1}, .box = {31, 31},
            .grp = {165, 0, 0, 0}, .speed = 0, .armor = 0,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 0, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 0, .resources = {0, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 0},
            .points = 0, .priority = 0, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_INDESTRUCTIBLE,
        },
    },
    [MT_HUMAN_GUARD_TOWER] = { /* PUD 96: unit-human-guard-tower */
        .doomednum = 97, .spawnhealth = 130, .speed = 0, .radius = 16, .damage = 16,
        .name = "human-guard-tower", .label = "Guard Tower",
        .w2 = {.projectile = "missile-arrow", .flags = BLD, .footprint = {2, 2}, .box = {63, 63},
            .grp = {80, 169, 80, 507}, .speed = 0, .armor = 20,
            .basic_damage = 4, .piercing_damage = 12, .damage_min = 6,
            .sight = 9, .attack_range = 6, .min_attack_range = 0,
            .reaction_range = {6, 6},
            .costs = {.time = 140, .resources = {500, 150, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 200, .priority = 50, .annoyance = 60, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_DETECT_CLOAK | W2_VISIBLE_UNDER_FOG | W2_ELEVATED | W2_CAN_ATTACK,
        },
    },
    [MT_ORC_GUARD_TOWER] = { /* PUD 97: unit-orc-guard-tower */
        .doomednum = 98, .spawnhealth = 130, .speed = 0, .radius = 16, .damage = 16,
        .name = "orc-guard-tower", .label = "Guard Tower",
        .w2 = {.projectile = "missile-arrow", .flags = BLD, .footprint = {2, 2}, .box = {63, 63},
            .grp = {81, 170, 81, 508}, .speed = 0, .armor = 20,
            .basic_damage = 4, .piercing_damage = 12, .damage_min = 6,
            .sight = 9, .attack_range = 6, .min_attack_range = 0,
            .reaction_range = {6, 6},
            .costs = {.time = 140, .resources = {500, 150, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 200, .priority = 50, .annoyance = 60, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 7, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_DETECT_CLOAK | W2_VISIBLE_UNDER_FOG | W2_ELEVATED | W2_CAN_ATTACK,
        },
    },
    [MT_HUMAN_CANNON_TOWER] = { /* PUD 98: unit-human-cannon-tower */
        .doomednum = 99, .spawnhealth = 160, .speed = 0, .radius = 16, .damage = 50,
        .name = "human-cannon-tower", .label = "Cannon Tower",
        .w2 = {.projectile = "missile-small-cannon", .flags = BLD, .footprint = {2, 2}, .box = {63, 63},
            .grp = {82, 171, 82, 509}, .speed = 0, .armor = 20,
            .basic_damage = 50, .piercing_damage = 0, .damage_min = 20,
            .sight = 9, .attack_range = 7, .min_attack_range = 2,
            .reaction_range = {7, 7},
            .costs = {.time = 190, .resources = {1000, 300, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 250, .priority = 60, .annoyance = 70, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 3, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_DETECT_CLOAK | W2_VISIBLE_UNDER_FOG | W2_ELEVATED | W2_CAN_ATTACK,
        },
    },
    [MT_ORC_CANNON_TOWER] = { /* PUD 99: unit-orc-cannon-tower */
        .doomednum = 100, .spawnhealth = 160, .speed = 0, .radius = 16, .damage = 50,
        .name = "orc-cannon-tower", .label = "Cannon Tower",
        .w2 = {.projectile = "missile-small-cannon", .flags = BLD, .footprint = {2, 2}, .box = {63, 63},
            .grp = {83, 172, 83, 510}, .speed = 0, .armor = 20,
            .basic_damage = 50, .piercing_damage = 0, .damage_min = 20,
            .sight = 9, .attack_range = 7, .min_attack_range = 2,
            .reaction_range = {7, 7},
            .costs = {.time = 190, .resources = {1000, 300, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .points = 250, .priority = 60, .annoyance = 70, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 3, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_DETECT_CLOAK | W2_VISIBLE_UNDER_FOG | W2_ELEVATED | W2_CAN_ATTACK,
        },
    },
    [MT_CIRCLE_OF_POWER] = { /* PUD 100: unit-circle-of-power */
        .doomednum = 101, .spawnhealth = 0, .speed = 0, .radius = 16, .damage = 0,
        .name = "circle-of-power", .label = "Circle of Power",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {2, 2}, .box = {63, 63},
            .grp = {166, 166, 166, 525}, .speed = 0, .armor = 0,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 0, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 0, .resources = {0, 0, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 0},
            .points = 0, .priority = 0, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_INDESTRUCTIBLE | W2_VISIBLE_UNDER_FOG | W2_NEUTRAL,
        },
    },
    [MT_DARK_PORTAL] = { /* PUD 101: unit-dark-portal */
        .doomednum = 102, .spawnhealth = 5000, .speed = 0, .radius = 16, .damage = 0,
        .name = "dark-portal", .label = "Dark Portal",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {4, 4}, .box = {127, 127},
            .grp = {167, 184, 185, 513}, .speed = 0, .armor = 0,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 4, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 100, .resources = {3000, 3000, 1000}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 1}}, .food = {0, 0},
            .mana = {255, 85, 1},
            .points = 0, .priority = 0, .annoyance = 0, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_BUILDER_OUTSIDE | W2_TELEPORTER,
        },
    },
    [MT_RUNESTONE] = { /* PUD 102: unit-runestone */
        .doomednum = 103, .spawnhealth = 5000, .speed = 0, .radius = 16, .damage = 0,
        .name = "runestone", .label = "Runestone",
        .w2 = {.projectile = "missile-none", .flags = BLD, .footprint = {2, 2}, .box = {63, 63},
            .grp = {181, 186, 181, 514}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 4, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 175, .resources = {900, 500, 0}},
            .repair = {.hp = 4, .range = 0, .auto_range = 0, .costs = {1, 1, 0}}, .food = {0, 0},
            .mana = {255, 85, 1},
            .points = 150, .priority = 15, .annoyance = 35, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG | W2_BUILDER_OUTSIDE,
        },
    },
    [MT_HUMAN_WALL] = { /* PUD 103: unit-human-wall */
        .doomednum = 104, .spawnhealth = 40, .speed = 0, .radius = 16, .damage = 0,
        .name = "human-wall", .label = "Wall",
        .w2 = {.projectile = "missile-none", .flags = W2_SKIP | BLD, .footprint = {1, 1}, .box = {31, 31},
            .grp = {0, 0, 0, 0}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 30, .resources = {20, 10, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 0},
            .points = 1, .priority = 0, .annoyance = 45, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
    [MT_ORC_WALL] = { /* PUD 104: unit-orc-wall */
        .doomednum = 105, .spawnhealth = 40, .speed = 0, .radius = 16, .damage = 0,
        .name = "orc-wall", .label = "Wall",
        .w2 = {.projectile = "missile-none", .flags = W2_SKIP | BLD, .footprint = {1, 1}, .box = {31, 31},
            .grp = {0, 0, 0, 0}, .speed = 0, .armor = 20,
            .basic_damage = 0, .piercing_damage = 0, .damage_min = 0,
            .sight = 1, .attack_range = 0, .min_attack_range = 0,
            .reaction_range = {0, 0},
            .costs = {.time = 30, .resources = {20, 10, 0}},
            .repair = {.hp = 0, .range = 0, .auto_range = 0, .costs = {0, 0, 0}}, .food = {0, 0},
            .points = 1, .priority = 0, .annoyance = 45, .level = 1,
            .decay = 0, .transport_capacity = 0, .target_mask = 0, .store_mask = 0,
            .income = {0, 0, 0},
            .domain = W2_DOMAIN_LAND, .gives_mask = 0,
            .attributes = W2_VISIBLE_UNDER_FOG,
        },
    },
};

#undef LAND
#undef HARV
#undef SHIP
#undef TANK
#undef FLY
#undef BALLOON
#undef BLD
#undef HALL
#undef CRIT


gameinfo_t game_info;

static int stand_state(int pud) { return 1 + pud * 2; }

/* Attack and death rows follow the retail GRP layout, decoded by phase
 * count: stand, four walk frames, then the attack frames and the death
 * frames (Wargus anim.lua: footman 25..40 / 45..55, archer 25..30 / 35..45,
 * knight adds two decay frames, peasants chop with five frames). Timings
 * are the Wargus waits. Siege and ships have separate short layouts.
 * Structures hold two frames,
 * the finished building and its half-built picture; they leave rubble. */
/* Logical frames (rows of five facings) in each type's forest MAINDAT GRP,
 * so the state rows exist before any art loads; w2_build_states rebuilds
 * them from the decoded sheet. Reserved and art-less slots are zero. */
static const uint8_t w2_phases[W2_TYPE_COUNT] = {
    12, 12, 13, 13, 4, 4, 14, 14, 10, 12, 16, 13, 14, 14, 13, 15, 13, 13, 10, 12,
    10, 13, 13, 14, 16, 12, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 3, 3,
    4, 2, 13, 10, 14, 1, 12, 12, 0, 14, 14, 13, 14, 12, 0, 14, 15, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 3, 3, 2, 2, 2, 2, 2, 1, 0, 0, 2, 2, 2, 2,
    1, 1, 1, 0, 0,
};
_Static_assert(sizeof(w2_phases) == W2_TYPE_COUNT, "phase table");

static void build_combat_states(int pud, int phases) {
    mobjinfo_t *unit = &mobjinfo[pud + 1];
    int stand = stand_state(pud);
    int attack = W2_ATTACK_STATE(pud), death = W2_DEATH_STATE(pud);
    bool structure = (unit->w2.flags & W2_STRUCTURE) != 0;
    bool worker = (unit->w2.flags & W2_HARVEST) != 0;
    int hit_first = 0, hit_count = 0, hit_frame = 0, recover_frame = 0;
    int windup_tics = 3, hit_tics = 5, recover_tics = 10;
    int fall_first = 0, fall_count = 0;
    int art_frames = phases;
    if (structure) phases = 1; /* A structure's extra frame is construction, not an attack row. */
    if (phases >= 12) { /* Melee rows: three windup frames, the blow, then stand. */
        hit_first = 5; hit_count = 3; hit_frame = 8; recover_frame = 0;
        fall_first = 9; fall_count = phases - 9;
        if (worker) { recover_frame = 9; fall_first = 10; fall_count = 3; }
        else if (phases == 13) { fall_first = 10; fall_count = 3; }
    } else if (phases == 10) { /* Archer rows: draw, loose, a long rest. */
        hit_first = 5; hit_count = 1; hit_frame = 6; windup_tics = 10; hit_tics = 10; recover_tics = 44;
        fall_first = 7; fall_count = 3;
    } else if (phases == 4) { /* Siege: wind, loose, reload. */
        hit_first = 2; hit_count = 1; hit_frame = 3; windup_tics = 25; hit_tics = 125; recover_tics = 49;
    } else if ((unit->w2.flags & W2_SEA) && !(unit->w2.attributes & W2_PERMANENT_CLOAK)) {
        /* Surface ships fire in frame 0. Rows 1 and 2 are sinking art. */
        windup_tics = 0;
        hit_tics = unit->w2.projectile && !strcmp(unit->w2.projectile, "missile-big-cannon") ? 229 : 119;
        recover_tics = 1;
    } else { /* Single-frame art, such as towers. */
        windup_tics = 1; hit_tics = 1; recover_tics = 58;
    }
    states[attack] = (state_t){
        .sprite = pud, .frame = hit_first, .count = hit_count > 0 ? hit_count : 1,
        .tics = windup_tics, .nextstate = attack + 1, .group = W2_GROUP_ATTACK,
    };
    states[attack + 1] = (state_t){
        .sprite = pud, .frame = hit_frame, .count = 1, .tics = hit_tics,
        .action = A_W2_Attack, .nextstate = attack + 2, .group = W2_GROUP_ATTACK,
    };
    states[attack + 2] = (state_t){
        .sprite = pud, .frame = recover_frame, .count = 1, .tics = recover_tics,
        .nextstate = stand, .group = W2_GROUP_ATTACK,
    };
    unit->missilestate = unit->damage > 0 ? attack : 0;
    if (structure) {
        /* Wargus animations-destroyed-place: two rubble frames of 200 cycles
         * each on the shared destroyed-site sheet (frames 2 and 3 over
         * water), then the corpse vanishes. The ground clears at once. */
        bool small = unit->w2.footprint.w <= 1 && unit->w2.footprint.h <= 1;
        bool water = !small && (unit->w2.domain == W2_DOMAIN_SEA || (unit->w2.attributes & W2_SHORE_BUILDING));
        int sprite = small ? W2_SPRITE_SMALL_RUBBLE : W2_SPRITE_RUBBLE;
        states[death] = (state_t){
            .sprite = sprite, .frame = water ? 2 : 0, .count = 1, .tics = 200,
            .action = A_W2_Collapse, .nextstate = death + 1, .group = W2_GROUP_DEATH,
        };
        states[death + 1] = (state_t){
            .sprite = sprite, .frame = water ? 3 : 1, .count = 1, .tics = 200,
            .nextstate = 0, .group = W2_GROUP_DEATH,
        };
        unit->deathstate = death;
        /* Wargus constructions: the site for the first quarter, its
         * framework to the half, then the type's own half-built frame. */
        int build = W2_BUILD_STATE(pud);
        int site_sprite = W2_SPRITE_CONSTRUCTION;
        /* Native construction sheets, paired human/orc in the archive. */
        if (pud + 1 == MT_HUMAN_SHIPYARD || pud + 1 == MT_ORC_SHIPYARD)
            site_sprite = W2_NAVAL_SITE_SPRITE + pud + 1 - MT_HUMAN_SHIPYARD;
        else if (pud + 1 == MT_HUMAN_OIL_PLATFORM || pud + 1 == MT_ORC_OIL_PLATFORM)
            site_sprite = W2_NAVAL_SITE_SPRITE + 2 + pud + 1 - MT_HUMAN_OIL_PLATFORM;
        else if (pud + 1 == MT_HUMAN_REFINERY || pud + 1 == MT_ORC_REFINERY)
            site_sprite = W2_NAVAL_SITE_SPRITE + 4 + pud + 1 - MT_HUMAN_REFINERY;
        else if (pud + 1 == MT_HUMAN_FOUNDRY || pud + 1 == MT_ORC_FOUNDRY)
            site_sprite = W2_NAVAL_SITE_SPRITE + 6 + pud + 1 - MT_HUMAN_FOUNDRY;
        for (int stage = 0; stage < 3; ++stage)
            states[build + stage] = (state_t){
                .sprite = stage < 2 ? site_sprite : pud,
                .frame = stage < 2 ? stage : (art_frames >= 2 ? 1 : 0), .count = 1,
                .tics = -1, .nextstate = build + stage, .group = W2_GROUP_BUILD,
            };
    } else if ((unit->w2.flags & W2_SEA) && !(unit->w2.attributes & W2_PERMANENT_CLOAK)) {
        states[death] = (state_t){.sprite = pud, .frame = 1, .count = 1, .tics = 50,
            .nextstate = death + 1, .group = W2_GROUP_DEATH};
        states[death + 1] = (state_t){.sprite = pud, .frame = 2, .count = 1, .tics = 51,
            .nextstate = 0, .group = W2_GROUP_DEATH};
        unit->deathstate = death;
    } else if (fall_count >= 3) {
        /* Two falling frames, a long rest on the ground, then the decay
         * frames (knights, ogres) before the corpse is removed. */
        states[death] = (state_t){
            .sprite = pud, .frame = fall_first, .count = 2, .tics = 3,
            .nextstate = death + 1, .group = W2_GROUP_DEATH,
        };
        states[death + 1] = (state_t){
            .sprite = pud, .frame = fall_first + 2, .count = 1, .tics = 100,
            .nextstate = fall_count > 3 ? death + 2 : 0, .group = W2_GROUP_DEATH,
        };
        if (fall_count > 3)
            states[death + 2] = (state_t){
                .sprite = pud, .frame = fall_first + 3, .count = fall_count - 3, .tics = 200,
                .nextstate = 0, .group = W2_GROUP_DEATH,
            };
        unit->deathstate = death;
    } else {
        unit->deathstate = 0; /* No death art: the engine removes it at once. */
    }
    if ((unit->w2.flags & W2_SEA) && (unit->w2.attributes & W2_PERMANENT_CLOAK)) {
        /* Submarines use five attack poses and no sinking frames. Reuse
         * their otherwise unused death slots for the last two poses. */
        static const int frames[] = {1, 2, 2, 1, 0}, waits[] = {10, 25, 25, 25, 30};
        for (int i = 0; i < 5; ++i)
            states[attack + i] = (state_t){.sprite = pud, .frame = frames[i], .count = 1,
                .tics = waits[i], .action = i == 2 ? A_W2_Attack : NULL,
                .nextstate = i == 4 ? stand : attack + i + 1, .group = W2_GROUP_ATTACK};
    }
}

void w2_build_states(int pud, int phases) {
    if (pud < 0 || pud >= W2_TYPE_COUNT) return;
    mobjinfo_t *unit = &mobjinfo[pud + 1];
    int stand = stand_state(pud);
    int walk = stand + 1;
    build_combat_states(pud, phases);
    int first = 1, count = 4;
    /* Wargus Move scripts: vehicles do not share infantry's rows 1..4.
     * GRP row count includes attack/death art and cannot select a cycle. */
    if (unit->w2.flags & W2_SEA || pud + 1 == MT_ZEPPELIN ||
        pud + 1 == MT_EYE_OF_KILROGG) {
        first = 0;
        count = 1;
    } else if (pud + 1 == MT_BALLISTA || pud + 1 == MT_CATAPULT ||
               pud + 1 == MT_FLYING_MACHINE) {
        first = 0;
        count = 2;
    }
    if (phases <= first) { first = 0; count = 1; }
    else if (count > phases - first) count = phases - first;
    bool mobile = (unit->w2.flags & W2_MOBILE) != 0;
    states[walk] = (state_t){
        .sprite = pud, .frame = first, .count = count, .tics = W2_WALK_TICS,
        .action = mobile ? A_Chase : NULL,
        .nextstate = walk, .group = W2_GROUP_WALK,
    };
    unit->seestate = mobile ? walk : stand;
}

/* ── Tile-based fog of war ────────────────────────────────────────────── */

/* Megatiles 0..15 are the native shroud masks. Index 0 is the hole and
 * index 239 is black. Stratagus TiledFogTable selects one mask from the
 * four corners that a hidden neighbor touches:
 * bit 0 top-right, bit 1 top-left, bit 2 bottom-right, bit 3 bottom-left. */
static const int tiled_fog_table[16] = {
     0, 11, 10, 2, 13, 6, 14, 3,
    12, 15,  4, 1,  8, 9,  7, 0,
};

enum { W2_FOG_VISIBLE, W2_FOG_EXPLORED, W2_FOG_UNEXPLORED };

static int w2_cell_fog(const level_t *map, int x, int y) {
    if (!map->sight.cells) return W2_FOG_VISIBLE;
    if (!L_Contains(map, x, y)) return W2_FOG_UNEXPLORED;
    uint32_t bits = map->sight.cells[L_Index(map, x, y)];
    if (!(bits & SIGHT_EXPLORED)) return W2_FOG_UNEXPLORED;
    return (bits & map->sight.allies[consoleplayer]) ? W2_FOG_VISIBLE : W2_FOG_EXPLORED;
}

static int w2_fog_index(const level_t *map, int cx, int cy, int match) {
    int tl = w2_cell_fog(map, cx - 1, cy - 1) == match;
    int t  = w2_cell_fog(map, cx,     cy - 1) == match;
    int tr = w2_cell_fog(map, cx + 1, cy - 1) == match;
    int l  = w2_cell_fog(map, cx - 1, cy    ) == match;
    int r  = w2_cell_fog(map, cx + 1, cy    ) == match;
    int bl = w2_cell_fog(map, cx - 1, cy + 1) == match;
    int b  = w2_cell_fog(map, cx,     cy + 1) == match;
    int br = w2_cell_fog(map, cx + 1, cy + 1) == match;
    int v = 0;
    if (t || r || tr) v |= 1;
    if (t || l || tl) v |= 2;
    if (b || r || br) v |= 4;
    if (b || l || bl) v |= 8;
    return v;
}

/* `solid` paints every mask pixel. Explored fog keeps only the mask's
 * even (x + y) phase, the same stipple the native edge art already uses. */
static void w2_draw_fog_tile(const tileset_t *tileset, int tile, int dx, int dy,
                             int cell_w, int cell_h, bool solid) {
    if (tile < 0 || tile >= tileset->count || !tileset->indices) return;
    const uint8_t *src = tileset->indices + (size_t)tile * tileset->tile_w * tileset->tile_h;
    uint8_t black_idx = V_NearestIndex(0xff000000u);
    for (int py = 0; py < cell_h; ++py) {
        int sy = py * tileset->tile_h / cell_h;
        int screen_y = dy + py;
        if (screen_y < 0 || screen_y >= screens[0].h) continue;
        uint8_t *row = screens[0].pixels + (size_t)screen_y * screens[0].w;
        for (int px = 0; px < cell_w; ++px) {
            int sx = px * tileset->tile_w / cell_w;
            if (!src[sy * tileset->tile_w + sx]) continue;
            if (!solid && ((px + py) & 1)) continue;
            int screen_x = dx + px;
            if (screen_x < 0 || screen_x >= screens[0].w) continue;
            row[screen_x] = black_idx;
        }
    }
}

static void w2_draw_fog(app_t *app, const level_t *map, const tileset_t *tileset) {
    if (!map->sight.cells || !screens[0].pixels || tileset->count < 16) return;
    int cell_w = app->cell.w > 0 ? app->cell.w : TILE_W;
    int cell_h = app->cell.h > 0 ? app->cell.h : TILE_H;
    irect_t view = G_WorldViewport(app);
    int origin = view.x < 0 ? 0 : view.x;
    int width = view.w;
    if (width > screens[0].w - origin) width = screens[0].w - origin;
    int height = app->win.h < screens[0].h ? app->win.h : screens[0].h;
    uint8_t black = V_NearestIndex(0xff000000u);

    /* Pass 1: unexplored cells solid black, explored cells stippled. */
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            int state = w2_cell_fog(map, x, y);
            if (state == W2_FOG_VISIBLE) continue;
            float sx, sy;
            R_GridToScreen(app, (float)x, (float)y, &sx, &sy);
            int dx = (int)sx, dy = (int)sy;
            if (dx + cell_w <= origin || dx >= origin + width ||
                dy + cell_h <= 0 || dy >= height) continue;
            if (state == W2_FOG_UNEXPLORED) {
                V_FillRect((irect_t){ dx, dy, cell_w, cell_h }, black);
            } else {
                for (int py = 0; py < cell_h; ++py) {
                    int screen_y = dy + py;
                    if (screen_y < 0 || screen_y >= height) continue;
                    uint8_t *row = screens[0].pixels + (size_t)screen_y * screens[0].w;
                    int x0 = dx < origin ? origin : dx;
                    int x1 = dx + cell_w > origin + width ? origin + width : dx + cell_w;
                    for (int screen_x = x0; screen_x < x1; ++screen_x)
                        if (((screen_x - dx + py) & 1) == 0)
                            row[screen_x] = black;
                }
            }
        }
    }

    /* Pass 2: shroud edges — seen cells bordering unexplored. */
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            if (w2_cell_fog(map, x, y) == W2_FOG_UNEXPLORED) continue;
            int v = w2_fog_index(map, x, y, W2_FOG_UNEXPLORED);
            if (!v) continue;
            int tile = tiled_fog_table[v];
            float sx, sy;
            R_GridToScreen(app, (float)x, (float)y, &sx, &sy);
            int dx = (int)sx, dy = (int)sy;
            if (dx + cell_w <= origin || dx >= origin + width ||
                dy + cell_h <= 0 || dy >= height) continue;
            w2_draw_fog_tile(tileset, tile, dx, dy, cell_w, cell_h, true);
        }
    }

    /* Pass 3: fog edges — visible cells bordering explored ground.
     * Unexplored corners stay on the solid shroud mask from pass 2. */
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            if (w2_cell_fog(map, x, y) != W2_FOG_VISIBLE) continue;
            int v = w2_fog_index(map, x, y, W2_FOG_EXPLORED);
            if (!v) continue;
            int tile = tiled_fog_table[v];
            float sx, sy;
            R_GridToScreen(app, (float)x, (float)y, &sx, &sy);
            int dx = (int)sx, dy = (int)sy;
            if (dx + cell_w <= origin || dx >= origin + width ||
                dy + cell_h <= 0 || dy >= height) continue;
            w2_draw_fog_tile(tileset, tile, dx, dy, cell_w, cell_h, false);
        }
    }
}

void w2_build_info(void) {
    memset(sprnames, 0, sizeof(sprnames));
    memset(states, 0, sizeof(states));
    states[0] = (state_t){
        .tics = -1, .nextstate = 0,
    };
    mobjinfo[MT_W2_EFFECT] = (mobjinfo_t){.name = "warcraft-effect", .spawnhealth = 1,
        .spawnstate = W2_EFFECT_STATE};
    states[W2_EFFECT_STATE] = (state_t){.sprite = W2_EFFECT_SPRITE, .tics = 1,
        .count = 1, .action = A_W2_Effect, .nextstate = W2_EFFECT_STATE};
    for (int i = 0; i < W2_FX_COUNT; ++i) sprnames[W2_EFFECT_SPRITE + i] = w2_effects[i].name;
    for (int pud = 0; pud < W2_TYPE_COUNT; ++pud) {
        mobjinfo_t *unit = &mobjinfo[pud + 1];
        bool fighter = (unit->w2.attributes & W2_CAN_ATTACK) && unit->damage > 0;
        int stand = stand_state(pud);
        sprnames[pud] = unit->name;
        /* Fighters glance around every few tics (Doom's A_Look cadence). */
        states[stand] = (state_t){
            .sprite = pud, .frame = 0, .count = 1, .tics = fighter ? 4 : -1,
            .action = fighter ? A_Look : NULL,
            .nextstate = stand, .group = W2_GROUP_STAND,
        };
        unit->spawnstate = stand;
        w2_build_states(pud, w2_phases[pud]);
    }
    static const int chop_frames[] = { 5, 6, 7, 8, 9, 5, 5 };
    static const int chop_tics[] = { 3, 3, 3, 5, 3, 7, 1 };
    for (int pud = 2; pud <= 3; ++pud) {
        int start = W2_WORK_STATE(pud);
        for (int i = 0; i < 7; ++i)
            states[start + i] = (state_t){
                .sprite = pud, .frame = chop_frames[i], .count = 1,
                .action = i == 3 ? A_W2_Chop : NULL,
                .tics = chop_tics[i], .nextstate = i == 6 ? stand_state(pud) : start + i + 1,
                .group = 5,
            };
        states[W2_WAIT_STATE(pud)] = (state_t){
            .sprite = pud, .count = 1, .tics = mobjinfo[pud + 1].w2.gather[0].resource_wait,
            .nextstate = stand_state(pud), .group = 5,
        };
        for (int i = 0; i < 7; ++i) {
            int repair = W2_REPAIR_STATE(pud);
            states[repair + i] = (state_t){
                .sprite = pud, .frame = chop_frames[i], .count = 1,
                .action = i == 6 ? A_W2_Repair : NULL,
                .tics = chop_tics[i], .nextstate = i == 6 ? stand_state(pud) : repair + i + 1,
                .group = W2_GROUP_WORK,
            };
        }
    }
    static const char *const carriers[] = { "peasant-gold", "peasant-lumber", "peon-gold", "peon-lumber" };
    static const char *const naval_sites[] = {"human-shipyard-site", "orc-shipyard-site",
        "human-oil-well-site", "orc-oil-well-site", "human-refinery-site", "orc-refinery-site",
        "human-foundry-site", "orc-foundry-site"};
    for (int i = 0; i < 8; ++i) sprnames[W2_NAVAL_SITE_SPRITE + i] = naval_sites[i];
    for (int side = 0; side < 2; ++side) {
        int stand = W2_TANK_CARRY_STATE(side), sprite = W2_TANK_FULL_SPRITE + side;
        sprnames[sprite] = side ? "orc-tanker-full" : "human-tanker-full";
        states[stand] = (state_t){.sprite = sprite, .count = 1, .tics = -1, .nextstate = stand};
        states[stand + 1] = (state_t){.sprite = sprite, .count = 1, .tics = W2_WALK_TICS,
            .nextstate = stand + 1, .group = W2_GROUP_WALK};
        int pud = MT_HUMAN_OIL_PLATFORM + side - 1, idle = stand_state(pud);
        states[idle].tics = 5;
        states[idle].action = A_W2_Platform;
        int active = W2_PLATFORM_ACTIVE_STATE(side);
        states[active] = (state_t){.sprite = pud, .frame = 2, .count = 1, .tics = 5,
            .action = A_W2_Platform, .nextstate = active};
    }
    for (int pud = 26; pud <= 27; ++pud)
        states[W2_TANK_WAIT_STATE(pud)] = (state_t){.sprite = pud, .count = 1,
            .tics = mobjinfo[pud + 1].w2.gather[2].resource_wait,
            .nextstate = stand_state(pud), .group = W2_GROUP_WORK};
    sprnames[W2_SPRITE_CONSTRUCTION] = "construction-site";
    sprnames[W2_SPRITE_RUBBLE] = "destroyed-site";
    sprnames[W2_SPRITE_SMALL_RUBBLE] = "small-destroyed-site";
    for (int i = 0; i < 4; ++i) {
        int stand = W2_CARRY_STATE(i);
        sprnames[W2_TYPE_COUNT + i] = carriers[i];
        states[stand] = (state_t){ .sprite = W2_TYPE_COUNT + i, .count = 1,
            .tics = -1, .nextstate = stand };
        states[stand + 1] = (state_t){ .sprite = W2_TYPE_COUNT + i, .frame = 1,
            .count = 4, .tics = W2_WALK_TICS, .nextstate = stand + 1, .group = 2 };
    }
    game_info = (gameinfo_t){
        .sprnames = (const char *const *)sprnames,
        .sprite_count = W2_SPRITE_COUNT,
        .states = states,
        .state_count = W2_STATE_COUNT,
        .mobjinfo = mobjinfo,
        .mobj_type_count = W2_MOBJ_COUNT,
        .null_state = 0,
        .state_coord_mode = RTS_STATE_COORDS_GROUND_OFFSET,
        .selection_marker = { .style = SELECTION_STYLE_DEFAULT },
        .draw_underlays = w2_draw_selection,
        .draw_overlays = w2_draw_buffs,
        .right_click_orders = true,
        .select_any = true,
        .f10_menu = true,
        .instant_turn = true,
        .sound = &w2_soundinfo,
        .draw_fog = w2_draw_fog,
    };
}

int w2_pud_named(const char *name) {
    if (!name || !name[0]) return -1;
    for (int type = 1; type < NUMMOBJTYPES; ++type)
        if (mobjinfo[type].name && strcmp(mobjinfo[type].name, name) == 0) return type - 1;
    return -1;
}
