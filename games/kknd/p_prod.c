#define _DEFAULT_SOURCE
#include "game.h"
#include "d_net.h"
#include "g_game.h"
#include "info.h"
#include "kknd.h"
#include "p_ai.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* OpenKrush economy: one authored row owns cost, producer, duration and tech. */
static const StaticProductDefinition KKND_PRODUCTS[] = {
#define KK_PRODUCT(id,faction,category,kind,type,maker,cost,ticks,tech,limit,label) \
    {(id),(id),(label),(cost),0,(kind),(type),(faction),{(maker)},1,{(maker)},1},
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
    int button_index = 0;
    for (int i = 0; i < product_count(); ++i) {
        const StaticProductDefinition *product = &KKND_PRODUCTS[i];
        bool is_maker = false;
        for (int m = 0; m < product->maker_count; ++m)
            if (product->makers[m] == (int)producer_type) { is_maker = true; break; }
        if (!is_maker) continue;
        int by = gameui->command_grid.y + button_index * gameui->icon_size.h;
        button_index++;
        bool available = G_ModelProductAvailable(model, consoleplayer, product) &&
                         snapshot->player_resources[consoleplayer][0] >= product->cost;
        append_ui_script(dst, dst_size, "x 864 y %d btn %d enabled %d pic %d\n",
                         by, product->ui_id, available ? 1 : 0, product->icon_frame);
    }
}

/* One ladder for both factions: {Survivor id, Mutant id, count}. */
static const struct { int survivor, mutant, count; } kk_ai_ladder[] = {
    { 40, 41, 1 },  /* Outpost / Clan hall, unpacked from the mobile outpost */
    { 42, 43, 1 },  /* Machine shop / Blacksmith */
    { 32, 33, 1 },  /* Oil tanker */
    { 0,  1,  3 },  /* Rifleman / Berserker */
    { 47, 48, 1 },  /* Research lab / Alchemy hall */
    { 32, 33, 2 },
    { 16, 17, 2 },  /* Dirt bike / Dire wolf */
    { 12, 13, 2 },  /* RPG launcher / Bazooka */
    { 49, 50, 1 },  /* Guard tower / Machinegun nest */
    { 18, 19, 2 },  /* 4x4 pickup / Bike and sidecar */
    { 0,  1,  6 },
    { 20, 21, 2 },  /* ATV / Monster truck */
    { 14, 15, 2 },  /* Sniper / Crazy Harry */
    { 24, 25, 2 },  /* Anaconda / War mastodon */
    { 51, 52, 1 },  /* Missile battery / Grapeshot tower */
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

static bool kk_ai_plan(const level_t *map, int owner, int level, AiPlan *out) {
    (void)map; (void)level;
    int faction = kk_faction(owner);
    if (faction < 0) return false;
    out->wave_interval_ms = 40000;
    out->wave_min_size = 6;
    out->wave_max_size = 16;
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

static int kk_ai_can_purchase(const level_t *map, int owner, int ui_id) {
    int status = G_AiCatalogCanPurchase(map, owner, ui_id);
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    if (status == AI_BUY_BLOCKED && product && ui_id != KKND_RESEARCH &&
        kk_maker_lacking_tech(owner, product)) return AI_BUY_NEED_TECH;
    return status;
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
    .owned = G_AiCatalogOwned,
    .can_purchase = kk_ai_can_purchase,
    .purchase = G_AiCatalogPurchase,
    .develop = kk_ai_develop,
    .is_anchor = G_AiIsStructure,
};

const AiGameInterface *G_AiInterface(void) { return &kk_ai_interface; }
