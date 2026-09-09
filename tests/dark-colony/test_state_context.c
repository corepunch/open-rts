#include "engine.h"
#include <assert.h>
#include <stdio.h>

static statecontext_t contexts[2];
static mobj_t nested;
static int observed[2];

static void A_Observe(mobj_t *actor) {
    assert(P_GetStateContext() == &contexts[actor->team]);
    observed[actor->team]++;
}

static void A_Nested(mobj_t *actor) {
    statecontext_t *outer = P_GetStateContext();
    assert(outer == &contexts[0] && actor->team == 0);
    /* A different world may be entered recursively; its zero-tic chain must
     * see that world, then leave the caller's services active. */
    assert(P_SetMobjState(&contexts[1], &nested, 2));
    assert(P_GetStateContext() == outer && observed[1] == 2);
    assert(!P_SetMobjState(&contexts[1], &nested, 0));
    assert(P_GetStateContext() == outer);
    statecontext_t invalid = {0};
    assert(!P_SetMobjState(&invalid, actor, 2));
    assert(P_GetStateContext() == outer);
}

int main(void) {
    static const state_t states[] = {
        {0},
        { .tics = 0, .action = A_Nested, .nextstate = 2 },
        { .tics = 0, .action = A_Observe, .nextstate = 3 },
        { .tics = -1, .action = A_Observe, .nextstate = 0 },
    };
    const gameinfo_t game = { .states = states, .state_count = 4 };
    level_t maps[2] = {0};
    for (int i = 0; i < 2; ++i)
        contexts[i] = (statecontext_t){ .map = &maps[i], .game_info = &game };
    mobj_t actor = {0};
    nested.team = 1;
    assert(P_GetStateContext() == NULL);
    assert(P_SetMobjState(&contexts[0], &actor, 1));
    assert(actor.core.state_id == 3 && actor.core.tics == -1 && nested.remove);
    assert(observed[0] == 2 && observed[1] == 2);
    assert(P_GetStateContext() == NULL);
    assert(P_TickMobjState(&contexts[0], &actor));
    assert(observed[0] == 2 && P_GetStateContext() == NULL);
    assert(!P_SetMobjState(&contexts[0], &actor, 0));
    assert(P_GetStateContext() == NULL);
    puts("PASS: actor-only actions, nested world context, zero-tic chains, and restoration");
    return 0;
}
