#include "engine.h"
#include "info.h"
#include "t_local.h"
#include "../../play/p_nav.h"

#define CHECK(c) RTS_CHECK(c, "shared nav", #c)

static void reset(int width, int height) {
    P_FreeLevel(&level);
    level = (level_t){.width = width, .height = height};
    level.blocked = calloc((size_t)width * height, 1);
}

static void block(int x, int y) { level.blocked[L_Index(&level, x, y)] = 1; }

static fixed_t polyline(fixed2_t from, const navpath_t *path) {
    fixed_t length = 0;
    for (int i = 0; i < path->count; ++i) {
        length += fixed2_length(fixed2_sub(path->points[i], from));
        from = path->points[i];
    }
    return length;
}

static int planner(void) {
    navpath_t path;
    reset(64, 64);
    /* Open ground: string pulling collapses the route to the goal itself. */
    CHECK(P_NavPlan(&level, 0, FIXED_LIT(0.42), FIXED2_LIT(1.5, 1.5), FIXED2_LIT(40.3, 22.7), NULL, &path));
    CHECK(path.complete && path.count == 1 && fixed2_near(path.points[0], FIXED2_LIT(40.3, 22.7), FIXED_LIT(1e-4)));

    /* A wall with one gap at the bottom: the route threads it and never clips stone. */
    reset(64, 64);
    for (int y = 0; y < 60; ++y) block(32, y);
    fixed2_t from = FIXED2_LIT(4.5, 4.5), goal = FIXED2_LIT(60.5, 4.5);
    CHECK(P_NavPlan(&level, 0, FIXED_LIT(0.42), from, goal, NULL, &path) && path.complete);
    fixed2_t at = from;
    for (int i = 0; i < path.count; ++i) {
        CHECK(P_NavLineClear(&level, 0, at, path.points[i], FIXED_LIT(0.42)));
        at = path.points[i];
    }
    CHECK(fixed2_near(at, goal, FIXED_LIT(1e-4)));
    cell_t cells[512];
    int steps = P_FindPath(&level, (cell_t){4, 4}, (cell_t){60, 4}, cells, 512);
    CHECK(steps > 0 && polyline(from, &path) <= steps * FIXED_LIT(1.42));

    /* Diagonal squeeze between two stones is forbidden (no corner cutting). */
    reset(16, 16);
    block(5, 5); block(6, 6);
    steps = P_FindPath(&level, (cell_t){6, 5}, (cell_t){5, 6}, cells, 512);
    CHECK(steps > 3);

    /* Moving the endpoint within its cell must not invalidate the incoming
     * diagonal. Keep the cell centre when the exact goal clips the next wall. */
    reset(16, 16);
    block(6, 4);
    from = FIXED2_LIT(4.5, 4.5); goal = FIXED2_LIT(5.975, 5.5);
    CHECK(P_NavPlan(&level, 0, FIXED_LIT(0.5), from, goal, NULL, &path) && path.complete);
    at = from;
    for (int i = 0; i < path.count; ++i) {
        CHECK(P_NavLineClear(&level, 0, at, path.points[i], FIXED_LIT(0.5)));
        at = path.points[i];
    }
    CHECK(fixed2_near(at, goal, FIXED_LIT(1e-4)));

    /* Goal inside a sealed pocket is relocated into the start's region. */
    reset(32, 32);
    for (int i = 10; i <= 14; ++i) { block(i, 10); block(i, 14); block(10, i); block(14, i); }
    CHECK(!P_NavReachable(&level, 0, (ivec2_t){1, 1}, (ivec2_t){12, 12}));
    CHECK(P_NavPlan(&level, 0, FIXED_LIT(0.42), FIXED2_LIT(1.5, 1.5), FIXED2_LIT(12.5, 12.5), NULL, &path));
    CHECK(P_NavReachable(&level, 0, (ivec2_t){1, 1}, fixed2_cell(path.goal)));

    /* Terrain edits are noticed without anyone invalidating a cache. */
    reset(32, 8);
    for (int y = 0; y < 8; ++y) block(16, y);
    block(16, 4); level.blocked[L_Index(&level, 16, 4)] = 0;
    from = FIXED2_LIT(2.5, 4.5); goal = FIXED2_LIT(30.5, 4.5);
    CHECK(P_NavPlan(&level, 0, FIXED_LIT(0.42), from, goal, NULL, &path) && path.complete);
    block(16, 4);
    CHECK(!P_NavPlan(&level, 0, FIXED_LIT(0.42), from, FIXED2_LIT(30.5, 4.5), NULL, &path) ||
          fixed2_cell(path.goal).x < 16);

    /* Determinism: the same request yields byte-identical waypoints. */
    reset(64, 64);
    for (int y = 8; y < 56; ++y) block(20 + y % 7, y);
    navpath_t a, b;
    CHECK(P_NavPlan(&level, 0, FIXED_LIT(0.42), FIXED2_LIT(3.5, 30.5), FIXED2_LIT(60.5, 33.5), NULL, &a));
    CHECK(P_NavPlan(&level, 0, FIXED_LIT(0.42), FIXED2_LIT(3.5, 30.5), FIXED2_LIT(60.5, 33.5), NULL, &b));
    CHECK(a.count == b.count && !memcmp(a.points, b.points, sizeof(fixed2_t) * a.count));

    /* Work budget: a corner to corner search on open ground stays far from flood-fill cost. */
    reset(256, 256);
    navstats_t before = P_NavStats();
    CHECK(P_NavPlan(&level, 0, FIXED_LIT(0.42), FIXED2_LIT(1.5, 1.5), FIXED2_LIT(254.5, 254.5), NULL, &path));
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
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(x, y), 0), type);
    if (!unit) return NULL;
    unit->traits = MF_MOBILE | MF_SELECTABLE | MF_RENDERABLE;
    unit->speed = FIXED_LIT(4.0);
    unit->radius = FIXED_LIT(0.4);
    unit->owner = unit->team = 0;
    return unit;
}

static int terrain_avoidance(int type) {
    reset(16, 4);
    P_FreeThinkers();
    mobj_t *unit = spawn(type, 2.5f, 1.5f), *other = spawn(type, 3.5f, 2.25f);
    CHECK(unit && other);
    unit->radius = other->radius = FIXED_LIT(0.5);
    fixed2_t direction = {FIXED_ONE, 0};
    const fixed_t step = FIXED_LIT(0.1);
    CHECK(P_SteerAvoid(unit, direction, step).y < 0); /* Pass below the neighbour on open ground. */
    for (int x = 0; x < level.width; ++x) block(x, 0);
    fixed2_t steered = P_SteerAvoid(unit, direction, step);
    CHECK(steered.x == direction.x && steered.y == direction.y); /* The same bend would enter the wall. */
    return 0;
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
    P_MoveUnitsAt(&level, units, N, FIXED2_LIT(40.5, 12.5));
    int arrived = 0, tic;
    for (tic = 0; tic < 4000 && arrived < N; ++tic) {
        P_Ticker();
        arrived = 0;
        for (int i = 0; i < N; ++i) {
            CHECK(P_MapCircleWalkable(&level, 0, fixed3_xy(units[i]->core.position), FIXED_LIT(0.38), NULL) || tic < 2); /* never inside stone */
            arrived += units[i]->movement.order_arrived;
        }
    }
    CHECK(arrived == N);
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            int64_t d2 = fixed2_distance_squared64(fixed3_xy(units[i]->core.position),
                                                   fixed3_xy(units[j]->core.position));
            CHECK(d2 > FIXED_LIT_64(0.5 * 0.5)); /* No stacking at the destination. */
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
    P_MoveUnitsAt(&level, left, 3, FIXED2_LIT(36.5, 1.5));
    P_MoveUnitsAt(&level, right, 3, FIXED2_LIT(3.5, 1.5));
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
    CHECK(unit && P_MoveUnitTo(&level, unit, FIXED2_LIT(12.5, 12.5)));
    for (int tic = 0; tic < 1200 && !unit->movement.order_arrived; ++tic) P_Ticker();
    CHECK(unit->movement.order_arrived);
    return 0;
}


/* ---- Terrain classes --------------------------------------------------- */

enum { T_LAND = 1, T_LAKE = 0, T_SWAMP = 5, T_ROAD = 8, C_FOOT = 1, C_HOVER = 2 };

static void terrain_level(int width, int height) {
    reset(width, height);
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.cell_terrain = malloc((size_t)width * height);
    level.cell_effect = malloc((size_t)width * height);
    level.cell_solid = calloc((size_t)width * height, 1);
    memset(level.cell_terrain, T_LAND, (size_t)width * height);
    memset(level.cell_effect, 255, (size_t)width * height);
    level.speeds->class_count = 3;
    memset(level.speeds->terrain, 100, sizeof(level.speeds->terrain));
    memset(level.speeds->overlay, 100, sizeof(level.speeds->overlay));
    level.speeds->terrain[C_FOOT][T_LAKE] = 0;    /* water stops feet ... */
    level.speeds->terrain[C_HOVER][T_LAKE] = 100; /* ... but not hovercraft */
    level.speeds->terrain[C_FOOT][T_SWAMP] = 25;
    level.speeds->terrain[C_FOOT][T_ROAD] = 200;
    level.speeds->terrain[C_HOVER][T_ROAD] = 200;
    level.speeds->overlay[C_FOOT][3] = 0;
    level.speeds->overlay[C_HOVER][3] = 0;
}

static void paint(int x0, int y0, int x1, int y1, int terrain) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            level.cell_terrain[L_Index(&level, x, y)] = (uint8_t)terrain;
            /* blocked[] is the plain view of the map: water is solid for unclassed units. */
            level.blocked[L_Index(&level, x, y)] = terrain == T_LAKE;
        }
}

static int terrain_classes(void) {
    navpath_t foot, hover;
    terrain_level(60, 30);
    paint(28, 0, 31, 29, T_LAKE); /* a lake spanning the whole map */
    paint(28, 27, 31, 29, T_LAND); /* with a land bridge at the bottom */
    fixed2_t from = FIXED2_LIT(5.5, 5.5), goal = FIXED2_LIT(55.5, 5.5);
    CHECK(P_NavPlan(&level, C_HOVER, FIXED_LIT(0.42), from, goal, NULL, &hover));
    CHECK(P_NavPlan(&level, C_FOOT, FIXED_LIT(0.42), from, goal, NULL, &foot));
    CHECK(hover.count == 1); /* straight across the water */
    fixed_t foot_len = polyline(from, &foot), hover_len = polyline(from, &hover);
    CHECK(foot_len > hover_len + FIXED_LIT(15.0)); /* feet detour via the bridge ... */
    bool bridge = false;
    for (int i = 0; i < foot.count; ++i) bridge |= foot.points[i].y > FIXED_LIT(26.0);
    CHECK(bridge);
    CHECK(L_MoveSpeed(&level, C_FOOT, 29, 5) == 0 && L_MoveSpeed(&level, C_HOVER, 29, 5) == 100);
    CHECK(!P_NavReachable(&level, C_FOOT, (ivec2_t){5, 5}, (ivec2_t){29, 5}));

    /* Cost scales with speed: swamp (25%) is worth a detour, a road (200%) is worth a longer walk. */
    terrain_level(60, 30);
    paint(20, 0, 40, 14, T_SWAMP);      /* a swamp directly between start and goal */
    paint(0, 26, 59, 26, T_ROAD);       /* and a long road around the south */
    from = FIXED2_LIT(5.5, 10.5); goal = FIXED2_LIT(55.5, 10.5);
    CHECK(P_NavPlan(&level, C_FOOT, FIXED_LIT(0.42), from, goal, NULL, &foot));
    bool dry = true;
    for (int i = 0; i < foot.count; ++i)
        if (level.cell_terrain[L_Index(&level, fixed_floor_int(foot.points[i].x), fixed_floor_int(foot.points[i].y))] == T_SWAMP) dry = false;
    CHECK(dry && polyline(from, &foot) > FIXED_LIT(50.0));
    P_NavPlan(&level, 0, FIXED_LIT(0.42), from, goal, NULL, &hover); /* plain units ignore speed tables */
    CHECK(polyline(from, &hover) < FIXED_LIT(51.0));

    /* Walking speed follows the terrain under the unit. */
    int type = find_ground_type();
    CHECK(type >= 0);
    terrain_level(60, 8);
    paint(10, 0, 19, 7, T_SWAMP);
    paint(30, 0, 39, 7, T_ROAD);
    P_FreeThinkers();
    static mobjtype_t classed;
    mobj_t *unit = spawn(type, 1.5f, 4.5f);
    CHECK(unit);
    classed = *unit->info; classed.move_class = C_FOOT;
    unit->info = &classed;
    unit->speed = FIXED_LIT(4.0);
    CHECK(P_MoveUnitTo(&level, unit, FIXED2_LIT(55.5, 4.5)));
    float swamp_ticks = 0, road_ticks = 0, open_ticks = 0;
    for (int tic = 0; tic < 4000 && !unit->movement.order_arrived; ++tic) {
        float x = fixed_to_float(fixed3_xy(unit->core.position).x);
        P_Ticker();
        if (x >= 10.5f && x < 19.5f) swamp_ticks += 1; else if (x >= 30.5f && x < 39.5f) road_ticks += 1;
        else if (x >= 42.0f && x < 51.0f) open_ticks += 1;
    }
    CHECK(unit->movement.order_arrived);
    CHECK(swamp_ticks > open_ticks * 3.0f && road_ticks < open_ticks * 0.7f);
    return 0;
}

/* ---- Shared plans, time slicing, idle units ------------------------------ */

static int group_orders(int type) {
    /* Forty units to one point cost a handful of searches, not forty. */
    reset(120, 60);
    for (int y = 0; y < 45; ++y) block(60, y);
    P_FreeThinkers();
    enum { N = 40 };
    mobj_t *units[N];
    for (int i = 0; i < N; ++i) {
        units[i] = spawn(type, 8.5f + (float)(i % 8), 10.5f + (float)(i / 8));
        CHECK(units[i]);
    }
    navstats_t before = P_NavStats();
    P_MoveUnitsAt(&level, units, N, FIXED2_LIT(100.5, 12.5));
    CHECK(P_NavStats().searches - before.searches <= 8);
    for (int i = 0; i < N; ++i) CHECK(P_HasMoveOrder(units[i]) && !units[i]->movement.plan_pending);
    int arrived = 0;
    for (int tic = 0; tic < 6000 && arrived < N; ++tic) {
        P_Ticker();
        arrived = 0;
        for (int i = 0; i < N; ++i) arrived += units[i]->movement.order_arrived;
    }
    CHECK(arrived == N);
    return 0;
}

static int time_slicing(int type) {
    /* A serpentine corridor makes every search expensive; the tic's budget
     * defers the excess and the queue drains in order. */
    reset(200, 120);
    for (int row = 2; row < 118; row += 4)
        for (int x = 0; x < 200; ++x)
            if (!(row % 8 == 2 ? x >= 197 : x < 3)) { block(x, row); block(x, row + 1); }
    P_FreeThinkers();
    enum { N = 12 };
    mobj_t *units[N];
    for (int i = 0; i < N; ++i) {
        units[i] = spawn(type, 10.5f + (float)i, 0.5f);
        CHECK(units[i]);
    }
    for (int i = 0; i < N; ++i) CHECK(P_MoveUnitTo(&level, units[i], FIXED2_LIT(10.5 + i, 118.5)));
    int pending = 0;
    for (int i = 0; i < N; ++i) pending += units[i]->movement.plan_pending;
    CHECK(pending > 0 && pending < N); /* some planned now, the rest wait */
    for (int i = 0; i < N; ++i) CHECK(P_HasMoveOrder(units[i])); /* waiting units still hold their order */
    uint32_t last = 0;
    for (int tic = 0; tic < 40; ++tic) P_Ticker();
    for (int i = 0; i < N; ++i) {
        CHECK(!units[i]->movement.plan_pending && units[i]->movement.path.count > 0);
        CHECK(units[i]->movement.plan_seq >= last); /* served in order */
        last = units[i]->movement.plan_seq;
    }
    return 0;
}

static int idle_units(int type) {
    /* A wall of idle units is paid for, not walked through: the route bends around them. */
    reset(60, 30);
    P_FreeThinkers();
    mobj_t *idle[9];
    for (int i = 0; i < 9; ++i) {
        idle[i] = spawn(type, 30.5f, 10.5f + (float)i);
        CHECK(idle[i]);
    }
    mobj_t *mover = spawn(type, 5.5f, 14.5f);
    CHECK(mover && P_MoveUnitTo(&level, mover, FIXED2_LIT(55.5, 14.5)));
    const navpath_t *path = &mover->movement.path;
    fixed2_t at = fixed3_xy(mover->core.position);
    for (int i = 0; i < path->count; ++i) {
        for (int step = 0; step <= 20; ++step) {
            fixed2_t delta = fixed2_sub(path->points[i], at);
            fixed2_t p = { at.x + delta.x / 20 * step, at.y + delta.y / 20 * step };
            for (int k = 0; k < 9; ++k)
                CHECK(fixed2_distance_squared64(p, FIXED2_LIT(30.5, 10.5 + k)) > FIXED_LIT_64(0.75 * 0.75));
        }
        at = path->points[i];
    }
    return 0;
}

int main(void) {
    G_InitGame(); P_InitThinkers();
    RTS_RUN(planner());
    int type = find_ground_type();
    CHECK(type >= 0);
    RTS_RUN(terrain_avoidance(type));
    RTS_RUN(crowd(type));
    RTS_RUN(head_on(type));
    RTS_RUN(unreachable_order(type));
    RTS_RUN(terrain_classes());
    RTS_RUN(group_orders(type));
    RTS_RUN(time_slicing(type));
    RTS_RUN(idle_units(type));
    puts("PASS: shared A* planner, steering and crowd handling");
    return 0;
}
