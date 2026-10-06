#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

/* Wargus stratagus.lua ActionVictory/ActionDefeat: a side that has lost every
 * building and worker is out. You win when no opponent is left and lose when
 * you are out. A map with no opponent (a sandbox) never ends. */

static bool done;
static int countdown;

static bool alive(int owner, mobj_t *const *units, int count) {
    for (int i = 0; i < count; ++i) {
        const mobj_t *unit = units[i];
        if (unit->owner != owner || unit->hp <= 0 || unit->remove) continue;
        if (unit->type_id >= (uint16_t)(W2_TYPE_COUNT + 1)) continue;
        if (mobjinfo[unit->type_id].w2.flags & (W2_STRUCTURE | W2_HARVEST)) return true;
    }
    return false;
}

void W2_VictoryReset(void) {
    done = false;
    countdown = 0;
}

void W2_CheckVictory(mobj_t *const *units, int count) {
    if (done || netgame || menuactive || --countdown > 0) return;
    countdown = 30;
    const w2_pud_t *pud = level.native_data;
    if (!pud || consoleplayer < 0 || consoleplayer >= 8) return;
    int opponents = 0;
    bool opponent_left = false;
    for (int i = 0; i < 8; ++i) {
        if (i == consoleplayer || (pud->owners[i] != 4 && pud->owners[i] != 5)) continue;
        ++opponents;
        opponent_left = opponent_left || alive(i, units, count);
    }
    if (!opponents) return;
    bool survived = alive(consoleplayer, units, count);
    if (survived && opponent_left) return;
    done = true;
    W2_ShowResult(survived);
}
