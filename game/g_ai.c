#include "engine.h"

/* Catalog-driven AiGameInterface defaults for games whose purchases go through
 * the shared G_FindProducer / G_QueueProduct path. A goal's product id is the
 * catalog ui_id. Dark Colony has its own purchase queue and does not use them. */

int G_AiCatalogOwned(int owner, int ui_id) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    return product ? G_CountPlannedActors(owner, G_ModelActorIdForProduct(product)) : 0;
}

int G_AiCatalogCanPurchase(const level_t *map, int owner, int ui_id) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    if (!product) return AI_BUY_BLOCKED;
    /* A worker's building is placed at once and does not wait in a queue. */
    if (!G_ModelProductAvailable(NULL, owner, product) ||
        !(product->worker_build ? G_FindProducer(owner, product) :
                                  G_FindProducerBelow(owner, product, AI_QUEUE_DEPTH)))
        /* Short of tech a producer could research: wait for it, not skip. */
        return R_ProducerLackingTech(owner, product) ? AI_BUY_NEED_TECH : AI_BUY_BLOCKED;
    /* Gas and the like cannot be saved for by waiting at the shop. */
    int cost[RTS_MAX_RESOURCES];
    R_ProductCosts(product, cost);
    for (int r = 1; r < RTS_MAX_RESOURCES; ++r)
        if (cost[r] > map->player_resources[owner][r]) return AI_BUY_BLOCKED;
    return map->player_resources[owner][0] < cost[0] ?
        AI_BUY_NEED_CREDITS : AI_BUY_OK;
}

bool G_AiCatalogPurchase(level_t *map, int owner, int ui_id) {
    (void)map;
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    mobj_t *producer = product ? G_FindProducerBelow(owner, product, AI_QUEUE_DEPTH) : NULL;
    return producer && G_QueueProduct(producer, product);
}

/* Any building: selectable and immobile. */
bool G_AiIsStructure(const mobj_t *unit) {
    return (unit->traits & (MF_MOBILE | MF_SELECTABLE)) == MF_SELECTABLE;
}

/* Units and structures; research makes no actor. */
int G_AiCatalogActor(int ui_id) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    return product && product->product_class != RTS_PRODUCT_UPGRADE ? G_ModelActorIdForProduct(product) : 0;
}
