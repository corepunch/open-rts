#include "engine.h"
#include "info.h"

#include <string.h>

/* Rustfront's production catalog. The Command Center builds every structure, the Barracks
 * trains infantry and the War Factory builds vehicles. Prerequisites are structures the owner
 * must have: power before a barracks or refinery, a refinery before a factory, a barracks
 * before a turret. ui ids are the actor types; icon frames index rf_icons (sprites.inc).
 * Times are in milliseconds at normal speed. */
enum { ICON_POWER, ICON_BARRACKS, ICON_REFINERY, ICON_FACTORY, ICON_TURRET, ICON_HQ,
       ICON_RIFLEMAN, ICON_ROCKET, ICON_SCOUT, ICON_TANK, ICON_APC, ICON_ARTILLERY, ICON_HARVESTER };

#define STRUCTURE(type, label, cost, icon, needs) \
    {type, type, label, cost, icon, RTS_PRODUCT_BUILDING, type, 0, {needs}, (needs) ? 1 : 0, \
     {MT_HQ}, 1, {0}, false}
#define TRAINED(type, label, cost, icon, maker, needs) \
    {type, type, label, cost, icon, RTS_PRODUCT_UNIT, type, 0, {needs}, (needs) ? 1 : 0, \
     {maker}, 1, {0}, false}

static const StaticProductDefinition RF_PRODUCTS[] = {
    STRUCTURE(MT_POWER, "POWER PLANT", 300, ICON_POWER, 0),
    STRUCTURE(MT_BARRACKS, "BARRACKS", 400, ICON_BARRACKS, MT_POWER),
    STRUCTURE(MT_REFINERY, "ORE REFINERY", 1500, ICON_REFINERY, MT_POWER),
    STRUCTURE(MT_FACTORY, "WAR FACTORY", 2000, ICON_FACTORY, MT_REFINERY),
    STRUCTURE(MT_GUN_TURRET, "GUN TURRET", 600, ICON_TURRET, MT_BARRACKS),
    TRAINED(MT_RIFLEMAN, "RIFLEMAN", 100, ICON_RIFLEMAN, MT_BARRACKS, 0),
    TRAINED(MT_ROCKET, "ROCKET SOLDIER", 300, ICON_ROCKET, MT_BARRACKS, 0),
    TRAINED(MT_SCOUT, "SCOUT BUGGY", 300, ICON_SCOUT, MT_FACTORY, 0),
    TRAINED(MT_TANK, "BATTLE TANK", 800, ICON_TANK, MT_FACTORY, 0),
    TRAINED(MT_APC, "APC", 700, ICON_APC, MT_FACTORY, 0),
    TRAINED(MT_ARTILLERY, "ARTILLERY", 900, ICON_ARTILLERY, MT_FACTORY, 0),
    TRAINED(MT_HARVESTER, "ORE HARVESTER", 1400, ICON_HARVESTER, MT_FACTORY, MT_REFINERY),
};
enum { PRODUCT_COUNT = (int)(sizeof(RF_PRODUCTS) / sizeof(*RF_PRODUCTS)) };

static const struct { int type, ms; } BUILD_TIMES[] = {
    {MT_POWER, 9000}, {MT_BARRACKS, 10000}, {MT_REFINERY, 16000}, {MT_FACTORY, 20000},
    {MT_GUN_TURRET, 9000}, {MT_RIFLEMAN, 3500}, {MT_ROCKET, 6000}, {MT_SCOUT, 6000},
    {MT_TANK, 12000}, {MT_APC, 10000}, {MT_ARTILLERY, 14000}, {MT_HARVESTER, 15000},
};

int G_ModelGetProducts(const RtsGameModel *model, int owner,
                       StaticProductDefinition *out, int max_products) {
    (void)model; (void)owner;
    if (!out || max_products <= 0) return 0;
    int count = PRODUCT_COUNT < max_products ? PRODUCT_COUNT : max_products;
    memcpy(out, RF_PRODUCTS, (size_t)count * sizeof(*out));
    return count;
}

const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id) {
    (void)model;
    for (int i = 0; i < PRODUCT_COUNT; ++i)
        if (RF_PRODUCTS[i].ui_id == ui_id) return &RF_PRODUCTS[i];
    return NULL;
}

const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class, int product_type) {
    (void)model;
    for (int i = 0; i < PRODUCT_COUNT; ++i)
        if ((int)RF_PRODUCTS[i].product_class == product_class && RF_PRODUCTS[i].product_type == product_type)
            return &RF_PRODUCTS[i];
    return NULL;
}

bool G_ModelProductAvailable(const RtsGameModel *model, int owner,
                             const StaticProductDefinition *product) {
    if (!product) return false;
    bool maker = product->maker_count <= 0;
    for (int i = 0; i < product->maker_count && !maker; ++i)
        maker = G_ModelHasActorType(model, owner, (uint16_t)product->makers[i]);
    for (int i = 0; maker && i < product->prerequisite_count; ++i)
        if (!G_ModelHasActorType(model, owner, (uint16_t)product->prerequisites[i])) return false;
    return maker;
}

uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *product) {
    return product ? (uint16_t)product->product_type : 0;
}

int G_ModelBuildingFrameForProduct(const StaticProductDefinition *product) {
    (void)product;
    return 0;
}

int G_ModelBuildingStateForProduct(const gameinfo_t *game_info,
                                  const StaticProductDefinition *product) {
    (void)game_info; (void)product;
    return -1;
}

int G_ModelProductTrainingTimeMs(const StaticProductDefinition *product) {
    if (!product) return 0;
    for (size_t i = 0; i < sizeof(BUILD_TIMES) / sizeof(*BUILD_TIMES); ++i)
        if (BUILD_TIMES[i].type == product->product_type) return BUILD_TIMES[i].ms;
    return product->cost * 10;
}

bool G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                   const StaticProductDefinition *product, uint16_t actor_id) {
    (void)model; (void)producer; (void)product; (void)actor_id;
    return false;
}

bool G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *model, const mobj_t *producer,
                                     const StaticProductDefinition *product,
                                     const mobj_t *new_unit, fixed2_t *out) {
    (void)model; (void)producer; (void)product; (void)new_unit; (void)out;
    return false;
}

void G_ModelBuildUIScript(const RtsGameModel *model, const RtsRenderSnapshot *snapshot,
                          char *dst, size_t dst_size) {
    (void)model; (void)snapshot;
    if (dst && dst_size) dst[0] = '\0';
}

bool G_PlayerBuildProduct(mobj_t *producer, const StaticProductDefinition *product) {
    return G_QueueProduct(producer, product);
}

bool G_ModelProducerHasTech(const mobj_t *producer, const StaticProductDefinition *product) {
    (void)producer; (void)product;
    return true;
}

const AiGameInterface *G_AiInterface(void) {
    return NULL;
}
