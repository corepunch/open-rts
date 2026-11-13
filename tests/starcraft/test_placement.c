#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft placement", #c)
/* Where a passenger is put down (OpenBW find_unit_placement): the order the
 * rings are walked, jumping past blockers, the 8 pixel grid, the playable
 * bounds, a spot's connection to the container, and a search that costs a
 * bounded number of box tests however large the map is. The searches here
 * call sc_unload_position directly, so nothing moves a unit afterwards. */

static mobj_t *spawn(int type, int px, int py, int owner) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fixed2((fixed2_t){SC_PIXELS(px), SC_PIXELS(py)}, 0), (uint16_t)type);
    if (u) {
        u->owner = u->team = (uint8_t)owner;
        u->allegiance = owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
        sc_start(u);
    }
    return u;
}
static fixed2_t at_of(const mobj_t *mo) { return fixed3_xy(mo->core.position); }
static ivec2_t px_of(const mobj_t *mo) {
    return (ivec2_t){(int)(at_of(mo).x / SC_PIXELS(1)), (int)(at_of(mo).y / SC_PIXELS(1))};
}
static void terrain(int walkable) {
    memset(level.blocked, walkable ? 0 : 1, (size_t)level.width * level.height);
    P_NavFree(&level);
    P_NavBeginTick();
}
static void walk(int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) level.blocked[y * level.width + x] = 0;
    P_NavFree(&level);
    P_NavBeginTick();
}
static void reset(void) {
    P_FreeThinkers();
    P_InitThinkers();
    terrain(1);
}
/* A Marine in the open that nothing holds in place, as a blocker. */
static mobj_t *blocker(int px, int py) {
    mobj_t *b = spawn(MT_MARINE, px, py, 1);
    if (b) b->traits &= ~MF_ATTACK;
    return b;
}
static bool place(mobj_t *marine, const mobj_t *container, ivec2_t at) {
    return sc_unload_position(marine, container, (fixed2_t){SC_PIXELS(at.x), SC_PIXELS(at.y)});
}
static void forget(void) { memset(&sc_placement_stats, 0, sizeof(sc_placement_stats)); }

/* Symmetric free ground around a Dropship over a Marine. For a Marine on a
 * Marine the first ring starts 18 by 21 pixels out: x 982 floors to 976,
 * y 979 + 42 ceils to 1024. That ring is 36 wide, wider than twice the
 * Marine's 17, so the bottom edge starts 24 pixels in, at x 1000; each
 * blocked spot is jumped past (to its far side, then up to the 8 pixel
 * grid) and a spent edge turns the walk onto the next one. */
static int ring_order(void) {
    reset();
    mobj_t *ship = spawn(MT_DROPSHIP, 1000, 1000, 0), *marine = spawn(MT_MARINE, 1000, 1000, 0);
    CHECK(ship && marine);
    mobj_t *origin_blocker = blocker(1000, 1000);
    CHECK(origin_blocker);
    forget();
    CHECK(place(marine, ship, (ivec2_t){1000, 1000}));
    CHECK(px_of(marine).x == 1000 && px_of(marine).y == 1024 && sc_placement_stats.candidates == 1);
    /* With that spot taken the walk jumps 17 pixels past it and onto the
     * grid, to 1024, the end of the bottom edge. */
    CHECK(blocker(1000, 1024));
    CHECK(place(marine, ship, (ivec2_t){1000, 1000}));
    CHECK(px_of(marine).x == 1024 && px_of(marine).y == 1024);
    /* The bottom edge spent, the walk turns up the right edge from its
     * lower end, where the jump past the blocker there lands on y 1000. */
    CHECK(blocker(1024, 1024));
    CHECK(place(marine, ship, (ivec2_t){1000, 1000}));
    CHECK(px_of(marine).x == 1024 && px_of(marine).y == 1000);
    /* Nothing blocks the origin of an open spot: it is used as it is. */
    reset();
    ship = spawn(MT_DROPSHIP, 1000, 1000, 0);
    marine = spawn(MT_MARINE, 1000, 1000, 0);
    forget();
    CHECK(place(marine, ship, (ivec2_t){1000, 1000}) && px_of(marine).x == 1000 && px_of(marine).y == 1000);
    CHECK(sc_placement_stats.candidates == 0);
    reset();
    return 0;
}

/* The ground at the container's own spot decides when no unit is in the
 * way; the container's region decides which displaced spots count. */
static int connection(void) {
    reset();
    /* The origin cell (20,20) and cell (20,16) are ground; a wall row lies
     * between them. The Marine on the origin leaves no room in its own cell,
     * so the only free spot is in the cell it cannot reach. */
    terrain(0);
    walk(20, 20, 20, 20);
    walk(20, 16, 20, 16);
    mobj_t *ship = spawn(MT_DROPSHIP, 656, 656, 0), *marine = spawn(MT_MARINE, 656, 656, 0);
    CHECK(ship && marine && blocker(656, 656));
    forget();
    CHECK(!place(marine, ship, (ivec2_t){656, 656}));
    CHECK(sc_placement_stats.candidates > 0 && sc_placement_stats.fit_tests == 0); /* All refused as unreachable. */
    /* Joined by ground, the same spot is allowed. */
    walk(20, 17, 20, 19);
    forget();
    CHECK(place(marine, ship, (ivec2_t){656, 656}));
    CHECK(px_of(marine).x / 32 == 20 && px_of(marine).y / 32 >= 16 && px_of(marine).y / 32 <= 20);
    CHECK(sc_placement_stats.fit_tests > 0);
    /* Terrain that cannot hold the Marine at a free origin fails at once,
     * with no search at all. */
    reset();
    terrain(0);
    ship = spawn(MT_DROPSHIP, 656, 656, 0);
    marine = spawn(MT_MARINE, 656, 656, 0);
    walk(24, 24, 28, 28);
    forget();
    CHECK(ship && marine && !place(marine, ship, (ivec2_t){656, 656}));
    CHECK(sc_placement_stats.candidates == 0 && sc_placement_stats.blocker_scans == 1);
    /* An occupied origin over a wall cannot reach ground beyond it. */
    CHECK(blocker(656, 656));
    forget();
    CHECK(!place(marine, ship, (ivec2_t){656, 656}));
    /* A Bunker blocks its own cell yet does not cut the ground under it:
     * ground on either side of a Bunker that plugs a one-cell gap is one area. */
    reset();
    terrain(0);
    walk(10, 20, 14, 20);
    mobj_t *bunker = spawn(MT_BUNKER, 12 * 32 + 16, 20 * 32 + 16, 0);
    marine = spawn(MT_MARINE, 12 * 32 + 16, 20 * 32 + 16, 0);
    CHECK(bunker && marine);
    P_SyncBuildingBlocking();
    P_NavFree(&level);
    forget();
    CHECK(place(marine, bunker, px_of(bunker)));
    CHECK(px_of(marine).y / 32 == 20);
    reset();
    return 0;
}

/* Map edges and corners: the box must stay inside the playable map, whose
 * last 32 pixel rows are not, and an origin whose box crosses the edge is
 * searched rather than refused. */
static int edges(void) {
    int w = level.width * 32, h = level.height * 32;
    const struct { int x, y; } origins[] = {{3, 330}, {330, 3}, {3, 3}, {w - 3, 330}, {w - 3, h - 40}, {3, h - 5}, {330, h - 2}};
    for (unsigned i = 0; i < sizeof(origins) / sizeof(*origins); i++) {
        reset();
        mobj_t *ship = spawn(MT_DROPSHIP, origins[i].x, origins[i].y, 0), *marine = spawn(MT_MARINE, origins[i].x, origins[i].y, 0);
        CHECK(ship && marine);
        CHECK(place(marine, ship, (ivec2_t){origins[i].x, origins[i].y}));
        ivec2_t at = px_of(marine);
        CHECK(at.x - 8 >= 0 && at.x + 8 < w && at.y - 9 >= 0 && at.y + 10 < h - 32);
        CHECK(abs(at.x - origins[i].x) <= 128 && abs(at.y - origins[i].y) <= 128);
    }
    /* Ground only in the unplayable strip: nothing to stand on. */
    reset();
    terrain(0);
    walk(10, level.height - 1, 14, level.height - 1);
    mobj_t *ship = spawn(MT_DROPSHIP, 12 * 32, h - 16, 0), *marine = spawn(MT_MARINE, 12 * 32, h - 16, 0);
    CHECK(ship && marine && !place(marine, ship, (ivec2_t){12 * 32, h - 16}));
    reset();
    return 0;
}

/* Cost: a search with nowhere to go still examines a bounded set of spots,
 * the same on every map size, and tests each against a table, never a path. */
static int bounded_cost(int *candidates) {
    reset();
    terrain(0);
    walk(64, 64, 64, 64);
    int mid = 64 * 32 + 16;
    mobj_t *ship = spawn(MT_DROPSHIP, mid, mid, 0), *marine = spawn(MT_MARINE, mid, mid, 0);
    CHECK(ship && marine && blocker(mid, mid));
    forget();
    CHECK(!place(marine, ship, (ivec2_t){mid, mid}));
    CHECK(sc_placement_stats.searches == 1 && sc_placement_stats.fit_tests == 0);
    CHECK(sc_placement_stats.reach_tests == sc_placement_stats.candidates);
    CHECK(sc_placement_stats.candidates > 100 && sc_placement_stats.candidates <= 600); /* 511 now; the old search made 1089. */
    /* A free map with the origin blocked succeeds at the first spot. */
    *candidates = sc_placement_stats.candidates;
    reset();
    ship = spawn(MT_DROPSHIP, mid, mid, 0);
    marine = spawn(MT_MARINE, mid, mid, 0);
    CHECK(blocker(mid, mid));
    forget();
    CHECK(place(marine, ship, (ivec2_t){mid, mid}) && sc_placement_stats.candidates == 1 && sc_placement_stats.fit_tests == 1);
    reset();
    return 0;
}

static bool open_level(int size) {
    level.width = level.height = size;
    level.blocked = calloc((size_t)size * size, 1);
    level.cell_solid = calloc((size_t)size * size, 1);
    level.mission = calloc(1, sizeof(sc_mission_t));
    return level.blocked && level.cell_solid && level.mission && P_InitSight();
}
static void close_level(void) {
    free(level.mission);
    level.mission = NULL;
    P_FreeLevel(&level);
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    consoleplayer = 0;
    int counts[2] = {0, 0};
    const int sizes[] = {128, 256};
    for (int i = 0; i < 2; i++) {
        CHECK(open_level(sizes[i]));
        ((sc_mission_t *)level.mission)->rng = 2;
        reset();
        if (i == 0) {
            CHECK(!ring_order());
            CHECK(!connection());
        }
        CHECK(!edges());
        CHECK(!bounded_cost(&counts[i]));
        P_FreeThinkers();
        P_InitThinkers();
        close_level();
    }
    /* The work does not grow with the map. */
    CHECK(counts[0] == counts[1]);
    puts("PASS: StarCraft unload placement ring order, connection, map edges and bounded search cost");
    return 0;
}
