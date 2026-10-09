#define _DEFAULT_SOURCE
#include "engine.h"
#include "info.h"
#include "kknd.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* OpenKrush economy: one authored row owns cost, producer, duration and tech. */
static const StaticProductDefinition KKND_PRODUCTS[] = {
#define KK_PRODUCT(id,faction,category,kind,type,maker,cost,ticks,tech,limit,label) \
    { (id),(id),(label),(cost),0,(kind),(type),(faction),{(maker)},1,{(maker)},1, {0}, false },
#include "products.inc"
#undef KK_PRODUCT
    {0}, /* Sentinel also permits an empty native product table in C11. */
};
static const struct { int id, ticks, level, limit; } rules[] = {
#define KK_PRODUCT(id,faction,category,kind,type,maker,cost,ticks,tech,limit,label) \
    {(id),(ticks),(tech),(limit)},
#include "products.inc"
#undef KK_PRODUCT
    {0},
};
static const StaticProductDefinition research_product = {
    .ui_id = KKND_RESEARCH, .label = "Research", .cost = 0,
};
static int product_count(void) {
    return (int)(sizeof(KKND_PRODUCTS) / sizeof(KKND_PRODUCTS[0])) - 1;
}

int G_ModelGetProducts(const RtsGameModel *model, int owner,
                       StaticProductDefinition *out, int max_products) {
    (void)model;
    if (!out || max_products <= 0) return 0;
    int faction = -1;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap && faction < 0; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *u = (mobj_t *)th;
        if (u->owner != owner || u->hp <= 0 || u->remove) continue;
        for (int i = 0; i < product_count(); ++i)
            if (KKND_PRODUCTS[i].product_type == u->type_id) { faction = KKND_PRODUCTS[i].faction; break; }
    }
    int count = 0;
    for (int i = 0; i < product_count() && count < max_products; ++i)
        if (faction < 0 || faction == KKND_PRODUCTS[i].faction) out[count++] = KKND_PRODUCTS[i];
    return count;
}

const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id) {
    (void)model;
    if (ui_id == KKND_RESEARCH) return &research_product;
    for (int i = 0; i < product_count(); ++i)
        if (KKND_PRODUCTS[i].ui_id == ui_id) return &KKND_PRODUCTS[i];
    return NULL;
}

const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class,
                                                         int product_type) {
    (void)model;
    for (int i = 0; i < product_count(); ++i)
        if ((int)KKND_PRODUCTS[i].product_class == product_class &&
            KKND_PRODUCTS[i].product_type == product_type)
            return &KKND_PRODUCTS[i];
    return NULL;
}

bool G_ModelProducerHasTech(const mobj_t *producer, const StaticProductDefinition *product) {
    for (int i = 0; i < product_count(); ++i)
        if (rules[i].id == product->ui_id) return producer->research.level >= rules[i].level;
    return false;
}

int KK_NextTechLevel(const mobj_t *actor) {
    int next = 0;
    if ((actor->type_id == MT_SURV_OUTPOST || actor->type_id == MT_MUTE_CLANHALL) && actor->research.level < 2)
        next = actor->research.level + 1; /* Radar and enemy radar. */
    if (actor->type_id == MT_SURV_RESEARCH_LAB || actor->type_id == MT_MUTE_ALCHEMY_HALL)
        return actor->research.level < 5 ? actor->research.level + 1 : 0;
    for (int i = 0; i < product_count(); ++i)
        if (KKND_PRODUCTS[i].makers[0] == actor->type_id && rules[i].level > actor->research.level &&
            (!next || rules[i].level < next)) next = rules[i].level;
    return next;
}

bool G_ModelProductAvailable(const RtsGameModel *model, int owner,
                             const StaticProductDefinition *product) {
    if (!product) return false;
    for (int i = 0; i < product_count(); ++i)
        if (rules[i].id == product->ui_id && rules[i].limit &&
            G_CountPlannedActors(owner, product->product_type) >= rules[i].limit) return false;
    if (product->ui_id == KKND_RESEARCH)
        return G_ModelHasActorType(model, owner, MT_SURV_RESEARCH_LAB) ||
               G_ModelHasActorType(model, owner, MT_MUTE_ALCHEMY_HALL);
    for (int i = 0; i < product->prerequisite_count; ++i)
        if (!G_ModelHasActorType(model, owner, product->prerequisites[i])) return false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *u = (mobj_t *)th;
        if (u->owner != owner || u->hp <= 0 || u->remove || gameinfo->states[u->core.state_id].group == 6) continue;
        for (int i = 0; i < product->maker_count; ++i)
            if (product->makers[i] == u->type_id && G_ModelProducerHasTech(u, product)) return true;
    }
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
    if (!product) return 0;
    for (int i = 0; i < product_count(); ++i)
        if (rules[i].id == product->ui_id) return rules[i].ticks * 40;
    return 0;
}

static bool kk_is_drillrig(uint16_t type) {
    return type == MT_SURV_DRILLRIG || type == MT_MUTE_DRILLRIG;
}

static bool kk_is_derrick(uint16_t type) {
    return type == MT_SURV_MOBILE_DERRICK || type == MT_MUTE_MOBILE_DERRICK;
}

static const mobjtype_t *kk_actor_type(uint16_t type) {
    for (int i = 0; i < num_actor_types; ++i)
        if (actor_types[i].id == type) return &actor_types[i];
    return NULL;
}

/* The Drill Rig product is the mobile derrick deploying: once its build time
 * has run, the derrick becomes the rig where it stands (like A_Deploy) instead
 * of a rig spawning beside it. Returning true tells the ticker the release
 * has been handled. */
bool G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                   const StaticProductDefinition *product,
                                   uint16_t actor_id) {
    (void)model; (void)product;
    if (!producer || !kk_is_derrick(producer->type_id) || !kk_is_drillrig(actor_id)) return false;
    const mobjtype_t *rig = kk_actor_type(actor_id);
    if (!rig) return false;
    P_FreeMobjProduction(producer);
    P_ClearMove(producer);
    producer->movement.order_id = 0;
    producer->attack.target = NULL;
    producer->core.momentum = fixed3_zero();
    producer->core.sprite_name[0] = '\0';
    producer->speed = 0.0f;
    producer->max_hp = rig->max_hp;
    producer->hp = rig->max_hp;
    producer->radius = 1.2f;
    P_ApplyActorTypeDefaults(producer, rig);
    P_SetMobjState(producer, gameinfo->mobjinfo[actor_id].spawnstate);
    return true;
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
    append_ui_script(dst, dst_size, "ui kknd 1\n");
    append_ui_script(dst, dst_size, "x 400 y 3 text \"Oil %d\"\n",
                     snapshot->player_resources[consoleplayer][0]);

    int selected_idx = -1;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (snapshot->units[i].selected && snapshot->units[i].owner == consoleplayer) {
            selected_idx = i;
            break;
        }
    }
    if (selected_idx < 0) return;

    uint16_t producer_type = snapshot->units[selected_idx].type_id;
    for (int i = 0; i < product_count(); ++i) {
        const StaticProductDefinition *product = &KKND_PRODUCTS[i];
        bool is_maker = false;
        for (int m = 0; m < product->maker_count; ++m)
            if (product->makers[m] == (int)producer_type) { is_maker = true; break; }
        if (!is_maker) continue;
        /* Every button at the top of the product grid. */
        int by = 32;
        bool available = G_ModelProductAvailable(model, consoleplayer, product) &&
                         snapshot->player_resources[consoleplayer][0] >= product->cost;
        append_ui_script(dst, dst_size, "x 864 y %d btn %d enabled %d pic %d\n",
                         by, product->ui_id, available ? 1 : 0, product->icon_frame);
    }
}

/* One ladder for both factions: {Survivor id, Mutant id, count}. Income needs
 * the whole oil loop: a power station to unload at, a drill rig (bought as a
 * mobile derrick that deploys, see kk_ai_owned) and tankers. */
static const struct { int survivor, mutant, count; } kk_ai_ladder[] = {
    { 40, 41, 1 },  /* Outpost / Clan hall, unpacked from the mobile outpost */
    { 42, 43, 1 },  /* Machine shop / Blacksmith */
    { 38, 39, 1 },  /* Power station */
    { 55, 56, 1 },  /* Drill rig */
    { 32, 33, 1 },  /* Oil tanker */
    { 0,  1,  3 },  /* Rifleman / Berserker */
    { 32, 33, 2 },
    { 47, 48, 1 },  /* Research lab / Alchemy hall */
    { 16, 17, 2 },  /* Dirt bike / Dire wolf */
    { 12, 13, 2 },  /* RPG launcher / Bazooka */
    { 49, 50, 1 },  /* Guard tower / Machinegun nest */
    { 18, 19, 2 },  /* 4x4 pickup / Bike and sidecar */
    { 0,  1,  6 },
    { 20, 21, 2 },  /* ATV / Monster truck */
    { 14, 15, 2 },  /* Sniper / Crazy Harry */
    { 24, 25, 2 },  /* Anaconda / War mastodon */
    { 51, 52, 1 },  /* Missile battery / Grapeshot tower */
    { 55, 56, 2 },
    { 32, 33, 4 },
    { 28, 29, 2 },  /* Autocannon / Missile crab */
    { 0,  1,  10 },
    { 24, 25, 4 },
    { 26, 27, 2 },  /* Barrage craft / Giant beetle */
    { 28, 29, 4 },
    { 22, 23, 2 },  /* Flame ATV / Giant scorpion */
};

/* Ids pair up by faction except the Mutant-only rows, so read the faction
 * from the catalog entry of any building or unit the owner already has. */
static int kk_faction(int owner) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *u = (mobj_t *)th;
        if (u->owner != owner || u->hp <= 0 || u->remove) continue;
        for (int i = 0; i < product_count(); ++i)
            if (KKND_PRODUCTS[i].product_type == u->type_id) return KKND_PRODUCTS[i].faction;
    }
    return -1;
}

/* Survivors fight with guns and engines: riflemen and rockets screen the
 * 4x4s, ATVs and heavy tanks, behind towers, and attack once they match
 * the enemy, falling back early to repair. The Evolved swarm with cheap
 * berserkers and beasts, attack sooner and fight to the end. KKnD has no
 * supply, so army_cap bounds the army. */
static const AiDoctrine survivor_doctrine = {
    .workers = 3, .defenses = 2, .army_cap = 40, .counter = 50,
    .attack_ratio = 110, .retreat_ratio = 50,
    .roster = { {32,0},{49,0},{51,0},{53,0},
                {0,15},{12,10},{4,5},{14,5},{16,5},{18,15},{20,15},{24,20},{28,10},{26,10} },
    .roster_count = 14,
};
static const AiDoctrine evolved_doctrine = {
    .workers = 3, .defenses = 1, .army_cap = 40, .counter = 50,
    .attack_ratio = 80, .retreat_ratio = 30,
    .roster = { {33,0},{50,0},{52,0},{54,0},
                {1,30},{13,10},{15,5},{17,20},{19,5},{21,5},{23,10},{25,15},{27,10},{29,5} },
    .roster_count = 14,
};

static bool kk_ai_plan(const level_t *map, int owner, int level, AiPlan *out) {
    (void)map; (void)level;
    int faction = kk_faction(owner);
    if (faction < 0) return false;
    out->wave_interval_ms = 40000;
    out->wave_min_size = 6;
    out->wave_max_size = 16;
    out->doctrine = faction == 1 ? survivor_doctrine : evolved_doctrine;
    for (unsigned i = 0; i < sizeof(kk_ai_ladder) / sizeof(*kk_ai_ladder); ++i)
        P_AiPlanAdd(out, faction == 1 ? kk_ai_ladder[i].survivor : kk_ai_ladder[i].mutant,
                    kk_ai_ladder[i].count);
    return true;
}

/* First owned maker of `product` that has not reached the product's tech. */
static mobj_t *kk_maker_lacking_tech(int owner, const StaticProductDefinition *product) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *u = (mobj_t *)th;
        if (u->owner != owner || u->hp <= 0 || u->remove ||
            gameinfo->states[u->core.state_id].group == 6) continue;
        for (int i = 0; i < product->maker_count; ++i)
            if (product->makers[i] == u->type_id && !G_ModelProducerHasTech(u, product)) return u;
    }
    return NULL;
}

/* The derrick that deploys into a rig of `ui_id`, or 0. */
static int kk_derrick_for_rig(int ui_id) {
    const StaticProductDefinition *rig = G_ModelProductByUIId(NULL, ui_id);
    if (!rig || !kk_is_drillrig((uint16_t)rig->product_type)) return 0;
    for (int i = 0; i < product_count(); ++i)
        if (KKND_PRODUCTS[i].product_type == rig->makers[0]) return KKND_PRODUCTS[i].ui_id;
    return 0;
}

static int kk_queued(int owner, uint16_t actor_type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == owner && !u->remove && u->hp > 0 &&
            u->production && u->production->actor_id == actor_type) n += u->production->queue_count;
    }
    return n;
}

/* A rig goal counts rigs (alive or deploying) plus derricks still in the
 * shop's queue. An idle derrick does not count, so the goal goes on to order
 * its deployment. */
static int kk_ai_owned(int owner, int ui_id) {
    int derrick = kk_derrick_for_rig(ui_id);
    const StaticProductDefinition *product = derrick ? G_ModelProductByUIId(NULL, derrick) : NULL;
    return G_AiCatalogOwned(owner, ui_id) +
           (product ? kk_queued(owner, (uint16_t)product->product_type) : 0);
}

static int kk_ai_can_purchase(const level_t *map, int owner, int ui_id) {
    int derrick = kk_derrick_for_rig(ui_id);
    if (derrick && G_AiCatalogCanPurchase(map, owner, ui_id) == AI_BUY_BLOCKED)
        return G_AiCatalogCanPurchase(map, owner, derrick);
    int status = G_AiCatalogCanPurchase(map, owner, ui_id);
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    if (status == AI_BUY_BLOCKED && product && ui_id != KKND_RESEARCH &&
        kk_maker_lacking_tech(owner, product)) return AI_BUY_NEED_TECH;
    return status;
}

static bool ready(const mobj_t *u) {
    return u && !u->remove && u->hp > 0 && gameinfo->states[u->core.state_id].group != 6;
}
static bool is_lab(const mobj_t *u) {
    return u->type_id == MT_SURV_RESEARCH_LAB || u->type_id == MT_MUTE_ALCHEMY_HALL;
}

bool KK_Research(mobj_t *target) {
    if (!ready(target)) return false;
    int next = KK_NextTechLevel(target);
    mobj_t *lab = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *u = (mobj_t *)th;
        if (!ready(u) || u->owner != target->owner || !is_lab(u)) continue;
        if (u->research.target == target->id) {
            u->research.target = 0; /* Cancel: paid research is not refunded. */
            return true;
        }
        if (!u->research.target && !lab) lab = u;
    }
    if (!lab || !next) return false;
    static const int rates[] = {100,90,80,70,60,50};
    int rate = rates[lab->research.level];
    lab->research.target = target->id;
    lab->research.next_level = next;
    /* Preserve the reference's arithmetic order: only the per-level term is discounted. */
    lab->research.total_cost = lab->research.remaining_cost = 250 + 500 * next * rate / 100;
    lab->research.total_time = lab->research.remaining_time = 400 + 300 * next * rate / 100;
    lab->research.clock = 0;
    return true;
}

void A_KkndResearch(mobj_t *lab) {
    if (!lab->research.target) return;
    mobj_t *target = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *u = (mobj_t *)th;
        if (u->id == lab->research.target) { target = u; break; }
    }
    if (!ready(target) || target->owner != lab->owner) {
        lab->research.target = 0;
        return;
    }
    /* Reference normal speed is 25 tics/s; keep exact progress at our 30 Hz. */
    lab->research.clock += 25;
    if (lab->research.clock < RTS_TICRATE) return;
    lab->research.clock -= RTS_TICRATE;
    int remaining = lab->research.remaining_time == 1 ? 0 :
        lab->research.total_cost * lab->research.remaining_time / lab->research.total_time;
    int cost = lab->research.remaining_cost - remaining;
    if (level.player_resources[lab->owner][0] < cost) return;
    level.player_resources[lab->owner][0] -= cost;
    lab->research.remaining_cost -= cost;
    if (--lab->research.remaining_time == 0) {
        target->research.level = lab->research.next_level;
        lab->research.target = 0;
    }
}

/* A lab researches one producer at a time; KK_Research on a producer that is
 * already being researched would cancel it, so check first. */
static bool kk_ai_develop(level_t *map, int owner, int ui_id) {
    (void)map;
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    mobj_t *maker = product ? kk_maker_lacking_tech(owner, product) : NULL;
    if (!maker) return false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t *)th)->research.target == maker->id)
            return true; /* Already under way: keep waiting. */
    return KK_Research(maker);
}

static bool kk_ai_purchase(level_t *map, int owner, int ui_id) {
    int derrick = kk_derrick_for_rig(ui_id);
    if (derrick && G_AiCatalogCanPurchase(map, owner, ui_id) == AI_BUY_BLOCKED)
        return G_AiCatalogPurchase(map, owner, derrick);
    return G_AiCatalogPurchase(map, owner, ui_id);
}

bool G_PlayerBuildProduct(mobj_t *producer, const StaticProductDefinition *product) {
    return product && product->ui_id == KKND_RESEARCH ? KK_Research(producer) : G_QueueProduct(producer, product);
}

int G_ModelRadarLevel(int owner) {
    int radar = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *u = (mobj_t *)th;
        if (u->owner == owner && !u->remove && u->hp > 0 &&
            gameinfo->states[u->core.state_id].group != 6 &&
            (u->type_id == MT_SURV_OUTPOST || u->type_id == MT_MUTE_CLANHALL) &&
            u->research.level > radar) radar = u->research.level;
    }
    return radar;
}

static const AiGameInterface kk_ai_interface = {
    .name = "kknd",
    .features = AI_FEATURE_ALL,
    .player_level = P_AiLevelNonHuman,
    .plan = kk_ai_plan,
    .owned = kk_ai_owned,
    .can_purchase = kk_ai_can_purchase,
    .purchase = kk_ai_purchase,
    .develop = kk_ai_develop,
    .is_anchor = G_AiIsStructure,
    .product_actor = G_AiCatalogActor,
};

const AiGameInterface *G_AiInterface(void) { return &kk_ai_interface; }
