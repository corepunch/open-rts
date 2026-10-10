#include "dark-colony.h"
#include "engine.h"
#include "info.h"
#include <math.h>
#include <string.h>

static int ai_nearest_vent(const level_t *map, fixed2_t position) {
    int best = -1;
    int64_t best_distance2 = INT64_MAX;
    if (!map || !map->resource_vents) return best;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->amount <= 0 || vent->rate <= 0) continue;
        int64_t distance2 = fixed2_distance_squared64(position, vent->attachment);
        if (distance2 < best_distance2) {
            best_distance2 = distance2;
            best = i;
        }
    }
    return best;
}

static void update_ai_economy(const level_t *map, mobj_t *const *units,
                                          int unit_count) {
    if (!map || !units) return;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *unit = units[i];
        if (unit->remove || unit->hp <= 0 || D_PlayerIsHuman(unit->owner) ||
            (unit->traits & (MF_MOBILE | MF_HARVESTER)) !=
                (MF_MOBILE | MF_HARVESTER) || unit->harvest.target >= 0) continue;
        int vent_index = ai_nearest_vent(
            map, fixed3_xy(unit->core.position));
        if (vent_index >= 0)
            P_HarvestUnitTo(map, unit, map->resource_vents[vent_index].attachment);
    }
}

void DC_UpdateAI(const level_t *map, mobj_t *const *units, int unit_count) {
    if (!map || !units) return;
    if (map_has_ai(map, 1)) update_ai_economy(map, units, unit_count);
}

/* ── universal AI interface for skirmish ─────────────────────────────────
 *
 * DC.EXE dispatches each AI player through per-type tables (0x474360 ->
 * list at 0x47630c for AI/AI+: a single score/action pair, 0x447670 and
 * 0x447854, which drives four squad objects per think). Its decision code is
 * not ported; this ladder reproduces the observable structure (think cadence,
 * economy first, tech up, steady army, waves) through the retail purchase
 * path, DC_SelectPurchase/DC_SubmitPurchases. The only verified AI versus
 * AI+ difference is the credit multiplier (see DC_ApplyAiIncome). */

static int dc_ai_level(const level_t *map, int owner) {
    const dc_skirmish_t *setup = DC_LevelSkirmish(map);
    if (!setup || owner < 0 || owner >= 8 || D_PlayerIsHuman(owner)) return AI_LEVEL_NONE;
    switch (setup->players[owner].type) {
    case DC_PLAYER_AI: return AI_LEVEL_NORMAL;
    case DC_PLAYER_AI_PLUS: return AI_LEVEL_PLUS;
    default: return AI_LEVEL_NONE;
    }
}

static int dc_ai_owned(int owner, int ui_id) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    if (!product || owner < 0 || owner >= 8) return 0;
    int actor_id = G_ModelActorIdForProduct(product);
    int count = 0;
    if (product->row_id >= 0 && product->row_id < 110)
        count += level.purchases[owner][product->row_id].queued +
                 level.purchases[owner][product->row_id].selected;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *actor = (const mobj_t *)th;
        if (actor->owner != owner || actor->remove || actor->hp <= 0) continue;
        if (DC_ProductActorMatches(actor->type_id, actor_id)) ++count;
        if (product->product_class == RTS_PRODUCT_UNIT && actor->production &&
            actor->production->queue_count > 0 &&
            actor->production->product_class == RTS_PRODUCT_UNIT &&
            actor->production->product_type == product->product_type)
            count += actor->production->queue_count;
    }
    return count;
}

static int dc_ai_can_purchase(const level_t *map, int owner, int ui_id) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    if (!product || product->faction != DC_PlayerRace(owner) ||
        !G_ModelProductAvailable(NULL, owner, product)) return AI_BUY_BLOCKED;
    if (product->product_class != RTS_PRODUCT_UPGRADE && !G_FindProducer(owner, product))
        return AI_BUY_BLOCKED;
    return map->player_resources[owner][0] < product->cost ?
        AI_BUY_NEED_CREDITS : AI_BUY_OK;
}

static bool dc_ai_purchase(level_t *map, int owner, int ui_id) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    if (!product || product->row_id < 0 || product->row_id >= 110) return false;
    int before = map->purchases[owner][product->row_id].selected;
    DC_SelectPurchase(owner, ui_id, false);
    if (map->purchases[owner][product->row_id].selected <= before) return false;
    DC_SubmitPurchases(owner);
    return true;
}

static bool dc_ai_is_base(const mobj_t *unit) {
    return unit->type_id >= MT_EXCOPOD && unit->type_id <= MT_ALIEN_RSCHIVE &&
           (unit->traits & MF_MOBILE) == 0 && unit->owner < 8;
}

static const AiGameInterface dc_ai_interface = {
    .name = "dark-colony",
    .features = AI_FEATURE_ALL,
    .player_level = dc_ai_level,
    .owned = dc_ai_owned,
    .can_purchase = dc_ai_can_purchase,
    .purchase = dc_ai_purchase,
    .is_base = dc_ai_is_base,
};

const AiGameInterface *G_AiInterface(void) {
    return &dc_ai_interface;
}

/* 0x401694/0x4016a0 set the owner's credit multiplier to 0x200 for AI+ and
 * 0x100 for AI; 0x412daa scales non-human owners' credits by it. */
void DC_ApplyAiIncome(level_t *map, const dc_skirmish_t *setup) {
    if (!map || !setup) return;
    for (int i = 0; i < 8; ++i)
        map->income_scale[i] = setup->players[i].type == DC_PLAYER_AI_PLUS ? 0x200 : 0;
}
