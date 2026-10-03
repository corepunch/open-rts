#define _DEFAULT_SOURCE
#include "engine.h"
#include "info.h"
#include "w2_local.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* Gold costs are the Wargus numbers. Makers are actor ids (PUD type + 1).
 * Release is left to the engine; this slice does not run build animations. */
static const StaticProductDefinition W2_PRODUCTS[] = {
    { 1, 1, "Footman", 600, 0, RTS_PRODUCT_UNIT, 1, 0, {0}, 0, {61}, 1 },
    { 2, 2, "Grunt",   600, 0, RTS_PRODUCT_UNIT, 2, 0, {0}, 0, {62}, 1 },
    { 3, 3, "Peasant", 400, 0, RTS_PRODUCT_UNIT, 3, 0, {0}, 0, {75, 89, 91}, 3 },
    { 4, 4, "Peon",    400, 0, RTS_PRODUCT_UNIT, 4, 0, {0}, 0, {76, 90, 92}, 3 },
};

static int product_count(void) {
    return (int)(sizeof(W2_PRODUCTS) / sizeof(W2_PRODUCTS[0]));
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
    return product->cost * 10;
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
    append_ui_script(dst, dst_size, "ui warcraft2 1\n");
    append_ui_script(dst, dst_size, "x 630 y 3 text \"Gold %d\"\n",
                     snapshot->player_resources[consoleplayer][0]);
}

bool G_PlayerBuildProduct(mobj_t *producer, const StaticProductDefinition *product) {
    return G_QueueProduct(producer, product);
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
    .name = "warcraft2",
    .features = 0,
    .player_level = P_AiLevelNonHuman,
    .plan = w2_ai_plan,
    .owned = G_AiCatalogOwned,
    .can_purchase = G_AiCatalogCanPurchase,
    .purchase = G_AiCatalogPurchase,
};

const AiGameInterface *G_AiInterface(void) { return &w2_ai_interface; }
