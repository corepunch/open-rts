#include "engine.h"
#include "info.h"

#include <string.h>

#include "rustfront.h"
#include "sprites.inc"

/* Rustfront's production catalog, one block per faction (factions.c). Each faction's
 * headquarters builds its structures, its barracks trains its infantry and its factory builds
 * its vehicles. Prerequisites are structures the owner must have: power before a barracks or
 * refinery, a refinery before a factory, a barracks before a turret. ui ids are the actor
 * types; icon frames are art/icons.py's (RF_ICON_* in sprites.inc). Times are milliseconds at
 * normal speed. A side sees only its own faction's products, and in a campaign mission the player
 * only what the mission allows (g_campaign.c). */
#define STRUCTURE(type, label, cost, icon, hq, needs) \
    {type, type, label, cost, RF_ICON_##icon, RTS_PRODUCT_BUILDING, type, 0, {needs}, (needs) ? 1 : 0, \
     {hq}, 1, {0}, false}
#define TRAINED(type, label, cost, icon, maker, needs) \
    {type, type, label, cost, RF_ICON_##icon, RTS_PRODUCT_UNIT, type, 0, {needs}, (needs) ? 1 : 0, \
     {maker}, 1, {0}, false}

static const StaticProductDefinition RF_PRODUCTS[] = {
    /* Directorate: the reference army. */
    STRUCTURE(MT_POWER, "POWER PLANT", 300, POWER, MT_HQ, 0),
    STRUCTURE(MT_BARRACKS, "BARRACKS", 400, BARRACKS, MT_HQ, MT_POWER),
    STRUCTURE(MT_REFINERY, "ORE REFINERY", 1500, REFINERY, MT_HQ, MT_POWER),
    STRUCTURE(MT_FACTORY, "WAR FACTORY", 2000, FACTORY, MT_HQ, MT_REFINERY),
    STRUCTURE(MT_GUN_TURRET, "GUN TURRET", 600, TURRET, MT_HQ, MT_BARRACKS),
    TRAINED(MT_RIFLEMAN, "RIFLEMAN", 100, RIFLEMAN, MT_BARRACKS, 0),
    TRAINED(MT_ROCKET, "ROCKET SOLDIER", 300, ROCKET, MT_BARRACKS, 0),
    TRAINED(MT_SCOUT, "SCOUT BUGGY", 300, SCOUT, MT_FACTORY, 0),
    TRAINED(MT_TANK, "BATTLE TANK", 800, TANK, MT_FACTORY, 0),
    TRAINED(MT_APC, "APC", 700, APC, MT_FACTORY, 0),
    TRAINED(MT_ARTILLERY, "ARTILLERY", 900, ARTILLERY, MT_FACTORY, 0),
    TRAINED(MT_MASTODON, "MASTODON", 1800, MASTODON, MT_FACTORY, MT_BARRACKS),
    TRAINED(MT_HARVESTER, "ORE HARVESTER", 1400, HARVESTER, MT_FACTORY, MT_REFINERY),
    STRUCTURE(MT_UPLINK, "UPLINK CENTRE", 1500, UPLINK, MT_HQ, MT_FACTORY),
    TRAINED(MT_HAMMER, "HAMMER", 1800, HAMMER, MT_FACTORY, MT_UPLINK),
    TRAINED(MT_SENTRY_TRUCK, "SENTRY TRUCK", 650, SENTRY_TRUCK, MT_FACTORY, 0),
    TRAINED(MT_CRANE, "FIELD CRANE", 700, CRANE, MT_FACTORY, 0),
    TRAINED(MT_PHANTOM, "PHANTOM", 1100, PHANTOM, MT_FACTORY, MT_BARRACKS),
    TRAINED(MT_KESTREL, "KESTREL", 1200, KESTREL, MT_FACTORY, MT_UPLINK),
    /* Scrap Clans: everything cheaper and quicker to make. */
    STRUCTURE(MT_CL_GENERATOR, "GENERATOR", 200, CL_GENERATOR, MT_CL_HALL, 0),
    STRUCTURE(MT_CL_CAMP, "WAR CAMP", 300, CL_CAMP, MT_CL_HALL, MT_CL_GENERATOR),
    STRUCTURE(MT_CL_SMELTER, "SMELTER", 1000, CL_SMELTER, MT_CL_HALL, MT_CL_GENERATOR),
    STRUCTURE(MT_CL_YARD, "SCRAP YARD", 1600, CL_YARD, MT_CL_HALL, MT_CL_SMELTER),
    STRUCTURE(MT_CL_TOWER, "SPIKE TOWER", 400, CL_TOWER, MT_CL_HALL, MT_CL_CAMP),
    TRAINED(MT_CL_RAIDER, "RAIDER", 70, CL_RAIDER, MT_CL_CAMP, 0),
    TRAINED(MT_CL_TORCHER, "TORCHER", 200, CL_TORCHER, MT_CL_CAMP, 0),
    TRAINED(MT_CL_TRIKE, "TRIKE", 200, CL_TRIKE, MT_CL_YARD, 0),
    TRAINED(MT_CL_TRUCK, "GUN TRUCK", 450, CL_TRUCK, MT_CL_YARD, 0),
    TRAINED(MT_CL_CRAWLER, "SCRAP CRAWLER", 650, CL_CRAWLER, MT_CL_YARD, 0),
    TRAINED(MT_CL_RIG, "ROCKET RIG", 800, CL_RIG, MT_CL_YARD, MT_CL_CAMP),
    TRAINED(MT_CL_HAULER, "SCRAP HAULER", 900, CL_HAULER, MT_CL_YARD, MT_CL_SMELTER),
    STRUCTURE(MT_CL_SHOP, "CHOP SHOP", 1000, CL_SHOP, MT_CL_HALL, MT_CL_YARD),
    TRAINED(MT_CL_WRECKER, "WRECKER", 600, CL_WRECKER, MT_CL_YARD, MT_CL_SHOP),
    TRAINED(MT_CL_SAPPER, "SAPPER", 250, CL_SAPPER, MT_CL_CAMP, 0),
    TRAINED(MT_CL_DRILL, "DRILL RIG", 1200, CL_DRILL, MT_CL_YARD, MT_CL_SMELTER),
    TRAINED(MT_CL_CRUSHER, "CRUSHER", 750, CL_CRUSHER, MT_CL_YARD, 0),
    TRAINED(MT_CL_BUZZARD, "BUZZARD", 900, CL_BUZZARD, MT_CL_YARD, MT_CL_SHOP),
    /* Lumen Concord: dear, slow to make, worth it. */
    STRUCTURE(MT_LC_FUSION, "FUSION CORE", 400, LC_FUSION, MT_LC_SPIRE, 0),
    STRUCTURE(MT_LC_ACADEMY, "ACADEMY", 500, LC_ACADEMY, MT_LC_SPIRE, MT_LC_FUSION),
    STRUCTURE(MT_LC_EXTRACTOR, "EXTRACTOR", 1800, LC_EXTRACTOR, MT_LC_SPIRE, MT_LC_FUSION),
    STRUCTURE(MT_LC_FOUNDRY, "FOUNDRY", 2400, LC_FOUNDRY, MT_LC_SPIRE, MT_LC_EXTRACTOR),
    STRUCTURE(MT_LC_TOWER, "BEAM TOWER", 900, LC_TOWER, MT_LC_SPIRE, MT_LC_ACADEMY),
    TRAINED(MT_LC_WARDEN, "WARDEN", 160, LC_WARDEN, MT_LC_ACADEMY, 0),
    TRAINED(MT_LC_LANCER, "LANCER", 450, LC_LANCER, MT_LC_ACADEMY, 0),
    TRAINED(MT_LC_SKIMMER, "SKIMMER", 450, LC_SKIMMER, MT_LC_FOUNDRY, 0),
    TRAINED(MT_LC_GLAIVE, "GLAIVE", 1400, LC_GLAIVE, MT_LC_FOUNDRY, 0),
    TRAINED(MT_LC_PRISM, "PRISM", 1500, LC_PRISM, MT_LC_FOUNDRY, MT_LC_ACADEMY),
    TRAINED(MT_LC_HARVESTER, "HOVER HARVESTER", 1600, LC_HARVESTER, MT_LC_FOUNDRY, MT_LC_EXTRACTOR),
    STRUCTURE(MT_LC_SANCTUM, "SANCTUM", 1600, LC_SANCTUM, MT_LC_SPIRE, MT_LC_FOUNDRY),
    TRAINED(MT_LC_AEGIS, "AEGIS", 1400, LC_AEGIS, MT_LC_FOUNDRY, MT_LC_SANCTUM),
    TRAINED(MT_LC_CANTOR, "CANTOR", 350, LC_CANTOR, MT_LC_ACADEMY, 0),
    TRAINED(MT_LC_STRIDER, "STRIDER", 1200, LC_STRIDER, MT_LC_FOUNDRY, MT_LC_ACADEMY),
    TRAINED(MT_LC_SERAPH, "SERAPH", 1500, LC_SERAPH, MT_LC_FOUNDRY, MT_LC_SANCTUM),
};
enum { PRODUCT_COUNT = (int)(sizeof(RF_PRODUCTS) / sizeof(*RF_PRODUCTS)) };

static const struct { int type, ms; } BUILD_TIMES[] = {
    {MT_POWER, 9000}, {MT_BARRACKS, 10000}, {MT_REFINERY, 16000}, {MT_FACTORY, 20000},
    {MT_GUN_TURRET, 9000}, {MT_RIFLEMAN, 3500}, {MT_ROCKET, 6000}, {MT_SCOUT, 6000},
    {MT_TANK, 12000}, {MT_APC, 10000}, {MT_ARTILLERY, 14000}, {MT_HARVESTER, 15000}, {MT_MASTODON, 22000},
    {MT_CL_GENERATOR, 6000}, {MT_CL_CAMP, 7000}, {MT_CL_SMELTER, 12000}, {MT_CL_YARD, 15000},
    {MT_CL_TOWER, 6000}, {MT_CL_RAIDER, 2200}, {MT_CL_TORCHER, 4000}, {MT_CL_TRIKE, 4000},
    {MT_CL_TRUCK, 7000}, {MT_CL_CRAWLER, 9000}, {MT_CL_RIG, 11000}, {MT_CL_HAULER, 11000},
    {MT_LC_FUSION, 12000}, {MT_LC_ACADEMY, 13000}, {MT_LC_EXTRACTOR, 20000}, {MT_LC_FOUNDRY, 25000},
    {MT_LC_TOWER, 12000}, {MT_LC_WARDEN, 3800}, {MT_LC_LANCER, 7500}, {MT_LC_SKIMMER, 6000},
    {MT_LC_GLAIVE, 15000}, {MT_LC_PRISM, 20000}, {MT_LC_HARVESTER, 16000},
    {MT_UPLINK, 18000}, {MT_HAMMER, 20000}, {MT_CL_SHOP, 12000}, {MT_CL_WRECKER, 8000},
    {MT_LC_SANCTUM, 20000}, {MT_LC_AEGIS, 18000},
    {MT_SENTRY_TRUCK, 9000}, {MT_CRANE, 10000}, {MT_PHANTOM, 14000}, {MT_KESTREL, 15000},
    {MT_CL_SAPPER, 4500}, {MT_CL_DRILL, 13000}, {MT_CL_CRUSHER, 9000}, {MT_CL_BUZZARD, 11000},
    {MT_LC_CANTOR, 6000}, {MT_LC_STRIDER, 16000}, {MT_LC_SERAPH, 17000},
};

int G_ModelGetProducts(const RtsGameModel *model, int owner,
                       StaticProductDefinition *out, int max_products) {
    (void)model;
    if (!out || max_products <= 0) return 0;
    int faction = RF_OwnerFaction(owner), count = 0;
    for (int i = 0; i < PRODUCT_COUNT && count < max_products; ++i)
        if ((faction < 0 || RF_FactionOfType((uint16_t)RF_PRODUCTS[i].product_type) == faction) &&
            RF_CampaignAllows(owner, (uint16_t)RF_PRODUCTS[i].product_type))
            out[count++] = RF_PRODUCTS[i];
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
    if (!product || !RF_CampaignAllows(owner, (uint16_t)product->product_type)) return false;
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

