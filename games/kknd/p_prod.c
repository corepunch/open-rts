#define _DEFAULT_SOURCE
#include "game.h"
#include "d_net.h"
#include "g_game.h"
#include "info.h"
#include "kknd.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* OpenKrush economy: one authored row owns cost, producer, duration and tech. */
static const StaticProductDefinition KKND_PRODUCTS[] = {
#define KK_PRODUCT(id,faction,category,kind,type,maker,cost,ticks,tech,limit,label) \
    {id,id,label,cost,0,kind,type,faction,{maker},1,{maker},1},
#include "products.inc"
#undef KK_PRODUCT
    {0}, /* Sentinel also permits an empty native product table in C11. */
};
static const struct { int id, ticks, level, limit; } rules[] = {
#define KK_PRODUCT(id,faction,category,kind,type,maker,cost,ticks,tech,limit,label) {id,ticks,tech,limit},
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

/* Engine skirmish goals for either faction; makers determine eligibility. */
static const productiongoal_t kk_ai_goals[] = {
    { 25, 1 }, { 126, 1 },   /* Research */
    { 23, 1 }, { 123, 1 },   /* Infantry production */
    { 124, 1 },              /* Evolved vehicles */
    { 21, 1 }, { 121, 1 },   /* Vehicle production */
    { 14, 1 }, { 114, 1 },   /* Oil tanker */
    { 1, 2 }, { 101, 2 },    /* Infantry */
    { 2, 1 }, { 102, 1 },    /* Flame infantry */
    { 3, 1 }, { 104, 1 },    /* Rockets */
    { 10, 1 }, { 110, 1 },   /* Fast vehicle */
    { 12, 1 }, { 113, 1 },   /* Heavy vehicle */
};

void G_ModelAIProduction(RtsGameModel *model, int elapsed_ms) {
    (void)model; (void)elapsed_ms;
    G_ProductionGoals(kk_ai_goals, sizeof(kk_ai_goals) / sizeof(*kk_ai_goals));
    for (int owner = 1; owner < RTS_MODEL_MAX_PLAYERS; ++owner) {
        if (D_PlayerIsHuman(owner)) continue;
        for (unsigned i = 0; i < sizeof(kk_ai_goals)/sizeof(*kk_ai_goals); ++i) {
            const StaticProductDefinition *p = G_ModelProductByUIId(NULL, kk_ai_goals[i].ui_id);
            if (!p) continue;
            if (G_CountPlannedActors(owner, p->product_type) >= kk_ai_goals[i].count) continue;
            for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
                if (th->function != P_MobjThinker) continue;
                mobj_t *u = (mobj_t *)th;
                if (u->owner != owner || u->type_id != p->makers[0] || G_ModelProducerHasTech(u,p)) continue;
                bool researching = false;
                for (thinker_t *other = thinkercap.next; other != &thinkercap; other = other->next)
                    if (other->function == P_MobjThinker && ((mobj_t *)other)->research.target == u->id)
                        researching = true;
                if (!researching) KK_Research(u);
            }
        }
    }
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
