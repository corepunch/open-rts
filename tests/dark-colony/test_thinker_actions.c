#include "game.h"
#include "info.h"
#include <assert.h>

static mobj_t *other;

static void count_action(mobj_t *actor) {
    assert(actor->thinker.function == P_MobjThinker);
    level.player_resources[0][0]++;
}

static void nested_action(mobj_t *actor) {
    assert(P_SetMobjState(other, 2));
    count_action(actor);
    assert(level.player_resources[0][0] == 2);
    assert(!P_SetMobjState(other, 0));
}

int main(void) {
    const state_t states[] = {
        {0}, { .tics = -1, .action = nested_action },
        { .tics = -1, .action = count_action },
    };
    const mobjinfo_t info[] = {{0}, { .spawnstate = 1 }};
    const gameinfo_t game = {
        .states = states, .state_count = 3,
        .mobjinfo = info, .mobj_type_count = 2,
    };
    gameinfo = &game;
    P_InitThinkers();
    mobj_t *actor = P_SpawnMobj(fixed3_zero(), 1);
    other = P_SpawnMobj(fixed3_zero(), 1);
    assert(actor && other && level.player_resources[0][0] == 0);
    assert(P_SetMobjState(actor, 1));
    assert(level.player_resources[0][0] == 2 && other->remove);
    P_FreeLevel(&level);
    assert(!level.player_resources[0][0]);
    assert(thinkercap.next == &thinkercap);
    return 0;
}
