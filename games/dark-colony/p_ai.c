#include "p_ai.h"
#include "../../play/p_ai.h"
#include "dc_types.h"
#include "dc_skirmish.h"
#include "game.h"
#include "info.h"
#include "p_local.h"
#include "d_net.h"
#include <math.h>
#include <string.h>

static int ai_nearest_vent(const level_t *map, fvec2_t position) {
    int best = -1;
    float best_distance2 = INFINITY;
    if (!map || !map->resource_vents) return best;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->amount <= 0 || vent->rate <= 0) continue;
        float distance2 = fvec2_distance_squared(position, vent->attachment);
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
            map, fixed3_xy_to_fvec2(unit->core.position));
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

enum {
    /* MAINE button ids of the DEPEND rows, see DARK_COLONY_PRODUCTS. */
    UI_BARRACKS = 80, UI_SCIPOD = 81, UI_ROBOFTR = 82, UI_SCIPOD2 = 85, UI_ROBOFTR2 = 86,
    UI_EXPLOITER = 87, UI_FIRESTORM = 88, UI_TROOPER = 89, UI_SENTINEL = 90,
    UI_REAPER = 91, UI_BARRAGER = 93,
    UI_WARFOLD = 41, UI_BREEDPOD = 42, UI_GENESAC = 43, UI_BROZAAR = 46,
    UI_XENOWORT = 47, UI_GRAY = 48, UI_SYDEMON = 50, UI_ATRIL = 51,
    UI_PODUPGRADE = 97, UI_GENEUPGRADE = 98, UI_SLOM = 71,
};

static int dc_ai_level(const level_t *map, int owner) {
    const dc_skirmish_t *setup = DC_LevelSkirmish(map);
    if (!setup || owner < 0 || owner >= 8 || D_PlayerIsHuman(owner)) return AI_LEVEL_NONE;
    switch (setup->players[owner].type) {
    case DC_PLAYER_AI: return AI_LEVEL_NORMAL;
    case DC_PLAYER_AI_PLUS: return AI_LEVEL_PLUS;
    default: return AI_LEVEL_NONE;
    }
}

static bool dc_ai_plan(const level_t *map, int owner, int level, AiPlan *out) {
    (void)level;
    if (!map || !out) return false;
    out->wave_interval_ms = 45000;
    out->wave_min_size = 6;
    out->wave_max_size = 16;
    if (DC_PlayerRace(owner) == 0) {
        P_AiPlanAdd(out, UI_EXPLOITER, 1);
        P_AiPlanAdd(out, UI_BARRACKS, 1);
        P_AiPlanAdd(out, UI_TROOPER, 3);
        P_AiPlanAdd(out, UI_EXPLOITER, 2);
        P_AiPlanAdd(out, UI_SCIPOD, 1);
        P_AiPlanAdd(out, UI_TROOPER, 6);
        P_AiPlanAdd(out, UI_ROBOFTR, 1);
        P_AiPlanAdd(out, UI_REAPER, 3);
        P_AiPlanAdd(out, UI_SENTINEL, 2);
        P_AiPlanAdd(out, UI_EXPLOITER, 3);
        P_AiPlanAdd(out, UI_SCIPOD2, 1);
        P_AiPlanAdd(out, UI_TROOPER, 10);
        P_AiPlanAdd(out, UI_REAPER, 6);
        P_AiPlanAdd(out, UI_ROBOFTR2, 1);
        P_AiPlanAdd(out, UI_BARRAGER, 2);
        P_AiPlanAdd(out, UI_FIRESTORM, 2);
        P_AiPlanAdd(out, UI_TROOPER, 16);
        P_AiPlanAdd(out, UI_REAPER, 10);
        P_AiPlanAdd(out, UI_SENTINEL, 6);
        P_AiPlanAdd(out, UI_BARRAGER, 4);
        P_AiPlanAdd(out, UI_FIRESTORM, 4);
        P_AiPlanAdd(out, UI_TROOPER, 24);
        P_AiPlanAdd(out, UI_REAPER, 16);
        P_AiPlanAdd(out, UI_SENTINEL, 12);
        P_AiPlanAdd(out, UI_BARRAGER, 8);
        P_AiPlanAdd(out, UI_FIRESTORM, 8);
        P_AiPlanAdd(out, UI_TROOPER, 36);
        P_AiPlanAdd(out, UI_REAPER, 24);
    } else {
        P_AiPlanAdd(out, UI_BROZAAR, 1);
        P_AiPlanAdd(out, UI_WARFOLD, 1);
        P_AiPlanAdd(out, UI_GRAY, 3);
        P_AiPlanAdd(out, UI_BROZAAR, 2);
        P_AiPlanAdd(out, UI_BREEDPOD, 1);
        P_AiPlanAdd(out, UI_GRAY, 6);
        P_AiPlanAdd(out, UI_GENESAC, 1);
        P_AiPlanAdd(out, UI_SYDEMON, 3);
        P_AiPlanAdd(out, UI_SLOM, 2);
        P_AiPlanAdd(out, UI_BROZAAR, 3);
        P_AiPlanAdd(out, UI_GRAY, 10);
        P_AiPlanAdd(out, UI_SYDEMON, 6);
        P_AiPlanAdd(out, UI_PODUPGRADE, 1);
        P_AiPlanAdd(out, UI_GENEUPGRADE, 1);
        P_AiPlanAdd(out, UI_ATRIL, 2);
        P_AiPlanAdd(out, UI_XENOWORT, 2);
        P_AiPlanAdd(out, UI_GRAY, 16);
        P_AiPlanAdd(out, UI_SYDEMON, 10);
        P_AiPlanAdd(out, UI_SLOM, 6);
        P_AiPlanAdd(out, UI_ATRIL, 4);
        P_AiPlanAdd(out, UI_GRAY, 24);
        P_AiPlanAdd(out, UI_SYDEMON, 16);
        P_AiPlanAdd(out, UI_SLOM, 12);
        P_AiPlanAdd(out, UI_ATRIL, 8);
        P_AiPlanAdd(out, UI_XENOWORT, 6);
        P_AiPlanAdd(out, UI_GRAY, 36);
        P_AiPlanAdd(out, UI_SYDEMON, 24);
    }
    return out->goal_count > 0;
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
    .plan = dc_ai_plan,
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
