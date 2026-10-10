#ifndef __AI_DOCTRINE_REGRESSION__
#define __AI_DOCTRINE_REGRESSION__
#include "engine.h"
#include "info.h"
#include "t_local.h"
#include <stdlib.h>
#ifdef RTS_GAME_KKND
void A_KkndResearch(mobj_t *actor);
#endif

/* A game's doctrine, headless, on an empty 96x96 level. The includer names
 * its units: DOCTRINE_BASE_A and _B list the standing base of a computer of
 * each faction, DOCTRINE_ANCHOR is a structure that gives faction A a base,
 * DOCTRINE_WEAK and DOCTRINE_STRONG are that faction's fighters and
 * DOCTRINE_ENEMY is the human's. */

#define DCHECK(c) RTS_CHECK(c, g_game_id, #c)

typedef struct {
    uint16_t type;
    uint32_t has, lacks; /* AiRole masks */
} doctrine_role_t;

static void doctrine_level(void) {
    P_FreeLevel(&level);
    P_InitThinkers();
    level.width = level.height = 96;
    level.blocked = calloc(96 * 96, 1);
}

static mobj_t *doctrine_spawn(uint16_t type, int owner, fixed2_t at) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(at, 0), type);
    if (unit) {
        unit->owner = unit->team = owner;
        unit->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    }
    return unit;
}

static void doctrine_attach(AiContext *ai, uint32_t features) {
    P_AiInit(ai);
    P_AiAttachGame(ai, G_AiInterface());
    P_AiSetFeatures(ai, features);
}

enum { DOCTRINE_INCOME = 30 }; /* Credits a second: less than the producers can spend. */
static int doctrine_income;

/* The AI, the production ticker and the computers' income; no thinkers,
 * so nothing moves or fights. */
static void doctrine_run(AiContext *ai, int ticks, int dt_ms) {
    for (int tick = 0; tick < ticks; ++tick) {
        for (int owner = 1; owner < 3; ++owner)
            level.player_resources[owner][0] += doctrine_income * dt_ms / 1000;
        mobjlist_t list = P_ListMobjs();
        P_AiTick(ai, &level, list.items, list.count, gameinfo, dt_ms);
        P_FreeMobjList(&list);
        G_ProductionTicker(dt_ms);
#ifdef RTS_GAME_KKND
        for (int tic = 0; tic < RTS_TICRATE * dt_ms / 1000; ++tic)
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
                if (th->function == P_MobjThinker) A_KkndResearch((mobj_t *)th);
#endif
    }
}

static int doctrine_roles(const doctrine_role_t *cases, int count) {
    AiContext ai;
    doctrine_attach(&ai, AI_FEATURE_ALL);
    for (int i = 0; i < count; ++i) {
        AiUnitInfo info;
        P_AiUnitInfo(&ai, cases[i].type, &info);
        if ((info.roles & cases[i].has) != cases[i].has || (info.roles & cases[i].lacks)) {
            fprintf(stderr, "type %d roles %x\n", cases[i].type, info.roles);
            DCHECK(!"roles");
        }
    }
    return 0;
}

typedef struct {
    int fighters, workers, defenses, strength;
    int opening;  /* fighters the opening ladder keeps */
    int top;      /* owned of the heaviest-weighted fighter */
    int most;     /* owned of the most numerous fighter */
    int skipped;  /* weighted fighters buyable now but never bought */
} doctrine_army_t;

/* The most of `product` the opening ladder keeps. */
static int ladder_goal(const AiPlan *plan, int product) {
    int most = 0;
    for (int i = 0; i < plan->goal_count; ++i)
        if (plan->goals[i].product == product && plan->goals[i].count > most) most = plan->goals[i].count;
    return most;
}

/* What a computer owns of its roster, alive plus queued. */
static int doctrine_census(const AiContext *ai, int owner, doctrine_army_t *out) {
    const AiTeamState *team = &ai->teams[owner];
    const AiDoctrine *d = &team->plan.doctrine;
    DCHECK(team->plan_loaded && d->roster_count > 0);
    memset(out, 0, sizeof(*out));
    int top = -1;
    for (int i = 0; i < d->roster_count; ++i) {
        AiUnitInfo info;
        int actor = G_AiCatalogActor(d->roster[i].product);
        DCHECK(actor > 0);
        P_AiUnitInfo(ai, (uint16_t)actor, &info);
        int n = P_AiOwned(ai, owner, d->roster[i].product);
        if (info.roles & AI_ROLE_WORKER) out->workers += n;
        if (info.roles & AI_ROLE_DEFENSE) out->defenses += n;
        if (!(info.roles & AI_ROLE_FIGHTER)) continue;
        out->fighters += n;
        out->strength += n * info.ground_strength;
        out->opening += ladder_goal(&team->plan, d->roster[i].product);
        if (top < 0 || d->roster[i].weight > d->roster[top].weight) { top = i; out->top = n; }
        if (n > out->most) out->most = n;
        int status = P_AiCanPurchase(ai, &level, owner, d->roster[i].product);
        if (d->roster[i].weight > 0 && n == 0 && (status == AI_BUY_OK || status == AI_BUY_NEED_CREDITS))
            ++out->skipped;
    }
    printf("  owner %d: fighters %d (opening %d, cap %d, strength %d), workers %d, defenses %d, "
           "top weight %d of %d, skipped %d\n", owner, out->fighters, out->opening, d->army_cap,
           out->strength, out->workers, out->defenses, out->top, out->most, out->skipped);
    return 0;
}

static void doctrine_bases(int copies) {
    static const uint16_t base_a[] = { DOCTRINE_BASE_A }, base_b[] = { DOCTRINE_BASE_B };
    for (int c = 0; c < copies; ++c) {
        for (unsigned i = 0; i < sizeof(base_a) / sizeof(*base_a); ++i)
            doctrine_spawn(base_a[i], 1, FIXED2_LIT(10 + 6 * (float)i, 12 + 10 * (float)c));
        for (unsigned i = 0; i < sizeof(base_b) / sizeof(*base_b); ++i)
            doctrine_spawn(base_b[i], 2, FIXED2_LIT(10 + 6 * (float)i, 60 + 10 * (float)c));
    }
    /* A modest income: credits, not free queues, limit the army. */
    level.player_resources[1][0] = level.player_resources[2][0] = 20000;
    doctrine_income = DOCTRINE_INCOME;
}

/* After the opening the doctrine keeps workers and defenses per town and
 * grows the army past the opening's units up to army_cap, then stops
 * spending. Faction A is owner 1, faction B owner 2. */
static int doctrine_mix(doctrine_army_t *a, doctrine_army_t *b) {
    doctrine_level();
    DCHECK(level.blocked);
    doctrine_bases(1);
    AiContext ai;
    doctrine_attach(&ai, AI_FEATURE_ECONOMY | AI_FEATURE_PRODUCTION | AI_FEATURE_RESEARCH | AI_FEATURE_DOCTRINE);
    doctrine_run(&ai, 1800, 1000);
    doctrine_army_t *armies[] = { a, b };
    for (int owner = 1; owner < 3; ++owner) {
        doctrine_army_t *army = armies[owner - 1];
        const AiTeamState *team = &ai.teams[owner];
        const AiDoctrine *d = &team->plan.doctrine;
        int towns = team->towns > 0 ? team->towns : 1;
        RTS_RUN(doctrine_census(&ai, owner, army));
        DCHECK(army->fighters >= d->army_cap && army->fighters > army->opening);
        DCHECK(army->fighters <= d->army_cap + army->opening);
        DCHECK(army->workers >= d->workers * towns);
        DCHECK(army->defenses >= d->defenses * towns);
    }
    int money1 = level.player_resources[1][0], money2 = level.player_resources[2][0];
    doctrine_run(&ai, 60, 1000);
    DCHECK(level.player_resources[1][0] == money1 + DOCTRINE_INCOME * 60);
    DCHECK(level.player_resources[2][0] == money2 + DOCTRINE_INCOME * 60);
    doctrine_income = 0;
    P_FreeLevel(&level);
    return 0;
}

/* The doctrine alone, with a producer free for every roster entry: the
 * heaviest weight is the most numerous, and nothing buyable is left out. */
static int doctrine_weights(void) {
    doctrine_level();
    doctrine_bases(3);
    AiContext ai;
    doctrine_attach(&ai, AI_FEATURE_ECONOMY | AI_FEATURE_PRODUCTION | AI_FEATURE_RESEARCH | AI_FEATURE_DOCTRINE);
    for (int owner = 1; owner < 3; ++owner) {
        AiTeamState *team = &ai.teams[owner];
        team->level = G_AiInterface()->player_level(&level, owner);
        team->plan_loaded = R_OwnerPlan(&level, owner, team->level, &team->plan);
        team->plan.goal_count = 0;
    }
    doctrine_run(&ai, 1800, 1000);
    for (int owner = 1; owner < 3; ++owner) {
        doctrine_army_t army;
        RTS_RUN(doctrine_census(&ai, owner, &army));
        DCHECK(army.fighters >= ai.teams[owner].plan.doctrine.army_cap);
        DCHECK(army.top == army.most && army.skipped == 0);
    }
    doctrine_income = 0;
    P_FreeLevel(&level);
    return 0;
}

static int army_value(const AiContext *ai, int owner, uint16_t type) {
    int total = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function != P_MobjThinker || u->owner != owner || u->type_id != type) continue;
        AiUnitInfo info;
        P_AiUnitInfo(ai, type, &info);
        total += info.ground_strength;
    }
    return total;
}

/* A wave waits while the idle army is weaker than attack_ratio percent of
 * the enemy army it has seen, and sets out once reinforcements tip it,
 * before the wave reaches its size cap. */
static int doctrine_waves(void) {
    doctrine_level();
    DCHECK(doctrine_spawn(DOCTRINE_ANCHOR, 1, FIXED2_LIT(16, 16)));
    DCHECK(doctrine_spawn(DOCTRINE_ANCHOR, 0, FIXED2_LIT(80, 80)));
    AiPlan plan = {0};
    DCHECK(R_OwnerPlan(&level, 1, AI_LEVEL_NORMAL, &plan));
    const AiDoctrine *d = &plan.doctrine;
    DCHECK(d->attack_ratio > 0 && plan.wave_min_size > 0 && plan.wave_max_size > plan.wave_min_size);
    for (int i = 0; i < plan.wave_min_size; ++i) {
        DCHECK(doctrine_spawn(DOCTRINE_WEAK, 1, FIXED2_LIT(20 + i, 20)));
        DCHECK(doctrine_spawn(DOCTRINE_ENEMY, 0, FIXED2_LIT(60 + i, 60)));
    }
    AiContext ai;
    doctrine_attach(&ai, AI_FEATURE_ATTACK);
    int weak = army_value(&ai, 1, DOCTRINE_WEAK), enemy = army_value(&ai, 0, DOCTRINE_ENEMY);
    DCHECK((int64_t)weak * 100 < (int64_t)enemy * d->attack_ratio);
    doctrine_run(&ai, (plan.wave_interval_ms + 20000) / 100, 100);
    const AiStats *stats = P_AiStats(&ai, 1);
    DCHECK(ai.teams[1].enemy_strength >= enemy / 2);
    DCHECK(stats->waves == 0 && stats->holds > 0);
    int army = plan.wave_min_size;
    while (stats->waves == 0 && army < plan.wave_max_size - 1) {
        DCHECK(doctrine_spawn(DOCTRINE_STRONG, 1, FIXED2_LIT(20 + army - plan.wave_min_size, 22)));
        ++army;
        doctrine_run(&ai, 20, 100);
    }
    printf("  waves: held %d thinks against %d (ratio %d%%), launched with %d units\n",
           stats->holds, ai.teams[1].enemy_strength, d->attack_ratio, army);
    DCHECK(stats->waves == 1 && stats->wave_units == army && army < plan.wave_max_size);
    P_FreeLevel(&level);
    return 0;
}

#endif
