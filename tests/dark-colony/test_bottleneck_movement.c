#include "engine.h"
#include "game.h"
#include "g_game.h"
#include "info.h"
#include "p_local.h"
#include "p_path.h"
#include "dc_skirmish.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

/* Retail Bottlenecks (J2PLAY05): a trooper group crosses the map through the
 * PTH region corridor. The shortest eight-connected walk from the spawn block
 * to the goal is about 113 cells; the corridor adds at most a handful. */
enum { TROOPERS = 12, MAX_TICS = 6000, MAX_TRAVEL_CELLS = 150 };

static angle_t heading_of(fixed3_t delta) {
    fvec2_t d = fixed3_xy_to_fvec2(delta);
#if RTS_WORLD_Y_UP
    d.y = -d.y;
#endif
    double turns = atan2(-d.y, d.x) / (2.0 * M_PI);
    if (turns < 0.0) turns += 1.0;
    return (angle_t)(uint64_t)llround(turns * 4294967296.0);
}

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
    assert(level.width == 112 && level.height == 98 && level.paths);

    mobj_t *units[TROOPERS];
    for (int i = 0; i < TROOPERS; ++i) {
        fvec2_t at = {20.5f + i % 4, 82.5f + i / 4};
        assert(L_IsWalkable(&level, (int)at.x, (int)at.y));
        units[i] = P_SpawnMobj(fixed3_from_fvec2(at, 0), MT_TROOPER);
        assert(units[i] && (units[i]->traits & MF_MOBILE));
        units[i]->owner = units[i]->team = 0;
    }
    P_Ticker();
    const fvec2_t goal = {40.5f, 60.5f};
    assert(!L_IsWalkable(&level, 30, 74)); /* The direct line crosses a ridge. */
    P_MoveUnitsAt(&level, units, TROOPERS, goal);
    for (int i = 0; i < TROOPERS; ++i)
        assert(P_HasMoveOrder(units[i]) && fvec2_near(units[i]->movement.goal, goal, 1.0f / FIXED_ONE));

    float traveled[TROOPERS] = {0};
    int stalled[TROOPERS] = {0}, active[TROOPERS] = {0}, arrived = 0, tic;
    fixed3_t before[TROOPERS];
    for (tic = 0; tic < MAX_TICS && arrived < TROOPERS; ++tic) {
        for (int i = 0; i < TROOPERS; ++i) before[i] = units[i]->core.position;
        P_Ticker();
        arrived = 0;
        for (int i = 0; i < TROOPERS; ++i) {
            mobj_t *u = units[i];
            fixed3_t delta = fixed3_planar_displacement(before[i], u->core.position);
            /* Route steps are cardinal or exactly diagonal, and the unit faces
             * the direction it translates: the previous slope-division overflow
             * turned every diagonal into a ~90 degree heading with per-tic jitter. */
            assert(!delta.x || !delta.y || abs(delta.x) == abs(delta.y));
            if (delta.x || delta.y)
                assert(angle_distance(u->core.angle, heading_of(delta)) <= ANG45 / 64u);
            traveled[i] += sqrtf(fvec2_length_squared(fixed3_xy_to_fvec2(delta)));
            if (u->movement.order_arrived) { ++arrived; continue; }
            ++active[i];
            stalled[i] += !delta.x && !delta.y;
            for (int j = i + 1; j < TROOPERS; ++j)
                assert(!ivec2_equal(DC_OccupiedPosition(units[i]), DC_OccupiedPosition(units[j])));
        }
    }
    if (arrived != TROOPERS) {
        for (int i = 0; i < TROOPERS; ++i) {
            fvec2_t p = fixed3_xy_to_fvec2(units[i]->core.position);
            fprintf(stderr, "trooper %d at %.2f,%.2f traveled %.1f stalled %d/%d arrived %d\n",
                    i, p.x, p.y, traveled[i], stalled[i], active[i], units[i]->movement.order_arrived);
        }
    }
    assert(arrived == TROOPERS);
    for (int i = 0; i < TROOPERS; ++i) {
        fvec2_t p = fixed3_xy_to_fvec2(units[i]->core.position);
        /* Every trooper ends near the common goal on its own cell. */
        assert(fvec2_distance_squared(p, goal) <= 3.0f * 3.0f);
        assert(fvec2_near(p, fvec2_cell_center(fvec2_cell(p)), 1.0f / FIXED_ONE));
        assert(traveled[i] < MAX_TRAVEL_CELLS);
        /* Turning in place and yielding to neighbours is a fraction of the trip;
         * with jittering headings a trooper spent most tics rotating. */
        assert(stalled[i] * 5 < active[i]);
        for (int j = i + 1; j < TROOPERS; ++j)
            assert(!ivec2_equal(fvec2_cell(p), fvec2_cell(fixed3_xy_to_fvec2(units[j]->core.position))));
    }
    rts_game_model_destroy(model);
    printf("PASS: %d troopers crossed Bottlenecks in %d tics facing their eight-direction steps, without stalls or shared cells\n",
           TROOPERS, tic);
    return 0;
}
