#define _DEFAULT_SOURCE
#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* Only the production relationships live here. Prices and durations belong
 * to mobjinfo[]; G_PlayerBuildProduct spends lumber on the training command. */
static StaticProductDefinition W2_PRODUCTS[] = {
    { 1, 1, "Footman",    0, 0, RTS_PRODUCT_UNIT, MT_FOOTMAN, 0, {0}, 0, {MT_HUMAN_BARRACKS}, 1 },
    { 2, 2, "Grunt",      0, 0, RTS_PRODUCT_UNIT, MT_GRUNT, 0, {0}, 0, {MT_ORC_BARRACKS}, 1 },
    { 3, 3, "Peasant",    0, 0, RTS_PRODUCT_UNIT, MT_PEASANT, 0, {0}, 0, {MT_TOWN_HALL, MT_KEEP, MT_CASTLE}, 3 },
    { 4, 4, "Peon",       0, 0, RTS_PRODUCT_UNIT, MT_PEON, 0, {0}, 0, {MT_GREAT_HALL, MT_STRONGHOLD, MT_FORTRESS}, 3 },
    { 5, 5, "Archer",     0, 0, RTS_PRODUCT_UNIT, MT_ARCHER, 0, {0}, 0, {MT_HUMAN_BARRACKS}, 1 },
    { 6, 6, "Axethrower", 0, 0, RTS_PRODUCT_UNIT, MT_AXETHROWER, 0, {0}, 0, {MT_ORC_BARRACKS}, 1 },
    { 7, 7, "Ballista",   0, 0, RTS_PRODUCT_UNIT, MT_BALLISTA, 0, {0}, 0, {MT_HUMAN_BARRACKS}, 1 },
    { 8, 8, "Catapult",   0, 0, RTS_PRODUCT_UNIT, MT_CATAPULT, 0, {0}, 0, {MT_ORC_BARRACKS}, 1 },
    { 9, 9, "Knight",     0, 0, RTS_PRODUCT_UNIT, MT_KNIGHT, 0, {0}, 0, {MT_HUMAN_BARRACKS}, 1 },
    { 10, 10, "Ogre",     0, 0, RTS_PRODUCT_UNIT, MT_OGRE, 0, {0}, 0, {MT_ORC_BARRACKS}, 1 },
};

static int product_count(void) {
    return (int)(sizeof(W2_PRODUCTS) / sizeof(W2_PRODUCTS[0]));
}

void w2_init_products(void) {
    for (int i = 0; i < product_count(); ++i)
        W2_PRODUCTS[i].cost = mobjinfo[W2_PRODUCTS[i].product_type].w2.costs.resources[0];
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
    if (product->maker_count <= 0) return true;
    for (int i = 0; i < product->maker_count; ++i)
        if (G_ModelHasActorType(model, owner, (uint16_t)product->makers[i]))
            return true;
    return false;
}

uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *product) {
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
    if (!product) return 0;
    return mobjinfo[product->product_type].w2.costs.time * 1000;
}

bool G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                   const StaticProductDefinition *product,
                                   uint16_t actor_id) {
    (void)model; (void)producer; (void)product; (void)actor_id;
    return false;
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
    int lumber = W2_ProductLumber(product);
    int *stock = level.player_resources[producer->owner];
    if (stock[1] < lumber || !G_QueueProduct(producer, product)) return false;
    stock[1] -= lumber;
    return true;
}

int W2_ProductLumber(const StaticProductDefinition *product) {
    return product ? mobjinfo[product->product_type].w2.costs.resources[1] : 0;
}

bool G_ModelProducerHasTech(const mobj_t *producer, const StaticProductDefinition *product) {
    (void)producer; (void)product;
    return true;
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
