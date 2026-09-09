#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    static const state_t states[] = {
        {0},
        { .tics = -1, .nextstate = 1 },
        { .tics = 2, .nextstate = 0 },
    };
    const gameinfo_t info = { .states = states, .state_count = 3 };
    level_t map = {0};
    mobj_t *units = calloc(MAXMOBJS, sizeof(*units));
    assert(units);
    int count = 0;
    assert(!P_AllocMobj(NULL, &count) && count == 0);
    assert(!P_AllocMobj(units, NULL));
    for (int i = 0; i < MAXMOBJS; ++i) {
        mobj_t *unit = P_AllocMobj(units, &count);
        assert(unit == &units[i] && count == i + 1);
        unit->id = i + 1;
        unit->hp = 10;
        unit->core.state_id = 1;
        unit->core.tics = -1;
    }
    units[1].hp = 0; /* A persistent corpse remains an object. */
    units[7].hp = 0;
    units[7].core.state_id = 2;
    units[7].core.tics = 2; /* This death animation has not finished. */
    units[3].remove = true;
    assert(P_EnsureMobjProduction(&units[3]));
    assert(!P_AllocMobj(units, &count)); /* Removal is still pending. */
    assert(count == MAXMOBJS && units[3].remove);

    P_Ticker(&map, units, &count, NULL, 0, &info, FIXED_DT);
    assert(count == MAXMOBJS - 1);
    assert(units[1].id == 2 && units[1].hp == 0);
    assert(units[6].id == 8 && units[6].core.tics == 1);
    mobj_t *unit = P_AllocMobj(units, &count);
    assert(unit == &units[MAXMOBJS - 1]);
    unsigned char zero[sizeof(*unit)] = {0};
    assert(memcmp(unit, zero, sizeof(*unit)) == 0);
    unit->id = MAXMOBJS + 1;
    unit->core.state_id = 1;
    unit->core.tics = -1;

    P_Ticker(&map, units, &count, NULL, 0, &info, FIXED_DT);
    assert(count == MAXMOBJS - 1); /* Finished death reached S_NULL. */
    assert(units[1].id == 2); /* Persistent corpse was not reclaimed. */
    for (int i = 0; i < count; ++i) assert(units[i].id != 8);
    assert(P_AllocMobj(units, &count) && count == MAXMOBJS);
    assert(!P_AllocMobj(units, &count));
    free(units);
    puts("PASS: shared mobj allocation, deferred reclamation, and zeroed reuse");
    return 0;
}
