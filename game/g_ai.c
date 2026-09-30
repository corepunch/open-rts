#include "game.h"
#include "p_ai.h"

/* Catalog-driven AiGameInterface hooks for games whose purchases go through
 * the shared G_FindProducer / G_QueueProduct path. A goal's product id is the
 * catalog ui_id. Dark Colony has its own purchase queue and does not use them. */

int G_AiCatalogOwned(int owner, int ui_id) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    return product ? G_CountPlannedActors(owner, G_ModelActorIdForProduct(product)) : 0;
}

int G_AiCatalogCanPurchase(const level_t *map, int owner, int ui_id) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    if (!product || !G_ModelProductAvailable(NULL, owner, product) ||
        !G_FindProducer(owner, product)) return AI_BUY_BLOCKED;
    return map->player_resources[owner][0] < product->cost ?
        AI_BUY_NEED_CREDITS : AI_BUY_OK;
}

bool G_AiCatalogPurchase(level_t *map, int owner, int ui_id) {
    (void)map;
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    mobj_t *producer = product ? G_FindProducer(owner, product) : NULL;
    return producer && G_QueueProduct(producer, product);
}

/* Any building: selectable and immobile. */
bool G_AiIsStructure(const mobj_t *unit) {
    return (unit->traits & (MF_MOBILE | MF_SELECTABLE)) == MF_SELECTABLE;
}
