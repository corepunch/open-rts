/* Universal AI engine rules, exercised through a mock game interface so the
 * scheduling, planner, toggles, waves and event log are tested independently
 * of any retail data. */
#include "mobj_test.h"
#include "engine.h"
#include "../rts_test.h"
#include "../../play/p_ai.h"
#include "../../play/p_local.h"

#include <string.h>

static int fail(const char *message) { return rts_fail("ai_interface", message); }
#define REQUIRE(cond, msg) do { if (!(cond)) return fail(msg); } while (0)

enum { PRODUCTS = 8 };
static struct {
    int owned[PRODUCTS];
    int status[PRODUCTS];
    int order[256];
    int order_count;
    int plan_calls, plan_level;
    bool owner4;
    AiPlan plan;
} mock;

static int mock_level(const level_t *map, int owner) {
    (void)map;
    return owner == 3 ? AI_LEVEL_NORMAL : owner == 4 && mock.owner4 ? AI_LEVEL_PLUS : AI_LEVEL_NONE;
}
static bool mock_plan(const level_t *map, int owner, int level, AiPlan *out) {
    (void)map; (void)owner;
    mock.plan_calls++;
    mock.plan_level = level;
    *out = mock.plan;
    return out->goal_count > 0;
}
static int mock_owned(int owner, int product) { (void)owner; return mock.owned[product]; }
static int mock_can(const level_t *map, int owner, int product) {
    (void)map; (void)owner; return mock.status[product];
}
static bool mock_purchase(level_t *map, int owner, int product) {
    (void)map; (void)owner;
    if (mock.order_count < 256) mock.order[mock.order_count] = product;
    mock.order_count++;
    mock.owned[product]++;
    return true;
}

static const AiGameInterface mock_game = {
    .name = "mock", .features = AI_FEATURE_ALL, .player_level = mock_level,
    .plan = mock_plan, .owned = mock_owned, .can_purchase = mock_can,
    .purchase = mock_purchase,
};

static level_t map;
static mobj_t *units[16];
static int unit_count;

static void reset(void) {
    memset(&mock, 0, sizeof(mock));
    memset(&map, 0, sizeof(map));
    map.width = map.height = 100;
    P_FreeThinkers();
    unit_count = 0;
}
static mobj_t *add(int owner, uint32_t traits, int x, int y, int allegiance) {
    mobj_t *u = spawn_mobj_fixture((mobj_t){0});
    u->owner = owner; u->hp = 100; u->traits = traits; u->allegiance = allegiance;
    u->core.position = (fixed3_t){ x << 16, y << 16, 0 };
    u->movement.order_arrived = true;
    units[unit_count++] = u;
    return u;
}
static void goal(int product, int count, int after_ms) {
    mock.plan.goals[mock.plan.goal_count++] = (AiGoal){ product, count, after_ms };
}
static void run(AiContext *ctx, int ticks) {
    for (int i = 0; i < ticks; ++i) P_AiTick(ctx, &map, units, unit_count, NULL, 33);
}

static int test_cadence_and_levels(void) {
    reset();
    mock.owner4 = true;
    goal(1, 1, 0);
    add(3, MF_MOBILE, 10, 10, ALLEGIANCE_PLAYER);
    add(4, MF_MOBILE, 20, 10, ALLEGIANCE_PLAYER);
    add(1, MF_MOBILE, 30, 10, ALLEGIANCE_ENEMY);
    AiContext ctx; P_AiInit(&ctx); P_AiAttachGame(&ctx, &mock_game);
    run(&ctx, 4);
    REQUIRE(P_AiStats(&ctx, 3)->thinks == 1 && P_AiStats(&ctx, 4)->thinks == 1,
            "an AI owner thinks once per four ticks (DC.EXE 0x419a54 cadence)");
    run(&ctx, 36);
    REQUIRE(P_AiStats(&ctx, 3)->thinks == 10 && P_AiStats(&ctx, 4)->thinks == 10,
            "forty ticks give ten thinks per AI owner");
    REQUIRE(P_AiStats(&ctx, 1)->thinks == 0 && P_AiStats(&ctx, 0)->thinks == 0,
            "AI_LEVEL_NONE owners never think");
    REQUIRE(ctx.teams[3].level == AI_LEVEL_NORMAL && ctx.teams[4].level == AI_LEVEL_PLUS,
            "AI level is recorded per owner");
    int on_same_tick = 0;
    AiContext c2; P_AiInit(&c2); P_AiAttachGame(&c2, &mock_game);
    for (int i = 0; i < 40; ++i) {
        int a = P_AiStats(&c2, 3)->thinks, b = P_AiStats(&c2, 4)->thinks;
        P_AiTick(&c2, &map, units, unit_count, NULL, 33);
        if (P_AiStats(&c2, 3)->thinks > a && P_AiStats(&c2, 4)->thinks > b) on_same_tick++;
    }
    REQUIRE(on_same_tick == 0, "owners are staggered so no tick thinks for every player");
    return 0;
}

static int test_planner(void) {
    reset();
    goal(1, 1, 0); goal(2, 1, 0); goal(3, 2, 0);
    add(3, MF_MOBILE, 10, 10, ALLEGIANCE_PLAYER);
    AiContext ctx; P_AiInit(&ctx); P_AiAttachGame(&ctx, &mock_game);

    mock.status[1] = AI_BUY_NEED_CREDITS; mock.status[2] = AI_BUY_OK; mock.status[3] = AI_BUY_OK;
    run(&ctx, 40);
    REQUIRE(mock.order_count == 0, "a goal short of credits blocks cheaper lower-priority goals (saving)");

    mock.status[1] = AI_BUY_OK;
    run(&ctx, 4);
    REQUIRE(mock.order_count >= 1 && mock.order[0] == 1, "highest-priority affordable goal is bought first");
    run(&ctx, 40);
    REQUIRE(mock.owned[1] == 1 && mock.owned[2] == 1 && mock.owned[3] == 2,
            "every goal is filled exactly to its count, never beyond");
    int total = mock.order_count;
    run(&ctx, 40);
    REQUIRE(mock.order_count == total, "a satisfied ladder buys nothing");
    REQUIRE(P_AiStats(&ctx, 3)->purchases == total, "stats count every purchase");

    mock.owned[2] = 0; mock.status[2] = AI_BUY_BLOCKED; mock.owned[3] = 0;
    int before = mock.order_count;
    run(&ctx, 8);
    REQUIRE(mock.order_count > before && mock.owned[2] == 0 && mock.owned[3] > 0,
            "blocked goals are skipped and lost units are replaced");
    return 0;
}

static int test_per_think_cap_and_timing(void) {
    reset();
    goal(1, 50, 0); goal(2, 50, 0); goal(3, 50, 0);
    add(3, MF_MOBILE, 10, 10, ALLEGIANCE_PLAYER);
    AiContext ctx; P_AiInit(&ctx); P_AiAttachGame(&ctx, &mock_game);
    run(&ctx, 4);
    REQUIRE(mock.order_count == 2 && mock.order[0] == 1 && mock.order[1] == 2,
            "a think buys along the ladder, at most two goals, highest priority first");

    reset();
    goal(1, 1, 1000);
    add(3, MF_MOBILE, 10, 10, ALLEGIANCE_PLAYER);
    P_AiInit(&ctx); P_AiAttachGame(&ctx, &mock_game);
    run(&ctx, 20); /* 660 ms */
    REQUIRE(mock.order_count == 0, "after_ms delays a goal");
    run(&ctx, 20);
    REQUIRE(mock.order_count == 1, "goal is bought once its time has come");
    return 0;
}

static int test_features_and_replan(void) {
    reset();
    goal(1, 1, 0);
    add(3, MF_MOBILE, 10, 10, ALLEGIANCE_PLAYER);
    AiContext ctx; P_AiInit(&ctx); P_AiAttachGame(&ctx, &mock_game);
    P_AiSetFeatures(&ctx, AI_FEATURE_ALL & ~AI_FEATURE_PRODUCTION);
    run(&ctx, 40);
    REQUIRE(mock.order_count == 0, "PRODUCTION off: no purchases");
    P_AiSetFeatures(&ctx, AI_FEATURE_ALL);
    run(&ctx, 8);
    REQUIRE(mock.order_count == 1 && mock.plan_calls == 1, "features toggle at runtime");
    REQUIRE(mock.plan_level == AI_LEVEL_NORMAL, "the planner receives the AI level");

    P_AiAttachGame(&ctx, &mock_game);
    mock.owned[1] = 0;
    run(&ctx, 8);
    REQUIRE(mock.plan_calls == 2, "attaching a game invalidates cached plans");
    return 0;
}

static int test_event_log(void) {
    reset();
    goal(1, 1000, 0);
    add(3, MF_MOBILE, 10, 10, ALLEGIANCE_PLAYER);
    AiContext ctx; P_AiInit(&ctx); P_AiAttachGame(&ctx, &mock_game);
    run(&ctx, 4);
    AiEvent e;
    REQUIRE(P_AiPollEvent(&ctx, &e) && e.type == AI_EVENT_PURCHASE && e.owner == 3 && e.value == 1,
            "purchase events carry owner and product");
    REQUIRE(!P_AiPollEvent(&ctx, &e), "polling drains the log");
    run(&ctx, 4 * 400);
    REQUIRE(ctx.event_count == AI_EVENT_LOG_SIZE && ctx.events_dropped > 0,
            "the ring keeps the newest entries and counts drops");
    REQUIRE(P_AiStats(&ctx, 3)->purchases == mock.order_count,
            "stats stay exact even when the log overflows");
    return 0;
}

static int test_waves(void) {
    reset();
    mock.plan.wave_min_size = 3; mock.plan.wave_max_size = 4; mock.plan.wave_interval_ms = 5000;
    goal(1, 0, 0);
    add(3, MF_RESOURCE_BASE, 10, 10, ALLEGIANCE_PLAYER);
    mobj_t *enemy = add(1, MF_RESOURCE_BASE, 60, 60, ALLEGIANCE_ENEMY);
    mobj_t *fighters[6];
    for (int i = 0; i < 2; ++i) fighters[i] = add(3, MF_ATTACK | MF_MOBILE, 11 + i, 11, ALLEGIANCE_PLAYER);
    AiContext ctx; P_AiInit(&ctx); P_AiAttachGame(&ctx, &mock_game);
    P_AiSetFeatures(&ctx, AI_FEATURE_ATTACK);
    run(&ctx, 40);
    REQUIRE(P_AiStats(&ctx, 3)->waves == 0, "the first wave waits one plan interval");
    run(&ctx, 160); /* interval elapsed, but only two fighters */
    REQUIRE(P_AiStats(&ctx, 3)->waves == 0, "no wave below the plan's minimum army");
    for (int i = 2; i < 6; ++i) fighters[i] = add(3, MF_ATTACK | MF_MOBILE, 11 + i, 11, ALLEGIANCE_PLAYER);
    run(&ctx, 8);
    REQUIRE(P_AiStats(&ctx, 3)->waves == 1 && P_AiStats(&ctx, 3)->wave_units == 4,
            "a wave launches once the minimum is met and is capped by the plan");
    int targeted = 0;
    for (int i = 0; i < 6; ++i) targeted += fighters[i]->attack.target == enemy;
    REQUIRE(targeted == 4, "wave units are pointed at the nearest enemy base");
    AiEvent e; bool seen = false;
    while (P_AiPollEvent(&ctx, &e)) seen |= e.type == AI_EVENT_WAVE_LAUNCHED && e.value == 4;
    REQUIRE(seen, "a wave event is logged");
    run(&ctx, 40); /* 1.3 s: still inside the 5 s interval */
    REQUIRE(P_AiStats(&ctx, 3)->waves == 1, "waves respect the plan's interval");
    return 0;
}

static int test_defense(void) {
    reset();
    goal(1, 0, 0);
    add(3, MF_RESOURCE_BASE, 10, 10, ALLEGIANCE_PLAYER);
    mobj_t *guard = add(3, MF_ATTACK | MF_MOBILE, 12, 10, ALLEGIANCE_PLAYER);
    mobj_t *intruder = add(1, MF_ATTACK | MF_MOBILE, 14, 12, ALLEGIANCE_ENEMY);
    add(1, MF_ATTACK | MF_MOBILE, 90, 90, ALLEGIANCE_ENEMY);
    AiContext ctx; P_AiInit(&ctx); P_AiAttachGame(&ctx, &mock_game);
    P_AiSetFeatures(&ctx, AI_FEATURE_DEFENSE);
    run(&ctx, 8);
    REQUIRE(guard->attack.target == intruder, "idle fighters rally on an intruder near the base");
    REQUIRE(P_AiStats(&ctx, 3)->defense_rallies >= 1, "rallies are counted");
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_cadence_and_levels();
    rc |= test_planner();
    rc |= test_per_think_cap_and_timing();
    rc |= test_features_and_replan();
    rc |= test_event_log();
    rc |= test_waves();
    rc |= test_defense();
    if (!rc) puts("PASS: ai_interface");
    return rc;
}
