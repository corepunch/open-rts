#include "game.h"
#include "info.h"
#include "dc_types.h"
#include <assert.h>

int main(void) {
    RtsGameModelConfig config = {
        .data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN02.MAP",
    };
    RtsGameModel *first = rts_game_model_create();
    RtsGameModel *second = rts_game_model_create();
    assert(first && second && rts_game_model_load(first, &config));
    assert(rts_game_model_load(second, &config));
    assert(!rts_game_model_tick(first, FIXED_DT));
    thinker_t *head = thinkercap.next;
    rts_game_model_destroy(first);
    assert(thinkercap.next == head && level.mission);

    mobj_t *producer = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        if (actor->owner == 0 && actor->type_id == MT_EXCOPOD) producer = actor;
        if (actor->owner != 0) P_RemoveMobj(actor);
    }
    assert(producer);
    level.player_resources[0][0] = 10000; /* Explicit production test fixture. */
    const int buttons[] = {81, 85, 82, 86};
    const int spawnstates[] = {S_SCNCPOD_BUILD1, S_SCNCPOD2_BUILD1, 0, S_ROBOPOD2_BUILD1};
    const int finalstates[] = {S_SCNCPOD_STND1, S_SCNCPOD2_STND1, 0, S_ROBOPOD2_STND1};
    for (int i = 0; i < 4; ++i) {
        RtsGameCommand build = { .kind = RTS_GAME_COMMAND_BUILD_PRODUCT,
            .data.build_product = { .producer_id = producer->id, .ui_id = buttons[i] } };
        assert(rts_game_model_command(second, &build));
        mobj_t *building = (mobj_t *)thinkercap.prev;
        assert(building != producer && building->thinker.function == P_MobjThinker);
        if (spawnstates[i]) {
            assert(building->core.state_id == spawnstates[i]);
            for (int tic = 0; tic < 500 && building->core.state_id != finalstates[i]; ++tic) P_Ticker();
            assert(!building->remove && building->core.state_id == finalstates[i]);
        }
    }
    mobj_t *blood = P_SpawnMobj(producer->core.position, MT_BLOOD);
    assert(blood && blood->thinker.function == P_MobjThinker);
    assert(blood->core.state_id == S_BLOOD1 && blood->core.tics == 2);
    for (int tic = 0; tic < 12; ++tic) P_Ticker();
    assert(blood->remove && blood->thinker.function == NULL);
    P_Ticker();
    rts_game_model_destroy(second);
    assert(!level.mission && thinkercap.next == &thinkercap);
    return 0;
}
