#define _DEFAULT_SOURCE
#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* Only the production relationships live here. Prices and durations belong
 * to mobjinfo[] and the research table; G_PlayerBuildProduct spends lumber
 * and oil on the training command, G_QueueProduct the gold. */
#define U RTS_PRODUCT_UNIT
#define R RTS_PRODUCT_UPGRADE
#define B RTS_PRODUCT_BUILDING
static int w2_product_count;
static StaticProductDefinition W2_PRODUCTS[256] = {
    { 1, 1, "Footman",    0, 0, U, MT_FOOTMAN, 0, {0}, 0, {MT_HUMAN_BARRACKS}, 1 },
    { 2, 2, "Grunt",      0, 0, U, MT_GRUNT, 0, {0}, 0, {MT_ORC_BARRACKS}, 1 },
    { 3, 3, "Peasant",    0, 0, U, MT_PEASANT, 0, {0}, 0, {MT_TOWN_HALL, MT_KEEP, MT_CASTLE}, 3 },
    { 4, 4, "Peon",       0, 0, U, MT_PEON, 0, {0}, 0, {MT_GREAT_HALL, MT_STRONGHOLD, MT_FORTRESS}, 3 },
    { 5, 5, "Archer",     0, 0, U, MT_ARCHER, 0, {MT_ELVEN_LUMBER_MILL}, 1, {MT_HUMAN_BARRACKS}, 1 },
    { 6, 6, "Axethrower", 0, 0, U, MT_AXETHROWER, 0, {MT_TROLL_LUMBER_MILL}, 1, {MT_ORC_BARRACKS}, 1 },
    { 7, 7, "Ballista",   0, 0, U, MT_BALLISTA, 0, {MT_HUMAN_BLACKSMITH, MT_ELVEN_LUMBER_MILL}, 2, {MT_HUMAN_BARRACKS}, 1 },
    { 8, 8, "Catapult",   0, 0, U, MT_CATAPULT, 0, {MT_ORC_BLACKSMITH, MT_TROLL_LUMBER_MILL}, 2, {MT_ORC_BARRACKS}, 1 },
    { 9, 9, "Knight",     0, 0, U, MT_KNIGHT, 0, {MT_STABLES, MT_HUMAN_BLACKSMITH}, 2, {MT_HUMAN_BARRACKS}, 1 },
    { 10, 10, "Ogre",     0, 0, U, MT_OGRE, 0, {MT_OGRE_MOUND, MT_ORC_BLACKSMITH}, 2, {MT_ORC_BARRACKS}, 1 },
    /* Research: the maker is the upgrade table's building. */
    { 11, W2_UI_SWORD1, "Upgrade sword", 0, 0, R, W2_UPGRADE_SWORD1, 0, {0}, 0, {MT_HUMAN_BLACKSMITH}, 1 },
    { 12, W2_UI_SWORD2, "Upgrade sword", 0, 0, R, W2_UPGRADE_SWORD2, 0, {0}, 0, {MT_HUMAN_BLACKSMITH}, 1 },
    { 13, W2_UI_AXE1, "Upgrade battle axe", 0, 0, R, W2_UPGRADE_AXE1, 1, {0}, 0, {MT_ORC_BLACKSMITH}, 1 },
    { 14, W2_UI_AXE2, "Upgrade battle axe", 0, 0, R, W2_UPGRADE_AXE2, 1, {0}, 0, {MT_ORC_BLACKSMITH}, 1 },
    { 15, W2_UI_ARROW1, "Upgrade arrows", 0, 0, R, W2_UPGRADE_ARROW1, 0, {0}, 0, {MT_ELVEN_LUMBER_MILL}, 1 },
    { 16, W2_UI_ARROW2, "Upgrade arrows", 0, 0, R, W2_UPGRADE_ARROW2, 0, {0}, 0, {MT_ELVEN_LUMBER_MILL}, 1 },
    { 17, W2_UI_THROWING_AXE1, "Upgrade throwing axe", 0, 0, R, W2_UPGRADE_THROWING_AXE1, 1, {0}, 0, {MT_TROLL_LUMBER_MILL}, 1 },
    { 18, W2_UI_THROWING_AXE2, "Upgrade throwing axe", 0, 0, R, W2_UPGRADE_THROWING_AXE2, 1, {0}, 0, {MT_TROLL_LUMBER_MILL}, 1 },
    { 19, W2_UI_HUMAN_SHIELD1, "Upgrade shield", 0, 0, R, W2_UPGRADE_HUMAN_SHIELD1, 0, {0}, 0, {MT_HUMAN_BLACKSMITH}, 1 },
    { 20, W2_UI_HUMAN_SHIELD2, "Upgrade shield", 0, 0, R, W2_UPGRADE_HUMAN_SHIELD2, 0, {0}, 0, {MT_HUMAN_BLACKSMITH}, 1 },
    { 21, W2_UI_ORC_SHIELD1, "Upgrade shield", 0, 0, R, W2_UPGRADE_ORC_SHIELD1, 1, {0}, 0, {MT_ORC_BLACKSMITH}, 1 },
    { 22, W2_UI_ORC_SHIELD2, "Upgrade shield", 0, 0, R, W2_UPGRADE_ORC_SHIELD2, 1, {0}, 0, {MT_ORC_BLACKSMITH}, 1 },
    /* Hall upgrades replace the maker in place (Wargus upgrade.lua dependencies). */
    { 23, W2_UI_KEEP, "Upgrade to keep", 0, 66, B, MT_KEEP, 0, {MT_HUMAN_BARRACKS}, 1, {MT_TOWN_HALL}, 1 },
    { 24, W2_UI_CASTLE, "Upgrade to castle", 0, 68, B, MT_CASTLE, 0,
      {MT_STABLES, MT_HUMAN_BLACKSMITH, MT_ELVEN_LUMBER_MILL}, 3, {MT_KEEP}, 1 },
    { 25, W2_UI_STRONGHOLD, "Upgrade to stronghold", 0, 67, B, MT_STRONGHOLD, 1, {MT_ORC_BARRACKS}, 1, {MT_GREAT_HALL}, 1 },
    { 26, W2_UI_FORTRESS, "Upgrade to fortress", 0, 69, B, MT_FORTRESS, 1,
      {MT_OGRE_MOUND, MT_ORC_BLACKSMITH, MT_TROLL_LUMBER_MILL}, 3, {MT_STRONGHOLD}, 1 },
    /* Structures a worker builds (Wargus upgrade.lua dependencies; a keep
     * or castle satisfies "keep"). Icons are the MAINDAT button pictures. */
#define SITE(ui, label, icon, type, side, maker, ...) \
    { ui, ui, label, 0, icon, B, type, side, {__VA_ARGS__}, \
      sizeof((int[]){0, __VA_ARGS__}) / sizeof(int) - 1, {maker}, 1 }
    SITE(W2_UI_FARM, "Farm", 38, MT_FARM, 0, MT_PEASANT, 0),
    SITE(W2_UI_PIG_FARM, "Pig farm", 39, MT_PIG_FARM, 1, MT_PEON, 0),
    SITE(W2_UI_HUMAN_BARRACKS, "Barracks", 42, MT_HUMAN_BARRACKS, 0, MT_PEASANT, 0),
    SITE(W2_UI_ORC_BARRACKS, "Barracks", 43, MT_ORC_BARRACKS, 1, MT_PEON, 0),
    SITE(W2_UI_TOWN_HALL, "Town hall", 40, MT_TOWN_HALL, 0, MT_PEASANT, 0),
    SITE(W2_UI_GREAT_HALL, "Great hall", 41, MT_GREAT_HALL, 1, MT_PEON, 0),
    SITE(W2_UI_ELVEN_LUMBER_MILL, "Lumber mill", 44, MT_ELVEN_LUMBER_MILL, 0, MT_PEASANT, 0),
    SITE(W2_UI_TROLL_LUMBER_MILL, "Lumber mill", 45, MT_TROLL_LUMBER_MILL, 1, MT_PEON, 0),
    SITE(W2_UI_HUMAN_BLACKSMITH, "Blacksmith", 46, MT_HUMAN_BLACKSMITH, 0, MT_PEASANT, 0),
    SITE(W2_UI_ORC_BLACKSMITH, "Blacksmith", 47, MT_ORC_BLACKSMITH, 1, MT_PEON, 0),
    SITE(W2_UI_HUMAN_WATCH_TOWER, "Scout tower", 60, MT_HUMAN_WATCH_TOWER, 0, MT_PEASANT, 0),
    SITE(W2_UI_ORC_WATCH_TOWER, "Scout tower", 61, MT_ORC_WATCH_TOWER, 1, MT_PEON, 0),
    SITE(W2_UI_HUMAN_SHIPYARD, "Shipyard", 48, MT_HUMAN_SHIPYARD, 0, MT_PEASANT, MT_ELVEN_LUMBER_MILL),
    SITE(W2_UI_ORC_SHIPYARD, "Shipyard", 49, MT_ORC_SHIPYARD, 1, MT_PEON, MT_TROLL_LUMBER_MILL),
    SITE(W2_UI_HUMAN_FOUNDRY, "Foundry", 52, MT_HUMAN_FOUNDRY, 0, MT_PEASANT, MT_HUMAN_SHIPYARD),
    SITE(W2_UI_ORC_FOUNDRY, "Foundry", 53, MT_ORC_FOUNDRY, 1, MT_PEON, MT_ORC_SHIPYARD),
    SITE(W2_UI_HUMAN_REFINERY, "Refinery", 50, MT_HUMAN_REFINERY, 0, MT_PEASANT, MT_HUMAN_SHIPYARD),
    SITE(W2_UI_ORC_REFINERY, "Refinery", 51, MT_ORC_REFINERY, 1, MT_PEON, MT_ORC_SHIPYARD),
    SITE(W2_UI_INVENTOR, "Gnomish inventor", 58, MT_INVENTOR, 0, MT_PEASANT, MT_KEEP),
    SITE(W2_UI_ALCHEMIST, "Goblin alchemist", 59, MT_ALCHEMIST, 1, MT_PEON, MT_STRONGHOLD),
    SITE(W2_UI_STABLES, "Stables", 56, MT_STABLES, 0, MT_PEASANT, MT_KEEP),
    SITE(W2_UI_OGRE_MOUND, "Ogre mound", 57, MT_OGRE_MOUND, 1, MT_PEON, MT_STRONGHOLD),
    SITE(W2_UI_MAGE_TOWER, "Mage tower", 64, MT_MAGE_TOWER, 0, MT_PEASANT, MT_CASTLE),
    SITE(W2_UI_TEMPLE_OF_THE_DAMNED, "Temple of the damned", 65, MT_TEMPLE_OF_THE_DAMNED, 1, MT_PEON, MT_FORTRESS),
    SITE(W2_UI_CHURCH, "Church", 62, MT_CHURCH, 0, MT_PEASANT, MT_CASTLE),
    SITE(W2_UI_ALTAR_OF_STORMS, "Altar of storms", 63, MT_ALTAR_OF_STORMS, 1, MT_PEON, MT_FORTRESS),
    SITE(W2_UI_GRYPHON_AVIARY, "Gryphon aviary", 72, MT_GRYPHON_AVIARY, 0, MT_PEASANT, MT_CASTLE),
    SITE(W2_UI_DRAGON_ROOST, "Dragon roost", 73, MT_DRAGON_ROOST, 1, MT_PEON, MT_FORTRESS),
#undef SITE
};
#undef U
#undef R
#undef B

/* The pinned Wargus upgrade.lua rows: time and costs, the modifier each
 * applies, and the types it applies to. Icons are the level researched. */
static const w2_upgrade_t W2_UPGRADES[W2_UPGRADE_COUNT] = {
    [W2_UPGRADE_SWORD1] = { "upgrade-sword1", 117, 200, 800, 0, 0, false, 1, 2,
        MT_HUMAN_BLACKSMITH, {MT_FOOTMAN, MT_KNIGHT, MT_PALADIN, MT_DEMOLITION_SQUAD, MT_DANATH, MT_LOTHAR, MT_UTHER_LIGHTBRINGER, MT_TURALYON, MT_ATTACK_PEASANT} },
    [W2_UPGRADE_SWORD2] = { "upgrade-sword2", 118, 250, 2400, 0, 0, false, 2, 2,
        MT_HUMAN_BLACKSMITH, {MT_FOOTMAN, MT_KNIGHT, MT_PALADIN, MT_DEMOLITION_SQUAD, MT_DANATH, MT_LOTHAR, MT_UTHER_LIGHTBRINGER, MT_TURALYON, MT_ATTACK_PEASANT} },
    [W2_UPGRADE_AXE1] = { "upgrade-battle-axe1", 120, 200, 500, 100, 0, false, 1, 2,
        MT_ORC_BLACKSMITH, {MT_GRUNT, MT_OGRE, MT_OGRE_MAGE, MT_GOBLIN_SAPPERS, MT_KARGATH_BLADEFIST, MT_GROM_HELLSCREAM, MT_DENTARG, MT_CHOGALL} },
    [W2_UPGRADE_AXE2] = { "upgrade-battle-axe2", 121, 250, 1500, 300, 0, false, 2, 2,
        MT_ORC_BLACKSMITH, {MT_GRUNT, MT_OGRE, MT_OGRE_MAGE, MT_GOBLIN_SAPPERS, MT_KARGATH_BLADEFIST, MT_GROM_HELLSCREAM, MT_DENTARG, MT_CHOGALL} },
    [W2_UPGRADE_ARROW1] = { "upgrade-arrow1", 125, 200, 300, 300, 0, false, 1, 1,
        MT_ELVEN_LUMBER_MILL, {MT_ARCHER, MT_RANGER, MT_ALLERIA} },
    [W2_UPGRADE_ARROW2] = { "upgrade-arrow2", 126, 250, 900, 500, 0, false, 2, 1,
        MT_ELVEN_LUMBER_MILL, {MT_ARCHER, MT_RANGER, MT_ALLERIA} },
    [W2_UPGRADE_THROWING_AXE1] = { "upgrade-throwing-axe1", 128, 200, 300, 300, 0, false, 1, 1,
        MT_TROLL_LUMBER_MILL, {MT_AXETHROWER, MT_BERSERKER, MT_ZULJIN} },
    [W2_UPGRADE_THROWING_AXE2] = { "upgrade-throwing-axe2", 129, 250, 900, 500, 0, false, 2, 1,
        MT_TROLL_LUMBER_MILL, {MT_AXETHROWER, MT_BERSERKER, MT_ZULJIN} },
    [W2_UPGRADE_HUMAN_SHIELD1] = { "upgrade-human-shield1", 165, 200, 300, 300, 0, true, 1, 2,
        MT_HUMAN_BLACKSMITH, {MT_FOOTMAN, MT_KNIGHT, MT_PALADIN, MT_DEMOLITION_SQUAD, MT_DANATH, MT_LOTHAR, MT_UTHER_LIGHTBRINGER, MT_TURALYON, MT_ATTACK_PEASANT} },
    [W2_UPGRADE_HUMAN_SHIELD2] = { "upgrade-human-shield2", 166, 250, 900, 500, 0, true, 2, 2,
        MT_HUMAN_BLACKSMITH, {MT_FOOTMAN, MT_KNIGHT, MT_PALADIN, MT_DEMOLITION_SQUAD, MT_DANATH, MT_LOTHAR, MT_UTHER_LIGHTBRINGER, MT_TURALYON, MT_ATTACK_PEASANT} },
    [W2_UPGRADE_ORC_SHIELD1] = { "upgrade-orc-shield1", 168, 200, 300, 300, 0, true, 1, 2,
        MT_ORC_BLACKSMITH, {MT_GRUNT, MT_OGRE, MT_OGRE_MAGE, MT_GOBLIN_SAPPERS, MT_KARGATH_BLADEFIST, MT_GROM_HELLSCREAM, MT_DENTARG, MT_CHOGALL} },
    [W2_UPGRADE_ORC_SHIELD2] = { "upgrade-orc-shield2", 169, 250, 900, 500, 0, true, 2, 2,
        MT_ORC_BLACKSMITH, {MT_GRUNT, MT_OGRE, MT_OGRE_MAGE, MT_GOBLIN_SAPPERS, MT_KARGATH_BLADEFIST, MT_GROM_HELLSCREAM, MT_DENTARG, MT_CHOGALL} },
#define DAMAGE(id, name, icon, time, gold, wood, oil, armor, tier, bonus, maker, ...) \
    [id] = {name, icon, time, gold, wood, oil, armor, tier, bonus, maker, {__VA_ARGS__}}
    DAMAGE(W2_UPGRADE_BALLISTA1, "upgrade-ballista1", 140, 250, 1500, 0, 0, false, 1, 15, MT_HUMAN_BLACKSMITH, MT_BALLISTA),
    DAMAGE(W2_UPGRADE_BALLISTA2, "upgrade-ballista2", 141, 250, 4000, 0, 0, false, 2, 15, MT_HUMAN_BLACKSMITH, MT_BALLISTA),
    DAMAGE(W2_UPGRADE_CATAPULT1, "upgrade-catapult1", 138, 250, 1500, 0, 0, false, 1, 15, MT_ORC_BLACKSMITH, MT_CATAPULT),
    DAMAGE(W2_UPGRADE_CATAPULT2, "upgrade-catapult2", 139, 250, 4000, 0, 0, false, 2, 15, MT_ORC_BLACKSMITH, MT_CATAPULT),
    DAMAGE(W2_UPGRADE_HUMAN_CANNON1, "upgrade-human-ship-cannon1", 145, 200, 700, 100, 1000, false, 1, 5, MT_HUMAN_FOUNDRY, MT_HUMAN_DESTROYER, MT_BATTLESHIP, MT_GNOMISH_SUBMARINE),
    DAMAGE(W2_UPGRADE_HUMAN_CANNON2, "upgrade-human-ship-cannon2", 146, 250, 2000, 250, 3000, false, 2, 5, MT_HUMAN_FOUNDRY, MT_HUMAN_DESTROYER, MT_BATTLESHIP, MT_GNOMISH_SUBMARINE),
    DAMAGE(W2_UPGRADE_ORC_CANNON1, "upgrade-orc-ship-cannon1", 148, 200, 700, 100, 1000, false, 1, 5, MT_ORC_FOUNDRY, MT_ORC_DESTROYER, MT_OGRE_JUGGERNAUGHT, MT_GIANT_TURTLE),
    DAMAGE(W2_UPGRADE_ORC_CANNON2, "upgrade-orc-ship-cannon2", 149, 250, 2000, 250, 3000, false, 2, 5, MT_ORC_FOUNDRY, MT_ORC_DESTROYER, MT_OGRE_JUGGERNAUGHT, MT_GIANT_TURTLE),
    DAMAGE(W2_UPGRADE_HUMAN_SHIP_ARMOR1, "upgrade-human-ship-armor1", 154, 200, 500, 500, 0, true, 1, 5, MT_HUMAN_FOUNDRY, MT_HUMAN_DESTROYER, MT_BATTLESHIP, MT_HUMAN_TRANSPORT),
    DAMAGE(W2_UPGRADE_HUMAN_SHIP_ARMOR2, "upgrade-human-ship-armor2", 155, 250, 1500, 900, 0, true, 2, 5, MT_HUMAN_FOUNDRY, MT_HUMAN_DESTROYER, MT_BATTLESHIP, MT_HUMAN_TRANSPORT),
    DAMAGE(W2_UPGRADE_ORC_SHIP_ARMOR1, "upgrade-orc-ship-armor1", 151, 200, 500, 500, 0, true, 1, 5, MT_ORC_FOUNDRY, MT_ORC_DESTROYER, MT_OGRE_JUGGERNAUGHT, MT_ORC_TRANSPORT),
    DAMAGE(W2_UPGRADE_ORC_SHIP_ARMOR2, "upgrade-orc-ship-armor2", 152, 250, 1500, 900, 0, true, 2, 5, MT_ORC_FOUNDRY, MT_ORC_DESTROYER, MT_OGRE_JUGGERNAUGHT, MT_ORC_TRANSPORT),
#undef DAMAGE
    /* Zero bonus denotes a distinct technology, not a weapon/armor tier. */
#define TECH(id, name, icon, time, gold, maker, unit) \
    [id] = {name, icon, time, gold, 0, 0, false, 1, 0, maker, {unit}}
    TECH(W2_UPGRADE_RANGER, "upgrade-ranger", 6, 250, 1500, MT_ELVEN_LUMBER_MILL, MT_RANGER),
    TECH(W2_UPGRADE_BERSERKER, "upgrade-berserker", 7, 250, 1500, MT_TROLL_LUMBER_MILL, MT_BERSERKER),
    TECH(W2_UPGRADE_PALADIN, "upgrade-paladin", 10, 250, 1000, MT_CHURCH, MT_PALADIN),
    TECH(W2_UPGRADE_OGRE_MAGE, "upgrade-ogre-mage", 11, 250, 1000, MT_ALTAR_OF_STORMS, MT_OGRE_MAGE),
    TECH(W2_UPGRADE_LONGBOW, "upgrade-longbow", 132, 250, 2000, MT_ELVEN_LUMBER_MILL, MT_RANGER),
    TECH(W2_UPGRADE_LIGHT_AXES, "upgrade-light-axes", 135, 250, 2000, MT_TROLL_LUMBER_MILL, MT_BERSERKER),
    TECH(W2_UPGRADE_RANGER_SCOUTING, "upgrade-ranger-scouting", 133, 250, 1500, MT_ELVEN_LUMBER_MILL, MT_RANGER),
    TECH(W2_UPGRADE_BERSERKER_SCOUTING, "upgrade-berserker-scouting", 136, 250, 1500, MT_TROLL_LUMBER_MILL, MT_BERSERKER),
    TECH(W2_UPGRADE_MARKSMANSHIP, "upgrade-ranger-marksmanship", 134, 250, 2500, MT_ELVEN_LUMBER_MILL, MT_RANGER),
    TECH(W2_UPGRADE_REGENERATION, "upgrade-berserker-regeneration", 137, 250, 3000, MT_TROLL_LUMBER_MILL, MT_BERSERKER),
    TECH(W2_UPGRADE_HEALING, "upgrade-healing", 107, 200, 1000, MT_CHURCH, MT_PALADIN),
    TECH(W2_UPGRADE_EXORCISM, "upgrade-exorcism", 110, 200, 2000, MT_CHURCH, MT_PALADIN),
    TECH(W2_UPGRADE_FLAME_SHIELD, "upgrade-flame-shield", 100, 100, 1000, MT_MAGE_TOWER, MT_MAGE),
    TECH(W2_UPGRADE_SLOW, "upgrade-slow", 94, 100, 500, MT_MAGE_TOWER, MT_MAGE),
    TECH(W2_UPGRADE_INVISIBILITY, "upgrade-invisibility", 95, 200, 2500, MT_MAGE_TOWER, MT_MAGE),
    TECH(W2_UPGRADE_POLYMORPH, "upgrade-polymorph", 115, 200, 2000, MT_MAGE_TOWER, MT_MAGE),
    TECH(W2_UPGRADE_BLIZZARD, "upgrade-blizzard", 105, 200, 2000, MT_MAGE_TOWER, MT_MAGE),
    TECH(W2_UPGRADE_BLOODLUST, "upgrade-bloodlust", 112, 100, 1000, MT_ALTAR_OF_STORMS, MT_OGRE_MAGE),
    TECH(W2_UPGRADE_RUNES, "upgrade-runes", 97, 150, 1000, MT_ALTAR_OF_STORMS, MT_OGRE_MAGE),
    TECH(W2_UPGRADE_RAISE_DEAD, "upgrade-raise-dead", 114, 100, 1500, MT_TEMPLE_OF_THE_DAMNED, MT_DEATH_KNIGHT),
    TECH(W2_UPGRADE_WHIRLWIND, "upgrade-whirlwind", 104, 150, 1500, MT_TEMPLE_OF_THE_DAMNED, MT_DEATH_KNIGHT),
    TECH(W2_UPGRADE_HASTE, "upgrade-haste", 96, 100, 500, MT_TEMPLE_OF_THE_DAMNED, MT_DEATH_KNIGHT),
    TECH(W2_UPGRADE_UNHOLY_ARMOR, "upgrade-unholy-armor", 98, 200, 2500, MT_TEMPLE_OF_THE_DAMNED, MT_DEATH_KNIGHT),
    TECH(W2_UPGRADE_DEATH_AND_DECAY, "upgrade-death-and-decay", 108, 200, 2000, MT_TEMPLE_OF_THE_DAMNED, MT_DEATH_KNIGHT),
#undef TECH
};

bool W2_HasResearch(int owner, int id) {
    return owner >= 0 && owner < 8 && id > 0 && id < W2_UPGRADE_COUNT &&
        (level.w2_research[owner] & (UINT64_C(1) << id));
}

const w2_upgrade_t *W2_Upgrade(int id) {
    return id > 0 && id < W2_UPGRADE_COUNT ? &W2_UPGRADES[id] : NULL;
}

/* The tier the owner has of this upgrade's line, read off its first type. */
int W2_UpgradeLevel(int owner, const w2_upgrade_t *upgrade) {
    if (!upgrade || owner < 0 || owner >= 8 || !upgrade->units[0]) return 0;
    if (!upgrade->bonus) return W2_HasResearch(owner, (int)(upgrade - W2_UPGRADES));
    return upgrade->armor ? level.upgrades[upgrade->units[0]][owner].armor :
                            level.upgrades[upgrade->units[0]][owner].weapon;
}

void W2_ApplyUpgrade(int owner, int id) {
    const w2_upgrade_t *upgrade = W2_Upgrade(id);
    if (!upgrade || owner < 0 || owner >= 8) return;
    level.w2_research[owner] |= UINT64_C(1) << id;
    if (!upgrade->bonus) {
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next)
            if (th->function == P_MobjThinker) W2_UpgradeUnit((mobj_t *)th);
        return;
    }
    for (size_t i = 0; i < sizeof(upgrade->units) / sizeof(*upgrade->units) && upgrade->units[i]; ++i) {
        uint8_t *tier = upgrade->armor ? &level.upgrades[upgrade->units[i]][owner].armor :
                                         &level.upgrades[upgrade->units[i]][owner].weapon;
        if (*tier < upgrade->tier) *tier = (uint8_t)upgrade->tier;
    }
}

/* Stratagus upgrade-to: the building becomes the new type where it stands,
 * keeping its share of hit points. */
bool W2_TransformUnit(mobj_t *unit, uint16_t type) {
    if (!unit || unit->remove || type == 0 || type >= NUMMOBJTYPES || !mobjinfo[type].name) return false;
    const mobjinfo_t *to = &mobjinfo[type];
    int old_max = unit->max_hp > 0 ? unit->max_hp : 1;
    int hp = unit->hp > 0 ? unit->hp : 1;
    unit->type_id = type;
    unit->info = &actor_types[type - 1];
    unit->speed = unit->info->speed;
    if (!unit->w2.mana && to->w2.mana.max) unit->w2.mana = to->w2.mana.initial;
    unit->traits = (unit->traits & (MF_SELECTED | MF_DONTDRAW)) | unit->info->traits;
    if (unit->w2.boarded) {
        unit->traits &= ~(MF_RENDERABLE | MF_SELECTABLE | MF_MOBILE);
        unit->traits |= MF_NOBLOCKMAP;
    }
    unit->max_hp = to->spawnhealth > 0 ? to->spawnhealth : 1;
    unit->hp = hp * unit->max_hp / old_max;
    if (unit->hp < 1) unit->hp = 1;
    snprintf(unit->core.sprite_name, sizeof(unit->core.sprite_name), "%s", to->name);
    unit->core.state_id = 0;
    P_SetMobjState(unit, to->spawnstate);
    W2_EnsureUnitSprite(type - 1);
    return true;
}

void W2_UpgradeUnit(mobj_t *unit) {
    if (!unit || unit->remove || unit->hp <= 0) return;
    static const struct { int research; uint16_t from, to; } conversions[] = {
        {W2_UPGRADE_RANGER, MT_ARCHER, MT_RANGER}, {W2_UPGRADE_BERSERKER, MT_AXETHROWER, MT_BERSERKER},
        {W2_UPGRADE_PALADIN, MT_KNIGHT, MT_PALADIN}, {W2_UPGRADE_OGRE_MAGE, MT_OGRE, MT_OGRE_MAGE},
    };
    for (size_t i = 0; i < sizeof(conversions) / sizeof(*conversions); ++i)
        if (unit->type_id == conversions[i].from && W2_HasResearch(unit->owner, conversions[i].research))
            W2_TransformUnit(unit, conversions[i].to);
}

float W2_AttackRange(const mobj_t *unit) {
    if (!unit || !unit->info) return 0;
    bool human = unit->type_id == MT_ARCHER || unit->type_id == MT_RANGER;
    bool orc = unit->type_id == MT_AXETHROWER || unit->type_id == MT_BERSERKER;
    return unit->info->attack.range +
        ((human && W2_HasResearch(unit->owner, W2_UPGRADE_LONGBOW)) ||
         (orc && W2_HasResearch(unit->owner, W2_UPGRADE_LIGHT_AXES)));
}

int W2_SightRange(const mobj_t *unit) {
    if (unit->type_id == MT_W2_EFFECT && unit->w2.cast.spell == W2_SPELL_VISION) return 12;
    int radius = mobjinfo[unit->type_id].w2.sight;
    if (unit->type_id == MT_ARCHER || unit->type_id == MT_RANGER) {
        radius += W2_HasResearch(unit->owner, W2_UPGRADE_LONGBOW);
        radius += 3 * W2_HasResearch(unit->owner, W2_UPGRADE_RANGER_SCOUTING);
    }
    if (unit->type_id == MT_AXETHROWER || unit->type_id == MT_BERSERKER)
        radius += 3 * W2_HasResearch(unit->owner, W2_UPGRADE_BERSERKER_SCOUTING);
    return radius;
}

static int product_count(void) {
    return w2_product_count;
}

static const w2_upgrade_t *product_upgrade(const StaticProductDefinition *product) {
    return product && product->product_class == RTS_PRODUCT_UPGRADE ?
        W2_Upgrade(product->product_type) : NULL;
}

/* A structure a worker puts up, as opposed to a hall that upgrades in place. */
static bool product_is_site(const StaticProductDefinition *product) {
    return product && product->product_class == RTS_PRODUCT_BUILDING &&
        product->maker_count > 0 && (actor_types[product->makers[0] - 1].traits & MF_MOBILE);
}

/* A finished structure of the owner that counts as `type` (tier rules). */
static bool owner_has(int owner, uint16_t type) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (unit->owner != owner || unit->remove || unit->hp <= 0 || W2_UnderConstruction(unit)) continue;
        if (W2_CountsAs(unit->type_id, type)) return true;
    }
    return false;
}

void w2_init_products(void) {
    w2_product_count = W2_UI_COUNT - 1;
    for (int id = W2_UPGRADE_BALLISTA1; id < W2_UPGRADE_COUNT; ++id) {
        const w2_upgrade_t *u = W2_Upgrade(id);
        W2_PRODUCTS[w2_product_count++] = (StaticProductDefinition){
            .ui_id = 100 + id, .label = u->name, .product_class = RTS_PRODUCT_UPGRADE,
            .product_type = id, .makers = {u->maker}, .maker_count = 1};
    }
    static const struct { uint16_t unit, maker, requires; int icon; } extra[] = {
        {MT_MAGE, MT_MAGE_TOWER, 0, 14}, {MT_DEATH_KNIGHT, MT_TEMPLE_OF_THE_DAMNED, 0, 15},
        {MT_DEMOLITION_SQUAD, MT_INVENTOR, 0, 12}, {MT_GOBLIN_SAPPERS, MT_ALCHEMIST, 0, 13},
        {MT_FLYING_MACHINE, MT_INVENTOR, MT_ELVEN_LUMBER_MILL, 28}, {MT_ZEPPELIN, MT_ALCHEMIST, MT_TROLL_LUMBER_MILL, 29},
        {MT_GRYPHON_RIDER, MT_GRYPHON_AVIARY, 0, 30}, {MT_DRAGON, MT_DRAGON_ROOST, 0, 31},
        {MT_HUMAN_OIL_TANKER, MT_HUMAN_SHIPYARD, 0, 18}, {MT_ORC_OIL_TANKER, MT_ORC_SHIPYARD, 0, 19},
        {MT_HUMAN_TRANSPORT, MT_HUMAN_SHIPYARD, MT_HUMAN_FOUNDRY, 20}, {MT_ORC_TRANSPORT, MT_ORC_SHIPYARD, MT_ORC_FOUNDRY, 21},
        {MT_HUMAN_DESTROYER, MT_HUMAN_SHIPYARD, 0, 22}, {MT_ORC_DESTROYER, MT_ORC_SHIPYARD, 0, 23},
        {MT_BATTLESHIP, MT_HUMAN_SHIPYARD, MT_HUMAN_FOUNDRY, 24}, {MT_OGRE_JUGGERNAUGHT, MT_ORC_SHIPYARD, MT_ORC_FOUNDRY, 25},
        {MT_GNOMISH_SUBMARINE, MT_HUMAN_SHIPYARD, MT_INVENTOR, 26}, {MT_GIANT_TURTLE, MT_ORC_SHIPYARD, MT_ALCHEMIST, 27},
        {MT_HUMAN_GUARD_TOWER, MT_HUMAN_WATCH_TOWER, MT_ELVEN_LUMBER_MILL, 75},
        {MT_ORC_GUARD_TOWER, MT_ORC_WATCH_TOWER, MT_TROLL_LUMBER_MILL, 77},
        {MT_HUMAN_CANNON_TOWER, MT_HUMAN_WATCH_TOWER, MT_HUMAN_BLACKSMITH, 76},
        {MT_ORC_CANNON_TOWER, MT_ORC_WATCH_TOWER, MT_ORC_BLACKSMITH, 78},
        {MT_HUMAN_OIL_PLATFORM, MT_HUMAN_OIL_TANKER, 0, 54},
        {MT_ORC_OIL_PLATFORM, MT_ORC_OIL_TANKER, 0, 55},
    };
    for (size_t i = 0; i < sizeof(extra) / sizeof(*extra); ++i) {
        int type = extra[i].unit;
        W2_PRODUCTS[w2_product_count++] = (StaticProductDefinition){
            .ui_id = 200 + type, .label = mobjinfo[type].label, .icon_frame = extra[i].icon,
            .product_class = (mobjinfo[type].w2.flags & W2_STRUCTURE) ? RTS_PRODUCT_BUILDING : RTS_PRODUCT_UNIT,
            .product_type = type, .makers = {extra[i].maker}, .maker_count = 1,
            .prerequisites = {extra[i].requires}, .prerequisite_count = extra[i].requires ? 1 : 0};
    }
    for (int i = 0; i < product_count(); ++i) {
        StaticProductDefinition *product = &W2_PRODUCTS[i];
        /* The SITE rows pad their prerequisite list with zero. */
        while (product->prerequisite_count > 0 && !product->prerequisites[product->prerequisite_count - 1])
            --product->prerequisite_count;
        const w2_upgrade_t *upgrade = product_upgrade(product);
        if (upgrade) {
            product->cost = upgrade->gold;
            product->icon_frame = upgrade->icon;
        } else {
            product->cost = mobjinfo[product->product_type].w2.costs.resources[0];
        }
    }
}

int G_ModelGetProducts(const RtsGameModel *model, int owner,
                       StaticProductDefinition *out, int max_products) {
    (void)model; (void)owner;
    if (!out || max_products <= 0) return 0;
    int count = product_count();
    if (count > max_products) count = max_products;
    memcpy(out, W2_PRODUCTS, (size_t)count * sizeof(StaticProductDefinition));
    return count;
}

const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id) {
    (void)model;
    for (int i = 0; i < product_count(); ++i)
        if (W2_PRODUCTS[i].ui_id == ui_id) return &W2_PRODUCTS[i];
    return NULL;
}

const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class,
                                                         int product_type) {
    (void)model;
    for (int i = 0; i < product_count(); ++i)
        if ((int)W2_PRODUCTS[i].product_class == product_class &&
            W2_PRODUCTS[i].product_type == product_type)
            return &W2_PRODUCTS[i];
    return NULL;
}

bool G_ModelProductAvailable(const RtsGameModel *model, int owner,
                             const StaticProductDefinition *product) {
    if (!product) return false;
    /* Research goes one tier at a time and never repeats. */
    const w2_upgrade_t *upgrade = product_upgrade(product);
    if (upgrade && W2_UpgradeLevel(owner, upgrade) != upgrade->tier - 1) return false;
    if (upgrade) {
        int id = product->product_type;
        if ((id == W2_UPGRADE_RANGER || id == W2_UPGRADE_LONGBOW || id == W2_UPGRADE_RANGER_SCOUTING || id == W2_UPGRADE_MARKSMANSHIP) &&
            (!owner_has(owner, MT_KEEP) || (id != W2_UPGRADE_RANGER && !W2_HasResearch(owner, W2_UPGRADE_RANGER)))) return false;
        if ((id == W2_UPGRADE_BERSERKER || id == W2_UPGRADE_LIGHT_AXES || id == W2_UPGRADE_BERSERKER_SCOUTING || id == W2_UPGRADE_REGENERATION) &&
            (!owner_has(owner, MT_STRONGHOLD) || (id != W2_UPGRADE_BERSERKER && !W2_HasResearch(owner, W2_UPGRADE_BERSERKER)))) return false;
        if ((id == W2_UPGRADE_HEALING || id == W2_UPGRADE_EXORCISM) && !W2_HasResearch(owner, W2_UPGRADE_PALADIN)) return false;
        if ((id == W2_UPGRADE_BLOODLUST || id == W2_UPGRADE_RUNES) && !W2_HasResearch(owner, W2_UPGRADE_OGRE_MAGE)) return false;
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            const mobj_t *maker = (const mobj_t *)th;
            if (th->function == P_MobjThinker && maker->owner == owner && maker->hp > 0 && !maker->remove &&
                maker->production && maker->production->queue_count &&
                maker->production->product_class == RTS_PRODUCT_UPGRADE &&
                maker->production->product_type == id) return false;
        }
    }
    for (int i = 0; i < product->prerequisite_count; ++i)
        if (!owner_has(owner, (uint16_t)product->prerequisites[i])) return false;
    if (product->maker_count <= 0) return true;
    for (int i = 0; i < product->maker_count; ++i)
        if (G_ModelHasActorType(model, owner, (uint16_t)product->makers[i]))
            return true;
    return false;
}

/* Research is filed under the first type it improves. */
uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *product) {
    const w2_upgrade_t *upgrade = product_upgrade(product);
    if (upgrade) return upgrade->units[0];
    return product ? (uint16_t)product->product_type : 0;
}

int G_ModelBuildingFrameForProduct(const StaticProductDefinition *product) {
    (void)product;
    return 0;
}

int G_ModelBuildingStateForProduct(const gameinfo_t *info,
                                  const StaticProductDefinition *product) {
    (void)info; (void)product;
    return -1;
}

int G_ModelProductTrainingTimeMs(const StaticProductDefinition *product) {
    const w2_upgrade_t *upgrade = product_upgrade(product);
    if (upgrade) return w2_cost_ms(upgrade->time);
    if (!product) return 0;
    return w2_cost_ms(mobjinfo[product->product_type].w2.costs.time);
}

/* Units leave the engine's queue as new actors. Research and hall upgrades
 * finish here instead: the effect lands on the player or the building, and
 * the order is taken off the queue. */
bool G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                   const StaticProductDefinition *product,
                                   uint16_t actor_id) {
    (void)model; (void)actor_id;
    if (!producer || !product) return false;
    if (product->product_class == RTS_PRODUCT_UNIT) {
        int used = 0, supply = 0, units = 0;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *unit = (mobj_t *)th;
            if (th->function != P_MobjThinker || unit->remove || unit->hp <= 0 ||
                unit->owner != producer->owner || unit->type_id > W2_TYPE_COUNT) continue;
            const w2_stats_t *stats = &mobjinfo[unit->type_id].w2;
            used += stats->food.demand;
            if (!W2_UnderConstruction(unit)) supply += stats->food.supply;
            if (!(stats->flags & W2_STRUCTURE)) ++units;
        }
        /* Stratagus action_train checks supply when releasing a completed
         * unit. Keep the paid queue waiting until a farm becomes available. */
        bool blocked = units >= 200 || used + mobjinfo[actor_id].w2.food.demand > supply;
        if (blocked && producer->production) producer->production->time_left_ms = 0;
        return blocked;
    }
    if (product->product_class == RTS_PRODUCT_UPGRADE)
        W2_ApplyUpgrade(producer->owner, product->product_type);
    else
        W2_TransformUnit(producer, (uint16_t)product->product_type);
    S_Bark(&producer, 1, SE_RESEARCH_COMPLETE, false);
    production_t *production = producer->production;
    if (production) {
        if (--production->queue_count > 0) production->time_left_ms = production->time_ms;
        else P_FreeMobjProduction(producer);
    }
    return true;
}

bool G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *model, const mobj_t *producer,
                                     const StaticProductDefinition *product,
                                     const mobj_t *new_unit,
                                     float *out_gx, float *out_gy) {
    (void)model; (void)producer; (void)product; (void)new_unit; (void)out_gx; (void)out_gy;
    return false;
}

static void append_ui_script(char *dst, size_t dst_size, const char *fmt, ...) {
    if (!dst || dst_size == 0) return;
    size_t len = strlen(dst);
    if (len >= dst_size - 1) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(dst + len, dst_size - len, fmt, args);
    va_end(args);
}

void G_ModelBuildUIScript(const RtsGameModel *model,
                          const RtsRenderSnapshot *snapshot,
                          char *dst, size_t dst_size) {
    if (!model || !snapshot || !dst || dst_size == 0) return;
    dst[0] = '\0';
    append_ui_script(dst, dst_size, "ui warcraft-2 1\n");
    append_ui_script(dst, dst_size, "x 630 y 3 text \"Gold %d\"\n",
                     snapshot->player_resources[consoleplayer][0]);
}

bool G_PlayerBuildProduct(mobj_t *producer, const StaticProductDefinition *product) {
    if (!producer || !product || producer->owner >= 8) return false;
    int lumber = W2_ProductLumber(product), oil = W2_ProductOil(product);
    int *stock = level.player_resources[producer->owner];
    if (stock[1] < lumber || stock[2] < oil || !G_QueueProduct(producer, product)) return false;
    stock[1] -= lumber;
    stock[2] -= oil;
    return true;
}

bool W2_CancelProduction(mobj_t *producer) {
    production_t *queue = producer ? producer->production : NULL;
    if (!queue || queue->queue_count <= 0 || producer->owner >= 8) return false;
    const StaticProductDefinition *product = G_ModelProductByClassType(NULL, queue->product_class, queue->product_type);
    if (!product) return false;
    int *stock = level.player_resources[producer->owner];
    /* Wargus refund factors for train, research and upgrade-to are 100%. */
    stock[0] += product->cost;
    stock[1] += W2_ProductLumber(product);
    stock[2] += W2_ProductOil(product);
    if (--queue->queue_count == 0) P_FreeMobjProduction(producer);
    else queue->time_left_ms = queue->time_ms;
    return true;
}

int W2_ProductLumber(const StaticProductDefinition *product) {
    const w2_upgrade_t *upgrade = product_upgrade(product);
    if (upgrade) return upgrade->lumber;
    return product ? mobjinfo[product->product_type].w2.costs.resources[1] : 0;
}

int W2_ProductOil(const StaticProductDefinition *product) {
    const w2_upgrade_t *upgrade = product_upgrade(product);
    if (upgrade) return upgrade->oil;
    return product ? mobjinfo[product->product_type].w2.costs.resources[2] : 0;
}

/* A building researches or upgrades one thing at a time, and not while it
 * is training. Workers never queue structures: those go through a
 * construction order with a site (W2_ConstructOrder). */
bool G_ModelProducerHasTech(const mobj_t *producer, const StaticProductDefinition *product) {
    if (!producer || !product) return false;
    if (product_is_site(product)) return false;
    if (product->product_class == RTS_PRODUCT_UNIT) return true;
    return !producer->production || producer->production->queue_count <= 0;
}

int G_ModelRadarLevel(int owner) {
    (void)owner;
    return 2;
}

/* ── computer player ──────────────────────────────────────────────────── */

static bool orc_owner(int owner) {
    const w2_pud_t *pud = level.native_data;
    if (pud && owner >= 0 && owner < 16) return pud->sides[owner] == 1;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (unit->owner != owner || unit->remove || unit->hp <= 0) continue;
        if (unit->type_id == MT_PEON || unit->type_id == MT_GREAT_HALL || unit->type_id == MT_GRUNT) return true;
        if (unit->type_id == MT_PEASANT || unit->type_id == MT_TOWN_HALL || unit->type_id == MT_FOOTMAN) return false;
    }
    return false;
}

/* The console player is the human; in a network game every joined slot is.
 * Only the PUD's computer and player slots field an opponent. */
static int w2_ai_level(const level_t *map, int owner) {
    if (owner < 0 || owner >= 8) return AI_LEVEL_NONE;
    if (netgame ? D_PlayerIsHuman(owner) : owner == consoleplayer) return AI_LEVEL_NONE;
    const w2_pud_t *pud = map ? map->native_data : NULL;
    if (pud && pud->owners[owner] != 4 && pud->owners[owner] != 5) return AI_LEVEL_NONE;
    return AI_LEVEL_NORMAL;
}

/* Wargus land_attack.lua in goal-ladder form: workers, farms and a
 * barracks, then soldiers, a mill and smithy with the first research,
 * towers, the keep, stables and knights. Human ids; orc ids are one more. */
static const struct { int product, count; } w2_ladder[] = {
    { W2_UI_TOWN_HALL, 1 }, { 3, 5 }, { W2_UI_FARM, 2 }, { W2_UI_HUMAN_BARRACKS, 1 },
    { 3, 8 }, { 1, 3 }, { W2_UI_FARM, 4 }, { W2_UI_ELVEN_LUMBER_MILL, 1 },
    { 1, 6 }, { 5, 3 }, { W2_UI_HUMAN_BLACKSMITH, 1 }, { W2_UI_FARM, 6 }, { 3, 10 },
    { W2_UI_SWORD1, 1 }, { W2_UI_HUMAN_WATCH_TOWER, 1 }, { W2_UI_HUMAN_SHIELD1, 1 },
    { 1, 10 }, { 5, 6 }, { W2_UI_KEEP, 1 }, { W2_UI_STABLES, 1 }, { W2_UI_FARM, 8 },
    { 9, 4 }, { W2_UI_HUMAN_BARRACKS, 2 }, { 1, 14 }, { 5, 8 }, { 9, 8 }, { 3, 12 },
};

static int orc_twin(int ui) {
    if (ui == W2_UI_SWORD1) return W2_UI_AXE1;
    if (ui == W2_UI_HUMAN_SHIELD1) return W2_UI_ORC_SHIELD1;
    if (ui == W2_UI_KEEP) return W2_UI_STRONGHOLD;
    return ui + 1;
}

static bool w2_ai_plan(const level_t *map, int owner, int level, AiPlan *out) {
    (void)map; (void)level;
    bool orc = orc_owner(owner);
    out->wave_interval_ms = 60000;
    out->wave_min_size = 4;
    out->wave_max_size = 12;
    for (unsigned i = 0; i < sizeof(w2_ladder) / sizeof(*w2_ladder); ++i)
        P_AiPlanAdd(out, orc ? orc_twin(w2_ladder[i].product) : w2_ladder[i].product, w2_ladder[i].count);
    return true;
}

static int builders_bound_for(int owner, uint16_t type) {
    int count = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (unit->owner == owner && !unit->remove && unit->hp > 0 &&
            unit->w2.build_phase == W2_BUILD_TO_SITE && unit->w2.build_type == type) ++count;
    }
    return count;
}

static int queued_product(int owner, const StaticProductDefinition *product) {
    int count = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (unit->owner != owner || unit->remove || unit->hp <= 0 || !unit->production) continue;
        if (unit->production->product_class == (uint8_t)product->product_class &&
            unit->production->product_type == product->product_type) count += unit->production->queue_count;
    }
    return count;
}

/* Alive plus on the way: research by tier, structures by tier rules with
 * sites and walking builders, units with their training queues. */
static int w2_ai_owned(int owner, int ui) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui);
    if (!product) return 0;
    const w2_upgrade_t *upgrade = product_upgrade(product);
    if (upgrade) return (W2_UpgradeLevel(owner, upgrade) >= upgrade->tier ? 1 : 0) + queued_product(owner, product);
    if (product->product_class == RTS_PRODUCT_UNIT) return G_CountPlannedActors(owner, (uint16_t)product->product_type);
    uint16_t type = (uint16_t)product->product_type;
    int count = queued_product(owner, product) + builders_bound_for(owner, type);
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (unit->owner == owner && !unit->remove && unit->hp > 0 && W2_CountsAs(unit->type_id, type)) ++count;
    }
    return count;
}

/* Builders on their way pay on arrival; the computer keeps their price
 * aside so a peon never finds the gold spent when it gets there. */
static bool affordable(int owner, const StaticProductDefinition *product) {
    int committed[3] = {0, 0, 0};
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (unit->owner != owner || unit->remove || unit->hp <= 0 || unit->w2.build_phase != W2_BUILD_TO_SITE) continue;
        for (int r = 0; r < 3; ++r) committed[r] += mobjinfo[unit->w2.build_type].w2.costs.resources[r];
    }
    const int *stock = level.player_resources[owner];
    return stock[0] - committed[0] >= product->cost && stock[1] - committed[1] >= W2_ProductLumber(product) &&
           stock[2] - committed[2] >= W2_ProductOil(product);
}

static int w2_ai_can_purchase(const level_t *map, int owner, int ui) {
    (void)map;
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui);
    if (!product || owner < 0 || owner >= 8 || !G_ModelProductAvailable(NULL, owner, product)) return AI_BUY_BLOCKED;
    if (product_is_site(product)) {
        if (!G_ModelHasActorType(NULL, owner, (uint16_t)product->makers[0])) return AI_BUY_BLOCKED;
    } else if (!G_FindProducer(owner, product)) {
        return AI_BUY_BLOCKED;
    }
    return affordable(owner, product) ? AI_BUY_OK : AI_BUY_NEED_CREDITS;
}

/* Idle workers first, then one chopping wood, never one inside a site. */
static mobj_t *pick_builder(int owner, uint16_t maker) {
    mobj_t *best = NULL;
    int best_rank = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (unit->owner != owner || unit->remove || unit->hp <= 0 || unit->type_id != maker) continue;
        if (unit->w2.build_phase != W2_BUILD_NONE || !(unit->traits & MF_MOBILE)) continue;
        int rank = unit->harvest.phase == HARVEST_PHASE_NONE && !P_HasMoveOrder(unit) ? 3 :
                   unit->harvest.resource_type == 1 ? 2 : 1;
        if (rank > best_rank) { best_rank = rank; best = unit; }
    }
    return best;
}

static bool w2_ai_purchase(level_t *map, int owner, int ui) {
    (void)map;
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui);
    if (!product) return false;
    if (!product_is_site(product)) {
        mobj_t *producer = G_FindProducer(owner, product);
        return producer && G_PlayerBuildProduct(producer, product);
    }
    uint16_t type = (uint16_t)product->product_type;
    ivec2_t cell;
    mobj_t *builder = pick_builder(owner, (uint16_t)product->makers[0]);
    return builder && W2_FindBuildSite(owner, type, &cell) && W2_ConstructOrder(builder, type, cell);
}

/* A worker with a building job is spoken for. */
static bool w2_ai_busy(const mobj_t *unit) {
    return unit && unit->w2.build_phase != W2_BUILD_NONE;
}

/* The depot the worker's trips will run from: its owner's nearest store
 * for the resource, or the worker itself without one. */
static fvec2_t depot_for(const mobj_t *unit, int resource) {
    fvec2_t at = fixed3_xy_to_fvec2(unit->core.position), best = at;
    float distance = 0;
    bool found = false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *base = (const mobj_t *)th;
        if (base->owner != unit->owner || base->remove || base->hp <= 0 || W2_UnderConstruction(base)) continue;
        if (base->type_id >= NUMMOBJTYPES || !(mobjinfo[base->type_id].w2.store_mask & (1 << resource))) continue;
        fvec2_t here = fixed3_xy_to_fvec2(base->core.position);
        float d = fvec2_distance_squared(at, here);
        if (!found || d < distance) { found = true; distance = d; best = here; }
    }
    return best;
}

/* Gold first; one worker in three takes lumber. The open deposit of the
 * wanted kind nearest the depot it will carry to, that the worker can
 * reach; the other kind otherwise. */
static bool send_to_resource(mobj_t *unit, int resource) {
    enum { TRIES = 6 };
    int best[TRIES];
    float dist[TRIES];
    int found = 0;
    fvec2_t from = depot_for(unit, resource);
    for (int i = 0; i < level.resource_vent_count; ++i) {
        const resourcevent_t *vent = &level.resource_vents[i];
        if (vent->resource_type != resource || !P_VentOpenTo(&level, vent, unit)) continue;
        float d = fvec2_distance_squared(from, vent->attachment);
        if (found == TRIES && d >= dist[TRIES - 1]) continue;
        int at = found < TRIES ? found++ : TRIES - 1;
        while (at > 0 && dist[at - 1] > d) { best[at] = best[at - 1]; dist[at] = dist[at - 1]; --at; }
        best[at] = i;
        dist[at] = d;
    }
    for (int i = 0; i < found; ++i)
        if (W2_HarvestOrder(unit, level.resource_vents[best[i]].attachment)) return true;
    return false;
}

static bool w2_ai_assign_harvester(level_t *map, int owner, mobj_t *unit) {
    (void)map;
    int gold = 0, lumber = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (other->owner != owner || other->remove || other->hp <= 0 || other == unit) continue;
        if (other->harvest.phase == HARVEST_PHASE_NONE || !(other->traits & MF_HARVESTER)) continue;
        if (other->harvest.resource_type == 1) ++lumber; else ++gold;
    }
    int first = lumber * 2 < gold ? 1 : 0;
    return send_to_resource(unit, first) || send_to_resource(unit, !first);
}

static const AiGameInterface w2_ai_interface = {
    .name = "warcraft-2",
    .features = AI_FEATURE_ALL,
    .player_level = w2_ai_level,
    .plan = w2_ai_plan,
    .owned = w2_ai_owned,
    .can_purchase = w2_ai_can_purchase,
    .purchase = w2_ai_purchase,
    .is_anchor = G_AiIsStructure,
    .is_busy = w2_ai_busy,
    .assign_harvester = w2_ai_assign_harvester,
};

const AiGameInterface *G_AiInterface(void) { return &w2_ai_interface; }
