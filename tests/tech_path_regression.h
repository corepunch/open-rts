#ifndef __TECH_PATH_REGRESSION__
#define __TECH_PATH_REGRESSION__
/* The tech-path planner (R_TechPath) from the requirement vectors of a game:
 * the includer names what its owner starts with and a deep-tech product with
 * the providers the owner must get, in order. Every product of the catalog is
 * also checked for a dependency-ordered, duplicate-free path, and a computer
 * player whose only goal is the deep product buys along that path.
 *   TECH_STARTERS   actor types owner 1 starts with
 *   TECH_CASES      { { target ui_id, count, { provider ui_id... } }, ... }
 * The includer defines tech_setup() for games that need a real map. */
#include "engine.h"
#include "info.h"
#include "t_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, g_game_id, #c)

typedef struct { int target, count, providers[10]; } techcase_t;
static const uint16_t starters[] = TECH_STARTERS;
static const techcase_t cases[] = TECH_CASES;
#define CASE_COUNT ((int)(sizeof(cases) / sizeof(*cases)))
#ifndef TECH_OWNER
#define TECH_OWNER 1
#endif
#define OWNER TECH_OWNER
#ifndef TECH_AI_TICKS
#define TECH_AI_TICKS 1200
#endif
#ifndef TECH_AI_PRELUDE
#define TECH_AI_PRELUDE
#endif
#ifndef TECH_AI_CASE
#define TECH_AI_CASE (CASE_COUNT - 1) /* the case the computer player is sent to */
#endif
#ifndef TECH_AI_STEP
#define TECH_AI_STEP() ((void)0)
#endif

#ifndef TECH_REAL_MAP
static mobj_t *spawn_owner(uint16_t type, int owner, fvec2_t position) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(fixed2_from_fvec2(position), 0), type);
    if (!unit) return NULL;
    unit->owner = unit->team = owner;
    unit->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    return unit;
}
#endif

#ifdef TECH_REAL_MAP
static int tech_setup(RtsGameModel **model); /* the includer loads its map */
#else
static int tech_setup(RtsGameModel **model) {
    *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root };
    CHECK(*model && rts_game_model_load(*model, &config));
    P_FreeLevel(&level);
    P_InitThinkers();
    level.width = level.height = 96;
    level.blocked = calloc(96 * 96, 1);
    CHECK(level.blocked && spawn_owner(starters[0], 0, (fvec2_t){10, 10}));
    for (size_t i = 0; i < sizeof(starters) / sizeof(*starters); ++i)
        CHECK(spawn_owner(starters[i], OWNER, (fvec2_t){20.0f + 6.0f * (float)i, 40}));
    return 0;
}
#endif

/* Every product's path lists a provider after everything that provider needs. */
static int test_all_paths(void) {
    StaticProductDefinition list[512];
    int n = G_ModelGetProducts(NULL, OWNER, list, 512), reachable = 0, deep = 0;
    for (int i = 0; i < n; ++i) {
        techstep_t steps[TECH_PATH_MAX];
        int count = R_TechPath(OWNER, &list[i], steps, TECH_PATH_MAX);
        if (count < 0) continue;
        ++reachable;
        deep += count >= 2;
        for (int s = 0; s < count; ++s) {
            CHECK(G_ModelProductByUIId(NULL, steps[s].wanted_by));
            for (int t = 0; t < s; ++t) CHECK(steps[t].product != steps[s].product || !steps[s].product);
            if (!steps[s].product) continue;
            const StaticProductDefinition *provider = G_ModelProductByUIId(NULL, steps[s].product);
            CHECK(provider);
            /* What the provider itself needs comes earlier. */
            techstep_t inner[TECH_PATH_MAX];
            int inner_count = R_TechPath(OWNER, provider, inner, TECH_PATH_MAX);
            CHECK(inner_count >= 0 && inner_count <= s);
            for (int k = 0; k < inner_count; ++k) {
                bool earlier = false;
                for (int t = 0; t < s; ++t) earlier |= steps[t].product == inner[k].product;
                CHECK(earlier);
            }
        }
    }
    CHECK(reachable > 0);
    CHECK(R_TechPath(OWNER, NULL, (techstep_t[1]){0}, 1) < 0);
    printf("  %d of %d products reachable, %d of them through two or more steps\n", reachable, n, deep);
    return 0;
}

static int test_cases(void) {
    for (int c = 0; c < CASE_COUNT; ++c) {
        const StaticProductDefinition *target = G_ModelProductByUIId(NULL, cases[c].target);
        CHECK(target);
        techstep_t steps[TECH_PATH_MAX];
        int count = R_TechPath(OWNER, target, steps, TECH_PATH_MAX);
        if (count != cases[c].count) fprintf(stderr, "%s: %d steps, wanted %d\n", target->label, count, cases[c].count);
        CHECK(count == cases[c].count);
        for (int s = 0; s < count; ++s) {
            if (steps[s].product != cases[c].providers[s])
                fprintf(stderr, "%s step %d: product %d, wanted %d\n", target->label, s, steps[s].product,
                        cases[c].providers[s]);
            CHECK(steps[s].product == cases[c].providers[s]);
        }
        techstep_t next;
        CHECK(R_TechNextStep(OWNER, target, &next) == (count > 0));
        if (count > 0) CHECK(next.product == steps[0].product);
        /* A path too small to hold the steps is not a path. */
        if (count > 1) CHECK(R_TechPath(OWNER, target, steps, count - 1) < 0);
    }
    return 0;
}

/* A computer player whose whole plan is one product buys the path to it. */
static int test_ai_follows_path(RtsGameModel *model) {
#ifndef TECH_MODEL_TICK
    (void)model;
#endif
    const techcase_t *deep = &cases[TECH_AI_CASE];
    /* TECH_AI_PRELUDE: what the product needs besides tech (a game with a supply cap wants a depot first). */
    AiStep goal[] = { TECH_AI_PRELUDE { deep->target, 1 } };
    faction_t only = { .name = "Planner", .opening = goal, .opening_count = (int)(sizeof(goal) / sizeof(*goal)),
        .wave_interval_ms = 3600000, .wave_min_size = 99, .wave_max_size = 99 };
    ruleset_t rules = g_ruleset;
    rules.factions = &only;
    rules.faction_count = 1;
    rules.faction_of = NULL;
    R_RulesOverride(&rules);
    level.player_resources[OWNER][0] = 1000000;
    for (int r = 1; r < RTS_MAX_RESOURCES; ++r) level.player_resources[OWNER][r] = 100000;
#ifdef TECH_MODEL_TICK
    /* The model's own tick drives the map's computer players. */
    AiContext *ai_ptr = rts_game_model_ai(model);
    CHECK(ai_ptr);
#else
    AiContext ai_local;
    P_AiInit(&ai_local);
    P_AiAttachGame(&ai_local, G_AiInterface());
    P_AiSetFeatures(&ai_local, AI_FEATURE_ECONOMY | AI_FEATURE_PRODUCTION | AI_FEATURE_RESEARCH);
    AiContext *ai_ptr = &ai_local;
#endif
    const StaticProductDefinition *target = G_ModelProductByUIId(NULL, deep->target);
    int order[32], bought = 0;
    bool done = false, ordered = false;
    for (int tick = 0; tick < TECH_AI_TICKS && !done; ++tick) {
#ifdef TECH_MODEL_TICK
        CHECK(rts_tick(model, NULL));
        RtsGameEvent skip;
        while (rts_game_model_poll_event(model, &skip)) {}
#else
        mobjlist_t list = P_ListMobjs();
        P_AiTick(ai_ptr, &level, list.items, list.count, gameinfo, 1000);
        P_FreeMobjList(&list);
        G_ProductionTicker(1000);
        TECH_AI_STEP();
#endif
        AiEvent event;
        while (P_AiPollEvent(ai_ptr, &event))
            if (event.type == AI_EVENT_PURCHASE && event.owner == OWNER && bought < 32) {
                order[bought++] = event.value;
                if (event.value == target->ui_id) ordered = true;
            }
        /* Research leaves no actor behind: the order for it is the goal met. */
        done = target->product_class == RTS_PRODUCT_UPGRADE ? ordered :
            G_ModelHasActorType(NULL, OWNER, G_ModelActorIdForProduct(target));
    }
    R_RulesOverride(NULL);
    if (!done) {
        const AiTeamState *team = &ai_ptr->teams[OWNER];
        fprintf(stderr, "team level %d plan %d goals %d thinks %d purchases %d research %d\n", team->level,
                team->plan_loaded, team->plan.goal_count, team->stats.thinks, team->stats.purchases,
                team->stats.research_orders);
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            const mobj_t *u = (const mobj_t *)th;
            if (th->function != P_MobjThinker) continue;
            if (u->owner != OWNER || u->remove) { fprintf(stderr, " o%d", u->owner); continue; }
            fprintf(stderr, " [%d hp%d%s%s]", u->type_id, u->hp, u->production ? " prod" : "", G_ModelHasActorType(NULL, OWNER, u->type_id) ? " ready" : "");
        }
        fprintf(stderr, "\ncan_purchase %d avail %d producer %p owned %d\n", P_AiCanPurchase(ai_ptr, &level, OWNER, target->ui_id),
                G_ModelProductAvailable(NULL, OWNER, target), (void *)G_FindProducerBelow(OWNER, target, AI_QUEUE_DEPTH), P_AiOwned(ai_ptr, OWNER, target->ui_id));
        fprintf(stderr, "AI bought %d:", bought);
        for (int i = 0; i < bought; ++i) fprintf(stderr, " %d", order[i]);
        fprintf(stderr, "\n");
    }
    CHECK(done);
    /* The deep product, and every provider on its path, was bought, and in order. */
    int at = -1;
    for (int s = 0; s < deep->count; ++s) {
        int found = -1;
        for (int i = at + 1; i < bought && found < 0; ++i) if (order[i] == deep->providers[s]) found = i;
        if (found < 0) fprintf(stderr, "provider %d was not bought after step %d\n", deep->providers[s], s);
        CHECK(found > at);
        at = found;
    }
    printf("  AI reached %s through %d purchases\n", target->label ? target->label : "?", bought);
    return 0;
}

int main(void) {
    if (getenv("TECH_DUMP")) {
        RtsGameModel *m = NULL;
        if (tech_setup(&m)) return 1;
        StaticProductDefinition list[512];
        int n = G_ModelGetProducts(NULL, OWNER, list, 512);
        for (int i = 0; i < n; ++i) {
            techstep_t st[TECH_PATH_MAX];
            int c = R_TechPath(OWNER, &list[i], st, TECH_PATH_MAX);
            printf("%d %s: %d:", list[i].ui_id, list[i].label ? list[i].label : "?", c);
            for (int k = 0; k < c; ++k) printf(" %d%s", st[k].product, st[k].pending ? "p" : "");
            puts("");
        }
        return 0;
    }
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RtsGameModel *model = NULL;
    RTS_RUN(tech_setup(&model));
    RTS_RUN(test_all_paths());
    RTS_RUN(test_cases());
    RTS_RUN(test_ai_follows_path(model));
    P_FreeLevel(&level);
    rts_game_model_destroy(model);
    SDL_Quit();
    printf("PASS: %s tech paths follow the requirement vectors and the AI buys along them\n", g_game_id);
    return 0;
}
#endif
