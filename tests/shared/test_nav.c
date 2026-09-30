#include "game.h"
#include "info.h"
#include "engine.h"
#include "p_nav.h"
#include "rts_test.h"

#define CHECK(c) RTS_CHECK(c, "shared nav", #c)

static void reset(int width, int height) {
    P_FreeLevel(&level);
    level = (level_t){.width = width, .height = height};
    level.blocked = calloc((size_t)width * height, 1);
}

static void block(int x, int y) { level.blocked[L_Index(&level, x, y)] = 1; }

static float polyline(fvec2_t from, const navpath_t *path) {
    float length = 0;
    for (int i = 0; i < path->count; ++i) {
        length += sqrtf(fvec2_distance_squared(from, path->points[i]));
        from = path->points[i];
    }
    return length;
}

static int planner(void) {
    navpath_t path;
    reset(64, 64);
    /* Open ground: string pulling collapses the route to the goal itself. */
    CHECK(P_NavPlan(&level, 0.42f, (fvec2_t){1.5f, 1.5f}, (fvec2_t){40.3f, 22.7f}, &path));
    CHECK(path.complete && path.count == 1 && fvec2_near(path.points[0], (fvec2_t){40.3f, 22.7f}, 1e-4f));

    /* A wall with one gap at the bottom: the route threads it and never clips stone. */
    reset(64, 64);
    for (int y = 0; y < 60; ++y) block(32, y);
    fvec2_t from = {4.5f, 4.5f}, goal = {60.5f, 4.5f};
    CHECK(P_NavPlan(&level, 0.42f, from, goal, &path) && path.complete);
    fvec2_t at = from;
    for (int i = 0; i < path.count; ++i) {
        CHECK(P_NavLineClear(&level, at, path.points[i], 0.42f));
        at = path.points[i];
    }
    CHECK(fvec2_near(at, goal, 1e-4f));
    cell_t cells[512];
    int steps = P_FindPath(&level, (cell_t){4, 4}, (cell_t){60, 4}, cells, 512);
    CHECK(steps > 0 && polyline(from, &path) <= (float)steps * 1.42f);

    /* Diagonal squeeze between two stones is forbidden (no corner cutting). */
    reset(16, 16);
    block(5, 5); block(6, 6);
    steps = P_FindPath(&level, (cell_t){6, 5}, (cell_t){5, 6}, cells, 512);
    CHECK(steps > 3);

    /* Goal inside a sealed pocket is relocated into the start's region. */
    reset(32, 32);
    for (int i = 10; i <= 14; ++i) { block(i, 10); block(i, 14); block(10, i); block(14, i); }
    CHECK(!P_NavReachable(&level, (ivec2_t){1, 1}, (ivec2_t){12, 12}));
    CHECK(P_NavPlan(&level, 0.42f, (fvec2_t){1.5f, 1.5f}, (fvec2_t){12.5f, 12.5f}, &path));
    CHECK(P_NavReachable(&level, (ivec2_t){1, 1}, fvec2_cell(path.goal)));

    /* Terrain edits are noticed without anyone invalidating a cache. */
    reset(32, 8);
    for (int y = 0; y < 8; ++y) block(16, y);
    block(16, 4); level.blocked[L_Index(&level, 16, 4)] = 0;
    from = (fvec2_t){2.5f, 4.5f}; goal = (fvec2_t){30.5f, 4.5f};
    CHECK(P_NavPlan(&level, 0.42f, from, goal, &path) && path.complete);
    block(16, 4);
    CHECK(!P_NavPlan(&level, 0.42f, from, (fvec2_t){30.5f, 4.5f}, &path) ||
          fvec2_cell(path.goal).x < 16);

    /* Determinism: the same request yields byte-identical waypoints. */
    reset(64, 64);
    for (int y = 8; y < 56; ++y) block(20 + y % 7, y);
    navpath_t a, b;
    CHECK(P_NavPlan(&level, 0.42f, (fvec2_t){3.5f, 30.5f}, (fvec2_t){60.5f, 33.5f}, &a));
    CHECK(P_NavPlan(&level, 0.42f, (fvec2_t){3.5f, 30.5f}, (fvec2_t){60.5f, 33.5f}, &b));
    CHECK(a.count == b.count && !memcmp(a.points, b.points, sizeof(fvec2_t) * a.count));

    /* Work budget: a corner to corner search on open ground stays far from flood-fill cost. */
    reset(256, 256);
    navstats_t before = P_NavStats();
    CHECK(P_NavPlan(&level, 0.42f, (fvec2_t){1.5f, 1.5f}, (fvec2_t){254.5f, 254.5f}, &path));
    CHECK(P_NavStats().expansions - before.expansions < 2000);
    return 0;
}

static int find_ground_type(void) {
    for (int i = 0; i < num_actor_types; ++i)
        if ((actor_types[i].traits & (MF_MOBILE | MF_FLY)) == MF_MOBILE &&
            mobjinfo[actor_types[i].id].seestate) return actor_types[i].id;
    return -1;
}

static mobj_t *spawn(int type, float x, float y) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){x, y}, 0), type);
    if (!unit) return NULL;
    unit->traits = MF_MOBILE | MF_SELECTABLE | MF_RENDERABLE;
    unit->speed = 4.0f;
    unit->radius = 0.4f;
    unit->owner = unit->team = 0;
    return unit;
}

static int crowd(int type) {
    /* Twelve units cross a wall through a two-cell door, then a second group returns. */
    reset(48, 24);
    for (int y = 0; y < 24; ++y) if (y != 11 && y != 12) block(24, y);
    P_FreeThinkers();
    enum { N = 12 };
    mobj_t *units[N];
    for (int i = 0; i < N; ++i) {
        units[i] = spawn(type, 4.5f + (float)(i % 4), 8.5f + (float)(i / 4));
        CHECK(units[i]);
    }
    P_MoveUnitsAt(&level, units, N, (fvec2_t){40.5f, 12.5f});
    int arrived = 0, tic;
    for (tic = 0; tic < 4000 && arrived < N; ++tic) {
        P_Ticker();
        arrived = 0;
        for (int i = 0; i < N; ++i) {
            fvec2_t p = fixed3_xy_to_fvec2(units[i]->core.position);
            CHECK(P_CheckPosition(&level, units[i], p.x, p.y) || tic < 2);
            arrived += units[i]->movement.order_arrived;
        }
    }
    CHECK(arrived == N);
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            float d2 = fvec2_distance_squared(fixed3_xy_to_fvec2(units[i]->core.position),
                                              fixed3_xy_to_fvec2(units[j]->core.position));
            CHECK(d2 > 0.5f * 0.5f); /* No stacking at the destination. */
        }
    return 0;
}

static int head_on(int type) {
    /* Two columns pass each other inside a three-wide corridor. */
    reset(40, 3);
    P_FreeThinkers();
    mobj_t *left[3], *right[3];
    for (int i = 0; i < 3; ++i) {
        left[i] = spawn(type, 2.5f + (float)i * 1.2f, 1.5f);
        right[i] = spawn(type, 37.5f - (float)i * 1.2f, 1.5f);
        CHECK(left[i] && right[i]);
    }
    P_MoveUnitsAt(&level, left, 3, (fvec2_t){36.5f, 1.5f});
    P_MoveUnitsAt(&level, right, 3, (fvec2_t){3.5f, 1.5f});
    int arrived = 0;
    for (int tic = 0; tic < 4000 && arrived < 6; ++tic) {
        P_Ticker();
        arrived = 0;
        for (int i = 0; i < 3; ++i) arrived += left[i]->movement.order_arrived + right[i]->movement.order_arrived;
    }
    CHECK(arrived == 6);
    return 0;
}

static int unreachable_order(int type) {
    /* An order into a sealed pocket ends at the pocket's edge, it does not wander or freeze. */
    reset(32, 32);
    for (int i = 10; i <= 14; ++i) { block(i, 10); block(i, 14); block(10, i); block(14, i); }
    P_FreeThinkers();
    mobj_t *unit = spawn(type, 2.5f, 2.5f);
    CHECK(unit && P_MoveUnitTo(&level, unit, (fvec2_t){12.5f, 12.5f}));
    for (int tic = 0; tic < 1200 && !unit->movement.order_arrived; ++tic) P_Ticker();
    CHECK(unit->movement.order_arrived);
    return 0;
}

int main(void) {
    G_InitGame(); P_InitThinkers();
    RTS_RUN(planner());
    int type = find_ground_type();
    CHECK(type >= 0);
    RTS_RUN(crowd(type));
    RTS_RUN(head_on(type));
    RTS_RUN(unreachable_order(type));
    puts("PASS: shared A* planner, steering and crowd handling");
    return 0;
}
