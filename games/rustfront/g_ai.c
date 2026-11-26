#include "engine.h"
#include "info.h"
#include "rustfront.h"

/* The computer player is the engine's (play/p_ai.c) with Rustfront's faction (rules.c). Two
 * things are Rustfront's own: it places structures itself, on spaced-out sites near its
 * Command Center, so they rise on scaffolding as the player's do (g_build.c); and every
 * structure anchors its defence and is a target for its waves, not only refineries. */

/* A site with a free cell all round it, so units still find their way between buildings. */
static bool roomy(uint16_t type, ivec2_t cell, const mobj_t *builder) {
    const mobjtype_t *info = P_ActorType(type);
    if (!info || !P_CanPlaceBuilding(type, cell, builder)) return false;
    for (int y = cell.y - 1; y <= cell.y + info->footprint.h; ++y)
        for (int x = cell.x - 1; x <= cell.x + info->footprint.w; ++x)
            if (!L_Contains(&level, x, y) || level.blocked[L_Index(&level, x, y)]) return false;
    return true;
}

/* The nearest roomy site to the Command Center, searched ring by ring. */
static bool find_site(const mobj_t *hq, uint16_t type, ivec2_t *out) {
    ivec2_t origin = fixed2_cell(fixed3_xy(hq->core.position));
    int limit = level.width > level.height ? level.width : level.height;
    for (int r = 2; r <= limit; ++r)
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                if (abs(x) != r && abs(y) != r) continue;
                ivec2_t cell = ivec2_add(origin, (ivec2_t){x, y});
                if (roomy(type, cell, hq)) { *out = cell; return true; }
            }
    return false;
}

static mobj_t *command_center(int owner) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        if (th->function == P_MobjThinker && unit->owner == owner && RF_IsRole(unit->type_id, RF_ROLE_HQ) &&
            !unit->remove && unit->hp > 0) return unit;
    }
    return NULL;
}

/* Structures go on a site the AI picks; everything else through the catalog. */
static bool purchase(level_t *map, int owner, int ui_id) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    if (!product || product->product_class != RTS_PRODUCT_BUILDING) return G_AiCatalogPurchase(map, owner, ui_id);
    mobj_t *hq = command_center(owner);
    ivec2_t cell;
    if (!hq || RF_SiteInProgress(owner) || !find_site(hq, (uint16_t)product->product_type, &cell)) return false;
    return G_PlaceProduct(hq, product, cell);
}

/* A harvester waits while its side has no finished refinery to bring ore to (a start's
 * harvester comes before the refinery, and a refinery can be burnt down): otherwise it fills
 * up, finds nowhere to unload and goes idle, and the engine plans it a new trip every tic. */
static bool busy(const mobj_t *unit) {
    if (!(unit->traits & MF_HARVESTER)) return false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == unit->owner && (u->traits & MF_RESOURCE_BASE) &&
            !u->remove && u->hp > 0) return false;
    }
    return true;
}

static const AiGameInterface rf_ai = {
    .name = "rustfront",
    .features = AI_FEATURE_ECONOMY | AI_FEATURE_PRODUCTION | AI_FEATURE_DEFENSE |
                AI_FEATURE_ATTACK | AI_FEATURE_DOCTRINE,
    .player_level = P_AiLevelNonHuman,
    .purchase = purchase,
    .is_anchor = G_AiIsStructure,
    .is_busy = busy,
};

const AiGameInterface *G_AiInterface(void) { return &rf_ai; }
