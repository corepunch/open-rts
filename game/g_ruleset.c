#include "engine.h"

#include <string.h>

/* Engine side of the per-game ruleset (g_ruleset, defined in
 * games/<id>/rules.c): faction plans, requirement vectors and the map
 * override patch. Nothing here names a game. */

rulepatchset_t g_rulepatch;

/* ── policy ───────────────────────────────────────────────────────────── */

static const ruleset_t *rules_override;

const ruleset_t *R_Rules(void) { return rules_override ? rules_override : &g_ruleset; }
void R_RulesOverride(const ruleset_t *rules) { rules_override = rules; }

const rulepolicy_t *R_Policy(void) { return &R_Rules()->policy; }

/* ── factions ─────────────────────────────────────────────────────────── */

const faction_t *R_OwnerFaction(const level_t *map, int owner) {
    const ruleset_t *rules = R_Rules();
    if (!rules->factions || rules->faction_count <= 0) return NULL;
    int index = rules->faction_of ? rules->faction_of(map, owner) : 0;
    return index >= 0 && index < rules->faction_count ? &rules->factions[index] : NULL;
}

int R_StartPlacements(const faction_t *faction, ivec2_t anchor, startplace_t *out, int max) {
    int written = 0;
    for (int i = 0; faction && i < faction->start_unit_count; ++i) {
        const startunit_t *unit = &faction->start_units[i];
        for (int n = 0; n < unit->count && written < max; ++n)
            out[written++] = (startplace_t){ .type = unit->type, .at = {
                anchor.x + unit->offset.x + n * unit->step.x, anchor.y + unit->offset.y + n * unit->step.y } };
    }
    return written;
}

bool R_FactionPlan(const faction_t *faction, int level, AiPlan *out) {
    if (!faction || !out) return false;
    out->wave_interval_ms = faction->wave_interval_ms;
    out->wave_min_size = faction->wave_min_size;
    out->wave_max_size = faction->wave_max_size;
    const AiDoctrine *doctrine = level > 0 && level < AI_LEVEL_COUNT ?
        faction->level_doctrine[level] : NULL;
    out->doctrine = doctrine ? *doctrine : faction->doctrine;
    for (int i = 0; i < faction->opening_count; ++i)
        P_AiPlanAdd(out, faction->opening[i].product, faction->opening[i].count);
    return out->goal_count > 0 || out->doctrine.roster_count > 0;
}

bool R_OwnerPlan(const level_t *map, int owner, int level, AiPlan *out) {
    const faction_t *faction = R_OwnerFaction(map, owner);
    if (!R_FactionPlan(faction, level, out)) return false;
    const ruleset_t *rules = R_Rules();
    int index = rules->variant_of ? rules->variant_of(map, owner) : -1;
    if (index < 0 || index >= faction->variant_count) return true;
    const factionvariant_t *variant = &faction->variants[index];
    if (variant->opening_count > 0) {
        out->goal_count = 0;
        for (int i = 0; i < variant->opening_count; ++i)
            P_AiPlanAdd(out, variant->opening[i].product, variant->opening[i].count);
    }
    AiDoctrine *d = &out->doctrine;
    for (int i = 0; i < variant->roster_count; ++i) {
        int j = 0;
        while (j < d->roster_count && d->roster[j].product != variant->roster[i].product) ++j;
        if (j < d->roster_count) d->roster[j].weight = variant->roster[i].weight;
        else if (d->roster_count < AI_MAX_ROSTER) d->roster[d->roster_count++] = variant->roster[i];
    }
    if (variant->defenses) d->defenses = variant->defenses;
    return true;
}

/* ── requirement vectors ──────────────────────────────────────────────── */

int R_ProductRequirements(const StaticProductDefinition *product, requirement_t *out, int max) {
    int count = 0;
    if (!product) return 0;
    for (int i = 0; i < product->prerequisite_count && count < max; ++i)
        out[count++] = (requirement_t){ .kind = REQ_BUILDING, .id = product->prerequisites[i] };
    const ruleset_t *rules = R_Rules();
    for (int i = 0; i < rules->requirement_count && count < max; ++i)
        if (rules->requirements[i].product == product->ui_id) out[count++] = rules->requirements[i].req;
    return count;
}

bool R_RowsMet(int owner, const StaticProductDefinition *product) {
    const ruleset_t *rules = R_Rules();
    for (int i = 0; product && i < rules->requirement_count; ++i)
        if (rules->requirements[i].product == product->ui_id && rules->requirements[i].req.kind != REQ_TECH &&
            !R_RequirementMet(owner, product, &rules->requirements[i].req)) return false;
    return true;
}

bool R_RequirementMet(int owner, const StaticProductDefinition *product, const requirement_t *req) {
    const ruleset_t *rules = R_Rules();
    switch (req->kind) {
    case REQ_BUILDING:
        return R_PrerequisiteMet(owner, req->id);
    case REQ_UPGRADE:
        return rules->upgrade_level && rules->upgrade_level(owner, req->id) >= req->level;
    case REQ_TECH:
        if (!product || !rules->tech_level) return false;
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            const mobj_t *unit = (const mobj_t *)th;
            if (unit->owner != owner || unit->hp <= 0 || unit->remove) continue;
            for (int m = 0; m < product->maker_count; ++m)
                if (product->makers[m] == unit->type_id && rules->tech_level(unit) >= req->level)
                    return true;
        }
        return false;
    }
    return false;
}

mobj_t *R_ProducerLackingTech(int owner, const StaticProductDefinition *product) {
    requirement_t reqs[RTS_MODEL_MAX_PRODUCT_PREREQUISITES + 8];
    int count = R_ProductRequirements(product, reqs, (int)(sizeof(reqs) / sizeof(*reqs)));
    if (!product || !R_Rules()->tech_level) return NULL;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (unit->owner != owner || unit->hp <= 0 || unit->remove) continue;
        bool maker = false;
        for (int m = 0; m < product->maker_count; ++m) maker |= product->makers[m] == unit->type_id;
        if (!maker) continue;
        int level = R_Rules()->tech_level(unit);
        for (int i = 0; i < count; ++i)
            if (reqs[i].kind == REQ_TECH && level >= 0 && level < reqs[i].level) return unit;
    }
    return NULL;
}

/* ── products ─────────────────────────────────────────────────────────── */

void R_ProductCosts(const StaticProductDefinition *product, int *out) {
    memset(out, 0, RTS_MAX_RESOURCES * sizeof(*out));
    if (!product) return;
    if (R_Rules()->product_costs) { R_Rules()->product_costs(product, out); return; }
    out[0] = product->cost;
    for (int r = 1; r < RTS_MAX_RESOURCES; ++r) out[r] = product->extra_costs[r - 1];
}

bool R_CanAfford(int owner, const StaticProductDefinition *product) {
    int cost[RTS_MAX_RESOURCES];
    if (!product || owner < 0 || owner >= 8) return false;
    R_ProductCosts(product, cost);
    for (int r = 0; r < RTS_MAX_RESOURCES; ++r)
        if (cost[r] < 0 || level.player_resources[owner][r] < cost[r]) return false;
    return true;
}

static void product_fill(const StaticProductDefinition *def, product_t *out) {
    *out = (product_t){ .def = def, .ui_id = def->ui_id, .kind = def->product_class,
        .time_ms = G_ModelProductTrainingTimeMs(def), .producers = def->makers, .producer_count = def->maker_count,
        .prerequisites = def->prerequisites, .prerequisite_count = def->prerequisite_count };
    R_ProductCosts(def, out->cost);
}

int R_ProductCount(int owner) {
    StaticProductDefinition list[512];
    return G_ModelGetProducts(NULL, owner, list, (int)(sizeof(list) / sizeof(*list)));
}

bool R_ProductAt(int owner, int index, product_t *out) {
    StaticProductDefinition list[512];
    int count = G_ModelGetProducts(NULL, owner, list, (int)(sizeof(list) / sizeof(*list)));
    if (index < 0 || index >= count || !out) return false;
    const StaticProductDefinition *def = G_ModelProductByUIId(NULL, list[index].ui_id);
    if (!def) return false;
    product_fill(def, out);
    return true;
}

bool R_ProductByUiId(int ui_id, product_t *out) {
    const StaticProductDefinition *def = G_ModelProductByUIId(NULL, ui_id);
    if (!def || !out) return false;
    product_fill(def, out);
    return true;
}

/* ── tech path ────────────────────────────────────────────────────────── */

static const StaticProductDefinition *product_for_actor(int owner, uint16_t id);

const StaticProductDefinition *R_PrerequisiteProduct(int owner, int id) {
    return R_Rules()->prerequisite_product ? R_Rules()->prerequisite_product(id) :
           product_for_actor(owner, (uint16_t)id);
}

bool R_PrerequisiteMet(int owner, int id) {
    if (R_Rules()->prerequisite_met) return R_Rules()->prerequisite_met(owner, id);
    return G_ModelHasActorType(NULL, owner, (uint16_t)id);
}

/* The catalog product that makes actor `id` (a unit or a structure). */
static const StaticProductDefinition *product_for_actor(int owner, uint16_t id) {
    static StaticProductDefinition catalog[512];
    int count = G_ModelGetProducts(NULL, owner, catalog, (int)(sizeof(catalog) / sizeof(*catalog)));
    for (int i = 0; i < count; ++i)
        if (catalog[i].product_class != RTS_PRODUCT_UPGRADE && G_ModelActorIdForProduct(&catalog[i]) == id)
            return G_ModelProductByUIId(NULL, catalog[i].ui_id);
    return NULL;
}

static const StaticProductDefinition *product_for_upgrade(int owner, int upgrade, int level) {
    if (R_Rules()->upgrade_product) return G_ModelProductByUIId(NULL, R_Rules()->upgrade_product(upgrade, level));
    static StaticProductDefinition catalog[512];
    int count = G_ModelGetProducts(NULL, owner, catalog, (int)(sizeof(catalog) / sizeof(*catalog)));
    for (int i = 0; i < count; ++i)
        if (catalog[i].product_class == RTS_PRODUCT_UPGRADE && catalog[i].product_type == upgrade)
            return G_ModelProductByUIId(NULL, catalog[i].ui_id);
    return NULL;
}

/* Whether the owner has `product` queued or under construction. */
static bool product_under_way(int owner, const StaticProductDefinition *product) {
    if (product->product_class != RTS_PRODUCT_UPGRADE)
        return G_CountPlannedActors(owner, G_ModelActorIdForProduct(product)) > 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (unit->owner == owner && unit->hp > 0 && !unit->remove && unit->production &&
            unit->production->queue_count > 0 && unit->production->product_class == RTS_PRODUCT_UPGRADE &&
            unit->production->product_type == product->product_type) return true;
    }
    return false;
}

typedef struct {
    int owner;
    techstep_t *out;
    int max, count;
    int stack[16], depth;
    bool fail;
} techpath_t;

static void path_visit(techpath_t *path, const StaticProductDefinition *product);

static void path_step(techpath_t *path, requirement_t req, const StaticProductDefinition *provider,
                      const StaticProductDefinition *wanted_by) {
    for (int i = 0; i < path->count; ++i)
        if (provider ? path->out[i].product == provider->ui_id :
            path->out[i].req.kind == req.kind && path->out[i].req.id == req.id &&
            path->out[i].req.level == req.level) return;
    if (path->count >= path->max) { path->fail = true; return; }
    path->out[path->count++] = (techstep_t){ .req = req, .product = provider ? provider->ui_id : 0,
        .wanted_by = wanted_by->ui_id, .pending = provider && product_under_way(path->owner, provider) };
}

/* Provides `req` for `wanted_by` through `provider`: its own path first, then the step. */
static void path_provide(techpath_t *path, requirement_t req, const StaticProductDefinition *provider,
                         const StaticProductDefinition *wanted_by) {
    if (!provider) { path->fail = true; return; }
    path_visit(path, provider);
    if (!path->fail) path_step(path, req, provider, wanted_by);
}

static void path_visit(techpath_t *path, const StaticProductDefinition *product) {
    if (path->fail) return;
    for (int i = 0; i < path->depth; ++i)
        if (path->stack[i] == product->ui_id) { path->fail = true; return; }
    if (path->depth >= (int)(sizeof(path->stack) / sizeof(*path->stack))) { path->fail = true; return; }
    path->stack[path->depth++] = product->ui_id;

    requirement_t reqs[RTS_MODEL_MAX_PRODUCT_PREREQUISITES + 8];
    int count = R_ProductRequirements(product, reqs, (int)(sizeof(reqs) / sizeof(*reqs)));
    for (int i = 0; i < count && !path->fail; ++i) {
        const requirement_t *req = &reqs[i];
        if (req->kind == REQ_TECH || R_RequirementMet(path->owner, product, req)) continue;
        path_provide(path, *req, req->kind == REQ_BUILDING ?
                     R_PrerequisiteProduct(path->owner, req->id) :
                     product_for_upgrade(path->owner, req->id, req->level), product);
    }
    /* Something must be there to make it: any one of the makers. */
    bool made = product->maker_count <= 0;
    for (int m = 0; m < product->maker_count && !made; ++m)
        made = G_ModelHasActorType(NULL, path->owner, (uint16_t)product->makers[m]);
    if (!made && !path->fail) {
        const StaticProductDefinition *maker = NULL;
        int id = 0;
        for (int m = 0; m < product->maker_count && !maker; ++m) {
            id = product->makers[m];
            maker = product_for_actor(path->owner, (uint16_t)id);
            for (int i = 0; maker && i < path->depth; ++i)
                if (path->stack[i] == maker->ui_id) maker = NULL;
        }
        path_provide(path, (requirement_t){ .kind = REQ_BUILDING, .id = id }, maker, product);
    }
    /* A tech level is raised on a producer that stands: after the makers. */
    for (int i = 0; i < count && !path->fail; ++i)
        if (reqs[i].kind == REQ_TECH && !R_RequirementMet(path->owner, product, &reqs[i]) &&
            R_ProducerLackingTech(path->owner, product))
            path_step(path, reqs[i], NULL, product);
    --path->depth;
}

int R_TechPath(int owner, const StaticProductDefinition *target, techstep_t *out, int max) {
    if (!target || !out || max <= 0) return -1;
    techpath_t path = { .owner = owner, .out = out, .max = max };
    path_visit(&path, target);
    return path.fail ? -1 : path.count;
}

bool R_TechNextStep(int owner, const StaticProductDefinition *target, techstep_t *step) {
    techstep_t steps[TECH_PATH_MAX];
    if (R_TechPath(owner, target, steps, TECH_PATH_MAX) <= 0) return false;
    *step = steps[0];
    return true;
}

/* ── map override patch ───────────────────────────────────────────────── */

bool R_PatchAdd(rulepatchset_t *set, int table, int row, int field, int32_t value) {
    if (!set || set->count >= RULEPATCH_MAX) return false;
    set->entries[set->count++] = (rulepatch_t){
        .table = (uint16_t)table, .row = (uint16_t)row, .field = (uint16_t)field, .value = value };
    return true;
}

/* Every value a patch wrote, with what the checked-in table held. */
static struct { int *slot; int old; } undo[2 * RULEPATCH_MAX];
static int undo_count;

bool R_PatchSet(int *slot, int value) {
    if (undo_count >= (int)(sizeof(undo) / sizeof(*undo))) return false;
    undo[undo_count].slot = slot;
    undo[undo_count++].old = *slot;
    *slot = value;
    return true;
}

void R_PatchApply(const rulepatchset_t *set) {
    const ruleset_t *rules = R_Rules();
    while (undo_count > 0) {
        --undo_count;
        *undo[undo_count].slot = undo[undo_count].old;
    }
    if (set != &g_rulepatch) {
        if (set) g_rulepatch = *set;
        else g_rulepatch.count = 0;
    }
    if (rules->patch_apply)
        for (int i = 0; i < g_rulepatch.count; ++i) rules->patch_apply(&g_rulepatch.entries[i]);
    if (rules->patch_done) rules->patch_done();
}

uint32_t R_PatchHash(uint32_t hash) {
    hash = G_HashValue(hash, (uint32_t)g_rulepatch.count);
    for (int i = 0; i < g_rulepatch.count; ++i) {
        const rulepatch_t *e = &g_rulepatch.entries[i];
        hash = G_HashValue(hash, ((uint32_t)e->table << 16) | e->row);
        hash = G_HashValue(hash, e->field);
        hash = G_HashValue(hash, (uint32_t)e->value);
    }
    return hash;
}
