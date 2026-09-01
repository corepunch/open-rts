#define _DEFAULT_SOURCE
#include "game.h"
#include "g_game.h"

int G_ModelGetProducts(const RtsGameModel *model, int owner,
                       StaticProductDefinition *out, int max_products) {
    (void)model; (void)owner; (void)out; (void)max_products;
    return 0;
}

const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id) {
    (void)model; (void)ui_id;
    return NULL;
}

const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class,
                                                         int product_type) {
    (void)model; (void)product_class; (void)product_type;
    return NULL;
}

bool G_ModelProductAvailable(const RtsGameModel *model, int owner,
                             const StaticProductDefinition *product) {
    (void)model; (void)owner; (void)product;
    return false;
}

uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *product) {
    (void)product;
    return 0;
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
    (void)product;
    return 0;
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

void G_ModelBuildUIScript(const RtsGameModel *model,
                          const RtsRenderSnapshot *snapshot,
                          char *dst, size_t dst_size) {
    (void)model; (void)snapshot;
    if (dst && dst_size > 0) dst[0] = '\0';
}

void G_ModelAIProduction(RtsGameModel *model, int elapsed_ms) {
    (void)model; (void)elapsed_ms;
}
