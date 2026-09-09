#include "game.h"
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
        assert(rts_game_model_tick(model, FIXED_DT));
        for (int i = 0; i < count; ++i) {
            mobj_t *guard = guards[i];
            fvec2_t position = fixed3_xy_to_fvec2(guard->core.position);
            /* Native route envelope separates the vent encounter from the city. */
            assert(!guard->remove && guard->hp == guard->max_hp);
            assert(position.x >= 50 && position.x <= 57 && position.y >= 24 && position.y <= 31);
            if (tic >= RTS_TICRATE) assert(guard->waypoints.count == 2);
            assert(!guard->attack.target);
        }
    }
    assert(fvec2_distance_squared(fixed3_xy_to_fvec2(start),
                                  fixed3_xy_to_fvec2(guards[0]->core.position)) > 0);

    /* Exercise a whole loop without other guards crowding the shared endpoint. */
    for (int i = 1; i < count; ++i) P_RemoveMobj(guards[i]);
    bool moved = false, returned = false;
    for (int tic = 0; tic < 60 * RTS_TICRATE; ++tic) {
        assert(rts_game_model_tick(model, FIXED_DT));
        if (guards[0]->waypoints.current == 1) moved = true;
        if (moved && guards[0]->waypoints.current == 0) returned = true;
    }
    assert(moved && returned);

    /* Approaching the guards must still trigger ordinary combat. */
    mobj_t *trooper = P_SpawnMobj(fixed3_add_planar(guards[0]->core.position,
                                  fixed3_planar_delta((fvec2_t){ 1, 0 })), MT_TROOPER);
    assert(trooper);
    trooper->owner = 0;
    trooper->allegiance = ALLEGIANCE_PLAYER;
    int hp = trooper->hp;
    for (int tic = 0; tic < RTS_TICRATE && trooper->hp == hp; ++tic)
        assert(rts_game_model_tick(model, FIXED_DT));
    assert(trooper->hp < hp);
    rts_game_model_destroy(model);
    puts("PASS: Human02 guards patrol the petrovent and fight an approaching player");
    return 0;
}
