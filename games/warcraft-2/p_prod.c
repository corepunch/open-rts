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
static StaticProductDefinition W2_PRODUCTS[] = {
    { 1, 1, "Footman",    0, 0, U, MT_FOOTMAN, 0, {0}, 0, {MT_HUMAN_BARRACKS}, 1 },
    { 2, 2, "Grunt",      0, 0, U, MT_GRUNT, 0, {0}, 0, {MT_ORC_BARRACKS}, 1 },
    { 3, 3, "Peasant",    0, 0, U, MT_PEASANT, 0, {0}, 0, {MT_TOWN_HALL, MT_KEEP, MT_CASTLE}, 3 },
    { 4, 4, "Peon",       0, 0, U, MT_PEON, 0, {0}, 0, {MT_GREAT_HALL, MT_STRONGHOLD, MT_FORTRESS}, 3 },
    { 5, 5, "Archer",     0, 0, U, MT_ARCHER, 0, {0}, 0, {MT_HUMAN_BARRACKS}, 1 },
    { 6, 6, "Axethrower", 0, 0, U, MT_AXETHROWER, 0, {0}, 0, {MT_ORC_BARRACKS}, 1 },
    { 7, 7, "Ballista",   0, 0, U, MT_BALLISTA, 0, {0}, 0, {MT_HUMAN_BARRACKS}, 1 },
    { 8, 8, "Catapult",   0, 0, U, MT_CATAPULT, 0, {0}, 0, {MT_ORC_BARRACKS}, 1 },
    { 9, 9, "Knight",     0, 0, U, MT_KNIGHT, 0, {0}, 0, {MT_HUMAN_BARRACKS}, 1 },
    { 10, 10, "Ogre",     0, 0, U, MT_OGRE, 0, {0}, 0, {MT_ORC_BARRACKS}, 1 },
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
};
#undef U
#undef R
#undef B

/* The pinned Wargus upgrade.lua rows: time and costs, the modifier each
 * applies, and the types it applies to. Icons are the level researched. */
static const w2_upgrade_t W2_UPGRADES[W2_UPGRADE_COUNT] = {
    [W2_UPGRADE_SWORD1] = { "upgrade-sword1", 117, 200, 800, 0, 0, false, 1, 2,
        MT_HUMAN_BLACKSMITH, {MT_FOOTMAN, MT_KNIGHT, MT_PALADIN} },
    [W2_UPGRADE_SWORD2] = { "upgrade-sword2", 118, 250, 2400, 0, 0, false, 2, 2,
        MT_HUMAN_BLACKSMITH, {MT_FOOTMAN, MT_KNIGHT, MT_PALADIN} },
    [W2_UPGRADE_AXE1] = { "upgrade-battle-axe1", 120, 200, 500, 100, 0, false, 1, 2,
        MT_ORC_BLACKSMITH, {MT_GRUNT, MT_OGRE, MT_OGRE_MAGE} },
    [W2_UPGRADE_AXE2] = { "upgrade-battle-axe2", 121, 250, 1500, 300, 0, false, 2, 2,
        MT_ORC_BLACKSMITH, {MT_GRUNT, MT_OGRE, MT_OGRE_MAGE} },
    [W2_UPGRADE_ARROW1] = { "upgrade-arrow1", 125, 200, 300, 300, 0, false, 1, 1,
        MT_ELVEN_LUMBER_MILL, {MT_ARCHER, MT_RANGER} },
    [W2_UPGRADE_ARROW2] = { "upgrade-arrow2", 126, 250, 900, 500, 0, false, 2, 1,
        MT_ELVEN_LUMBER_MILL, {MT_ARCHER, MT_RANGER} },
    [W2_UPGRADE_THROWING_AXE1] = { "upgrade-throwing-axe1", 128, 200, 300, 300, 0, false, 1, 1,
        MT_TROLL_LUMBER_MILL, {MT_AXETHROWER, MT_BERSERKER} },
    [W2_UPGRADE_THROWING_AXE2] = { "upgrade-throwing-axe2", 129, 250, 900, 500, 0, false, 2, 1,
        MT_TROLL_LUMBER_MILL, {MT_AXETHROWER, MT_BERSERKER} },
    [W2_UPGRADE_HUMAN_SHIELD1] = { "upgrade-human-shield1", 165, 200, 300, 300, 0, true, 1, 2,
        MT_HUMAN_BLACKSMITH, {MT_FOOTMAN, MT_KNIGHT, MT_PALADIN} },
    [W2_UPGRADE_HUMAN_SHIELD2] = { "upgrade-human-shield2", 166, 250, 900, 500, 0, true, 2, 2,
        MT_HUMAN_BLACKSMITH, {MT_FOOTMAN, MT_KNIGHT, MT_PALADIN} },
    [W2_UPGRADE_ORC_SHIELD1] = { "upgrade-orc-shield1", 168, 200, 300, 300, 0, true, 1, 2,
        MT_ORC_BLACKSMITH, {MT_GRUNT, MT_OGRE, MT_OGRE_MAGE} },
    [W2_UPGRADE_ORC_SHIELD2] = { "upgrade-orc-shield2", 169, 250, 900, 500, 0, true, 2, 2,
        MT_ORC_BLACKSMITH, {MT_GRUNT, MT_OGRE, MT_OGRE_MAGE} },
};

const w2_upgrade_t *W2_Upgrade(int id) {
    return id > 0 && id < W2_UPGRADE_COUNT ? &W2_UPGRADES[id] : NULL;
}

/* The tier the owner has of this upgrade's line, read off its first type. */
int W2_UpgradeLevel(int owner, const w2_upgrade_t *upgrade) {
    if (!upgrade || owner < 0 || owner >= 8 || !upgrade->units[0]) return 0;
    return upgrade->armor ? level.upgrades[upgrade->units[0]][owner].armor :
                            level.upgrades[upgrade->units[0]][owner].weapon;
}

void W2_ApplyUpgrade(int owner, int id) {
    const w2_upgrade_t *upgrade = W2_Upgrade(id);
    if (!upgrade || owner < 0 || owner >= 8) return;
    for (int i = 0; i < 4 && upgrade->units[i]; ++i) {
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
    unit->traits = (unit->traits & (MF_SELECTED | MF_DONTDRAW)) | unit->info->traits;
    unit->max_hp = to->spawnhealth > 0 ? to->spawnhealth : 1;
    unit->hp = hp * unit->max_hp / old_max;
    if (unit->hp < 1) unit->hp = 1;
    snprintf(unit->core.sprite_name, sizeof(unit->core.sprite_name), "%s", to->name);
    unit->core.state_id = 0;
    P_SetMobjState(unit, to->spawnstate);
    W2_EnsureUnitSprite(type - 1);
    return true;
}

static int product_count(void) {
    return (int)(sizeof(W2_PRODUCTS) / sizeof(W2_PRODUCTS[0]));
}

static const w2_upgrade_t *product_upgrade(const StaticProductDefinition *product) {
    return product && product->product_class == RTS_PRODUCT_UPGRADE ?
        W2_Upgrade(product->product_type) : NULL;
}

void w2_init_products(void) {
    for (int i = 0; i < product_count(); ++i) {
        StaticProductDefinition *product = &W2_PRODUCTS[i];
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
    for (int i = 0; i < product->prerequisite_count; ++i)
        if (!G_ModelHasActorType(model, owner, (uint16_t)product->prerequisites[i]))
            return false;
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
    if (upgrade) return upgrade->time * 1000;
    if (!product) return 0;
    return mobjinfo[product->product_type].w2.costs.time * 1000;
}

/* Units leave the engine's queue as new actors. Research and hall upgrades
 * finish here instead: the effect lands on the player or the building, and
 * the order is taken off the queue. */
bool G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                   const StaticProductDefinition *product,
                                   uint16_t actor_id) {
    (void)model; (void)actor_id;
    if (!producer || !product || product->product_class == RTS_PRODUCT_UNIT) return false;
    if (product->product_class == RTS_PRODUCT_UPGRADE)
        W2_ApplyUpgrade(producer->owner, product->product_type);
    else
        W2_TransformUnit(producer, (uint16_t)product->product_type);
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
 * is training. */
bool G_ModelProducerHasTech(const mobj_t *producer, const StaticProductDefinition *product) {
    if (!producer || !product) return false;
    if (product->product_class == RTS_PRODUCT_UNIT) return true;
    return !producer->production || producer->production->queue_count <= 0;
}

int G_ModelRadarLevel(int owner) {
    (void)owner;
    return 2;
}

static bool w2_ai_plan(const level_t *map, int owner, int level, AiPlan *out) {
    (void)map; (void)owner; (void)level;
    out->wave_interval_ms = 35000;
    out->wave_min_size = 4;
    out->wave_max_size = 12;
    P_AiPlanAdd(out, 4, 4);
    P_AiPlanAdd(out, 2, 6);
    return true;
}

static const AiGameInterface w2_ai_interface = {
    .name = "warcraft-2",
    .features = 0,
    .player_level = P_AiLevelNonHuman,
    .plan = w2_ai_plan,
    .owned = G_AiCatalogOwned,
    .can_purchase = G_AiCatalogCanPurchase,
    .purchase = G_AiCatalogPurchase,
};

const AiGameInterface *G_AiInterface(void) { return &w2_ai_interface; }
