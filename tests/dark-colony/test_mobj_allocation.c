#include "game.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    static const state_t states[] = {
        {0}, { .tics = -1 }, { .tics = 2, .nextstate = 0 },
    };
    const gameinfo_t info = { .states = states, .state_count = 3 };
    gameinfo = &info;
    P_InitThinkers();
    mobj_t *first = P_SpawnMobj(fixed3_zero(), 0);
    mobj_t *removed = P_SpawnMobj(fixed3_zero(), 0);
    mobj_t *corpse = P_SpawnMobj(fixed3_zero(), 0);
    assert(first && removed && corpse);
    assert(P_SetMobjState(first, 1) && P_SetMobjState(corpse, 1));
    assert(P_EnsureMobjProduction(removed));
    first->attack.target = removed;
    P_RemoveMobj(removed);
    assert(!first->attack.target && removed->thinker.function == NULL);
    assert(first->thinker.next == &removed->thinker); /* Deferred unlink. */
    for (int i = 0; i < 1000; ++i) {
        mobj_t *actor = P_SpawnMobj(fixed3_zero(), 0);
        assert(actor && P_SetMobjState(actor, 1));
    }
    P_Ticker();
    assert(thinkercap.next == &first->thinker);
    assert(first->thinker.next == &corpse->thinker);
    assert(corpse->hp == 0 && corpse->core.tics == -1);
    mobjlist_t objects = P_ListMobjs();
    assert(objects.count == 1002 && objects.items[0] == first);
    P_FreeMobjList(&objects);
    assert(P_SetMobjState(first, 2));
    P_Ticker();
    assert(first->core.tics == 1);
    P_Ticker();
    assert(first->remove && !first->thinker.function);
    P_Ticker();
    assert(thinkercap.next == &corpse->thinker);
    P_FreeLevel(&level);
    assert(thinkercap.next == &thinkercap && thinkercap.prev == &thinkercap);
    puts("PASS: unbounded mobj allocation, stable addresses, deferred removal, persistent corpses");
    return 0;
}
