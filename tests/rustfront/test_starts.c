/* Multiplayer maps and their starts (w_starts.c): the map list a lobby shows, every start
 * spawning a faction's base where the plan puts it, teams, empty seats, and every start
 * able to drive to every other. */
#include "t_local.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#define CHECK(c) RTS_CHECK(c, "rustfront starts", #c)

static RtsGameModel *model;

static bool load(const char *map, const rf_startplan_t *plan) {
    if (model) rts_game_model_destroy(model);
    RF_SetStartPlan(plan);
    model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = g_game_default_root, .map_path = map};
    return model && rts_game_model_load(model, &config);
}

static mobj_t *hq_of(int owner) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == owner && RF_IsRole(u->type_id, RF_ROLE_HQ)) return u;
    }
    return NULL;
}

static int owned(int owner) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        n += th->function == P_MobjThinker && ((mobj_t *)th)->owner == owner && ((mobj_t *)th)->type_id < MT_FX_HIT;
    return n;
}

/* Cells reachable from `from` over open ground, four ways (cliffs and water block). */
static bool reachable(ivec2_t from, ivec2_t to) {
    int cells = level.width * level.height, head = 0, tail = 0;
    int *queue = malloc(sizeof(int) * (size_t)cells);
    uint8_t *seen = calloc((size_t)cells, 1);
    queue[tail++] = L_Index(&level, from.x, from.y);
    seen[queue[0]] = 1;
    bool found = false;
    while (head < tail && !found) {
        int i = queue[head++], x = i % level.width, y = i / level.width;
        static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int k = 0; k < 4; ++k) {
            int nx = x + d[k][0], ny = y + d[k][1];
            if (!L_Contains(&level, nx, ny)) continue;
            int n = L_Index(&level, nx, ny);
            if (nx == to.x && ny == to.y) found = true;
            if (seen[n] || level.blocked[n]) continue;
            seen[n] = 1;
            queue[tail++] = n;
        }
    }
    free(queue);
    free(seen);
    return found;
}

static int test_map_list(void) {
    rf_mapinfo_t maps[32];
    int count = RF_ListMaps(NULL, maps, 32);
    static const struct { const char *path, *title; int players; } WANT[] = {
        {"maps/ironbridge.txt", "Ironbridge", 2}, {"maps/mesa-duel.txt", "Mesa Duel", 2},
        {"maps/three-crowns.txt", "Three Crowns", 3}, {"maps/crossroads.txt", "Crossroads", 4},
        {"maps/highland-ring.txt", "Highland Ring", 4},
    };
    int found = 0;
    for (size_t w = 0; w < sizeof(WANT) / sizeof(*WANT); ++w)
        for (int i = 0; i < count; ++i)
            if (!strcmp(maps[i].path, WANT[w].path)) {
                CHECK(!strcmp(maps[i].title, WANT[w].title) && maps[i].players == WANT[w].players);
                CHECK(maps[i].sides == 0 && maps[i].width > 0 && maps[i].height > 0);
                found++;
            }
    CHECK(found == 5);
    for (int i = 1; i < count; ++i) CHECK(maps[i - 1].players <= maps[i].players);
    for (int i = 0; i < count; ++i)
        if (!strcmp(maps[i].path, "maps/skirmish.txt")) CHECK(maps[i].players == 0 && maps[i].sides == 2);
    /* Every multiplayer map: each start a side of its default faction, free for all, every
     * headquarters reachable from every other. */
    for (int i = 0; i < count; ++i) {
        if (!maps[i].players) continue;
        CHECK(load(maps[i].path, NULL));
        CHECK(RF_StartCount() == maps[i].players && level.player_teams);
        for (int a = 0; a < maps[i].players; ++a) {
            mobj_t *hq = hq_of(a);
            CHECK(hq && RF_OwnerFaction(a) == RF_Start(a)->faction && owned(a) >= 6);
            ivec2_t from = {RF_Start(a)->cell.x + 1, RF_Start(a)->cell.y + 4};
            for (int b = 0; b < maps[i].players; ++b) {
                CHECK(P_IsAlly(hq, hq_of(b)) == (a == b));
                CHECK(reachable(from, (ivec2_t){RF_Start(b)->cell.x + 1, RF_Start(b)->cell.y + 3}));
            }
        }
        printf("%s (%s): %d starts, %dx%d\n", maps[i].title, maps[i].path, maps[i].players, maps[i].width,
               maps[i].height);
    }
    puts("PASS: the map list names the multiplayer maps; every start spawns, free for all, all reachable");
    return 0;
}

static int test_plans(void) {
    /* Two against two, owner 0 at the south-west start as the Concord, owner 3 sitting out. */
    rf_startplan_t plan = {.count = 4, .seat = {{3, RF_CONCORD, 1}, {1, RF_CLANS, 2}, {2, -1, 1}, {-1, -1, 0}}};
    CHECK(load("maps/crossroads.txt", &plan));
    CHECK(hq_of(0) && hq_of(0)->type_id == MT_LC_SPIRE);
    irect_t cells = P_MobjCells(hq_of(0));
    CHECK(cells.x == RF_Start(3)->cell.x && cells.y == RF_Start(3)->cell.y);
    CHECK(hq_of(1)->type_id == MT_CL_HALL && hq_of(2)->type_id == RF_Role(RF_Start(2)->faction, RF_ROLE_HQ));
    CHECK(!owned(3) && !hq_of(3));
    CHECK(P_IsAlly(hq_of(0), hq_of(2)) && P_IsAlly(hq_of(2), hq_of(0)));
    CHECK(!P_IsAlly(hq_of(0), hq_of(1)) && !P_IsAlly(hq_of(1), hq_of(2)));
    /* Each faction's starting force: HQ, harvester, a light vehicle and a squad. */
    CHECK(owned(0) == 6 && owned(1) == 9);
    /* A start on blocked ground fails, and spawns nothing. */
    int before = owned(5);
    CHECK(!RF_SpawnStart(5, RF_DIRECTORATE, 0, 26) && owned(5) == before);   /* in the west canyon's river */
    /* A plan is kept until changed; NULL brings back the defaults. */
    CHECK(load("maps/crossroads.txt", NULL) && hq_of(3) && hq_of(0)->type_id == MT_HQ);
    puts("PASS: plans put factions at chosen starts, ally teams, leave seats empty; blocked starts fail");
    return 0;
}

int main(void) {
    RTS_RUN(test_map_list());
    RTS_RUN(test_plans());
    if (model) rts_game_model_destroy(model);
    return 0;
}
