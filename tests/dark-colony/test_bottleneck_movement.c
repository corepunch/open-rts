#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

/* Retail Bottlenecks (J2PLAY05): a trooper group crosses the map around a
 * ridge with the shared A* planner. The shortest eight-connected walk from the
 * spawn block to the goal is about 113 cells; smoothing and crowding add little. */
enum { TROOPERS = 12, MAX_TICS = 6000, MAX_TRAVEL_CELLS = 150 };

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY",
        .map_path = "SCENARIO/MPLAYER/J2PLAY05.MAP"};
    dc_skirmish_t setup = {.quantity = 20, .flow = 2, .rank = 3};
    for (int i = 0; i < 8; ++i)
        setup.players[i] = (dc_skirmish_player_t){.type = DC_PLAYER_NONE, .team = i};
    setup.players[0] = (dc_skirmish_player_t){.type = DC_PLAYER_HUMAN, .race = 0, .color = 6, .team = 0};
    setup.players[1] = (dc_skirmish_player_t){.type = DC_PLAYER_HUMAN, .race = 1, .color = 4, .team = 1};
    DC_RequestSkirmish(config.map_path, &setup);
    assert(model && rts_game_model_load(model, &config));
    assert(level.width == 112 && level.height == 98);

    mobj_t *units[TROOPERS];
    for (int i = 0; i < TROOPERS; ++i) {
        fixed2_t at = {FIXED_LIT(20.5) + (i % 4) * FIXED_ONE, FIXED_LIT(82.5) + (i / 4) * FIXED_ONE};
        assert(L_IsWalkable(&level, fixed_floor_int(at.x), fixed_floor_int(at.y)));
        units[i] = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_TROOPER);
        assert(units[i] && (units[i]->traits & MF_MOBILE));
        units[i]->owner = units[i]->team = 0;
    }
    P_Ticker();
    const fixed2_t goal = FIXED2_LIT(40.5, 60.5);
    assert(!L_IsWalkable(&level, 30, 74)); /* The direct line crosses a ridge. */
    P_MoveUnitsAt(&level, units, TROOPERS, goal);
    for (int i = 0; i < TROOPERS; ++i)
        assert(P_HasMoveOrder(units[i]) &&
               fixed2_distance_squared64(units[i]->movement.goal, goal) <= fixed_sq64(3 * FIXED_ONE));

    fixed_t traveled[TROOPERS] = {0};
    int stalled[TROOPERS] = {0}, active[TROOPERS] = {0}, arrived = 0, tic;
    fixed3_t before[TROOPERS];
    for (tic = 0; tic < MAX_TICS && arrived < TROOPERS; ++tic) {
        for (int i = 0; i < TROOPERS; ++i) before[i] = units[i]->core.position;
        P_Ticker();
        arrived = 0;
        for (int i = 0; i < TROOPERS; ++i) {
            mobj_t *u = units[i];
            fixed3_t delta = fixed3_planar_displacement(before[i], u->core.position);
            traveled[i] += fixed2_length(fixed3_xy(delta));
            if (u->movement.order_arrived) { ++arrived; continue; }
            ++active[i];
            stalled[i] += !delta.x && !delta.y;
        }
    }
    if (arrived != TROOPERS) {
        for (int i = 0; i < TROOPERS; ++i) {
            fixed2_t p = fixed3_xy(units[i]->core.position);
            fprintf(stderr, "trooper %d at %.2f,%.2f traveled %.1f stalled %d/%d arrived %d\n",
                    i, fixed_to_float(p.x), fixed_to_float(p.y), fixed_to_float(traveled[i]),
                    stalled[i], active[i], units[i]->movement.order_arrived);
        }
    }
    assert(arrived == TROOPERS);
    for (int i = 0; i < TROOPERS; ++i) {
        fixed2_t p = fixed3_xy(units[i]->core.position);
        /* Every trooper settles in the blob around the common goal. */
        assert(fixed2_distance_squared64(p, goal) <= fixed_sq64(4 * FIXED_ONE));
        assert(traveled[i] < MAX_TRAVEL_CELLS * FIXED_ONE);
        /* Turning in place and yielding to neighbours is a fraction of the trip;
         * with jittering headings a trooper spent most tics rotating. */
        assert(stalled[i] * 5 < active[i]);
        for (int j = i + 1; j < TROOPERS; ++j)
            assert(fixed2_distance_squared64(p, fixed3_xy(units[j]->core.position)) > FIXED_LIT_64(0.5 * 0.5));
    }
    rts_game_model_destroy(model);
    printf("PASS: %d troopers crossed Bottlenecks in %d tics without stalls or stacking\n",
           TROOPERS, tic);
    return 0;
}
