#include "t_local.h"
#include "info.h"
#include "../../play/p_nav.h"

static int narrow_corner(void) {
    const char *tag = "narrow corner";
    G_InitGame();
    P_FreeLevel(&level);
    level = (level_t){.width = 16, .height = 12};
    level.blocked = malloc((size_t)level.width * level.height);
    assert(level.blocked);
    memset(level.blocked, 1, (size_t)level.width * level.height);
    for (int y = 1; y <= 4; ++y) level.blocked[L_Index(&level, 4, y)] = 0;
    for (int x = 4; x <= 12; ++x) level.blocked[L_Index(&level, x, 4)] = 0;
    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){4.5f, 1.5f}, 0), MT_TROOPER);
    assert(unit);
    unit->traits = MF_MOBILE;
    unit->speed = 1.0f;
    unit->core.angle = ANG90;
    RTS_CHECK(P_MoveUnitTo(&level, unit, (fvec2_t){12.5f, 4.5f}), tag, "create route");
    navstats_t planned = P_NavStats();
    for (int tic = 0; tic < 600 && !unit->movement.order_arrived; ++tic) P_Ticker();
    RTS_CHECK(unit->movement.order_arrived, tag, "reach goal around the bend");
    RTS_CHECK(P_NavStats().searches == planned.searches, tag,
              "clear route needs no replans while rounding the corner");
    P_FreeLevel(&level);
    return 0;
}

static int human01_pair(fvec2_t goal, int phase) {
    const char *tag = "Human01 corner pair";
    P_FreeThinkers();
    for (int tic = 0; tic < phase; ++tic) P_Ticker();
    mobj_t *units[2];
    for (int i = 0; i < 2; ++i) {
        units[i] = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){22.5f - i, 2.5f}, 0), MT_TROOPER);
        assert(units[i]);
        units[i]->traits = MF_MOBILE; /* Isolate navigation from combat. */
    }
    P_MoveUnitsAt(&level, units, 2, goal);
    int active[2] = {0}, stalls[2] = {0}, episodes[2] = {0};
    bool stopped[2] = {false};
    int arrived = 0, tic;
    for (tic = 0; tic < 1200 && arrived < 2; ++tic) {
        fixed3_t before[2] = {units[0]->core.position, units[1]->core.position};
        P_Ticker();
        arrived = 0;
        for (int i = 0; i < 2; ++i) {
            if (units[i]->movement.order_arrived) { ++arrived; continue; }
            fixed3_t delta = fixed3_planar_displacement(before[i], units[i]->core.position);
            bool waiting = !delta.x && !delta.y;
            ++active[i];
            stalls[i] += waiting;
            episodes[i] += waiting && !stopped[i];
            stopped[i] = waiting;
        }
    }
    printf("Human01 goal=(%.1f,%.1f): arrived=%d tics=%d stalls=%d/%d,%d/%d stop episodes=%d,%d\n",
           goal.x, goal.y, arrived, tic, stalls[0], active[0], stalls[1], active[1], episodes[0], episodes[1]);
    RTS_CHECK(arrived == 2, tag, "both troopers reach the goal");
    for (int i = 0; i < 2; ++i) {
        RTS_CHECK(stalls[i] * 2 < active[i], tag, "turning and yielding are a fraction of travel");
        RTS_CHECK(episodes[i] <= 8, tag, "short route has no recurring stop/move oscillation");
    }
    return 0;
}

int main(void) {
    RTS_RUN(narrow_corner());
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN01.MAP"};
    assert(model && rts_game_model_load(model, &config));
    /* Exercise every phase of the throttled waypoint checks. */
    for (int pass = 0; pass < 4; ++pass) {
        RTS_RUN(human01_pair((fvec2_t){30.5f, 7.5f}, pass));
        RTS_RUN(human01_pair((fvec2_t){24.5f, 11.5f}, pass));
    }
    rts_game_model_destroy(model);
    puts("PASS: troopers round narrow and retail corners without jitter");
    return 0;
}
