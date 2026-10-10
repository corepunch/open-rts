#include "engine.h"
#include "info.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    RtsGameModelConfig config = {
        .data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN02.MAP",
    };
    RtsGameModel *model = rts_game_model_create();
    assert(model && rts_game_model_load(model, &config));
    mobj_t *guards[10];
    int count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        if (actor->type_id == MT_GREY) {
            assert(count < 10);
            guards[count++] = actor;
        }
    }
    assert(count == 10);
    fixed3_t start = guards[0]->core.position;
    for (int tic = 0; tic < 60 * RTS_TICRATE; ++tic) {
        assert(rts_game_model_tick(model, RTS_TICK_MS));
        for (int i = 0; i < count; ++i) {
            mobj_t *guard = guards[i];
            fixed2_t position = fixed3_xy(guard->core.position);
            /* Crowded routes detour beyond the waypoint rectangle. */
            assert(!guard->remove && guard->hp == guard->max_hp);
            ivec2_t cell = fixed2_cell(position);
            assert(L_Contains(&level, cell.x, cell.y));
            if (tic >= RTS_TICRATE) assert(guard->waypoints.count == 2);
            assert(!guard->attack.target);
        }
    }
    assert(fixed2_distance_squared64(fixed3_xy(start),
                                  fixed3_xy(guards[0]->core.position)) > 0);

    /* Exercise a whole loop without other guards crowding the shared endpoint. */
    for (int i = 1; i < count; ++i) P_RemoveMobj(guards[i]);
    bool moved = false, returned = false;
    for (int tic = 0; tic < 60 * RTS_TICRATE; ++tic) {
        assert(rts_game_model_tick(model, RTS_TICK_MS));
        if (guards[0]->waypoints.current == 1) moved = true;
        if (moved && guards[0]->waypoints.current == 0) returned = true;
    }
    assert(moved && returned);

    /* Approaching the guards must still trigger ordinary combat. */
    mobj_t *trooper = P_SpawnMobj(fixed3_add_planar(guards[0]->core.position,
                                  fixed3_planar_delta(FIXED2_LIT(1, 0))), MT_TROOPER);
    assert(trooper);
    trooper->owner = 0;
    trooper->allegiance = ALLEGIANCE_PLAYER;
    int hp = trooper->hp;
    for (int tic = 0; tic < RTS_TICRATE && trooper->hp == hp; ++tic)
        assert(rts_game_model_tick(model, RTS_TICK_MS));
    assert(trooper->hp < hp);
    rts_game_model_destroy(model);
    puts("PASS: Human02 guards patrol the petrovent and fight an approaching player");
    return 0;
}
