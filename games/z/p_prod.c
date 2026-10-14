#include "engine.h"
#include "info.h"

/* Z builds through forts and factories on timers rather than a product
 * list; none of that is decoded yet, so nothing can be bought. */

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
                                                         int product_class, int product_type) {
    (void)model; (void)product_class; (void)product_type;
    return NULL;
}

bool G_ModelProductAvailable(const RtsGameModel *model, int owner,
                             const StaticProductDefinition *product) {
    (void)model; (void)owner; (void)product;
    return false;
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
    (void)product;
    return 0;
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
    (void)producer; (void)product;
    return false;
}

bool G_ModelProducerHasTech(const mobj_t *producer, const StaticProductDefinition *product) {
    (void)producer; (void)product;
    return false;
}

int G_ModelRadarLevel(int owner) {
    (void)owner;
    return 2;
}

static const AiGameInterface z_ai_interface = {
    .name = "z",
    .player_level = P_AiLevelNonHuman,
};

const AiGameInterface *G_AiInterface(void) { return &z_ai_interface; }
