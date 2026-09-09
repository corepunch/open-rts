#include "game.h"
#include "info.h"

/* DC.EXE 0x41368a–0x4136e6: idle city animation follows remaining HP.
 * Production's mode-1 animation finishes before this selection resumes. */
void A_DC_BuildingStand(mobj_t *building) {
    if (building->hp <= 0 || building->type_id < MT_EXCOPOD ||
        building->type_id > MT_RSCHPOD || states[building->core.state_id].group != 1)
        return;
    enum { SCRCH, BURN, DIE };
    static const struct { int first, last; } sequences[MT_RSCHPOD - MT_EXCOPOD + 1][3] = {
#define DC_BUILDING_LABEL(type, action, first, last) [MT_##type - MT_EXCOPOD][action] = {first, last},
#define DC_BUILDING_STATE(id, sprite, frame, tics, action, next, group)
#include "building_states.inc"
#undef DC_BUILDING_STATE
#undef DC_BUILDING_LABEL
    };
    int band = building->hp > (building->max_hp * 11 >> 4) ? -1 :
               building->hp > (building->max_hp * 5 >> 4) ? BURN : SCRCH;
    int first = mobjinfo[building->type_id].spawnstate;
    if (band >= 0) {
        first = sequences[building->type_id - MT_EXCOPOD][band].first;
        int last = sequences[building->type_id - MT_EXCOPOD][band].last;
        if (building->core.state_id >= first && building->core.state_id <= last) return;
    } else {
        bool damaged = false;
        for (int i = SCRCH; i <= BURN; ++i)
            damaged |= building->core.state_id >= sequences[building->type_id - MT_EXCOPOD][i].first &&
                       building->core.state_id <= sequences[building->type_id - MT_EXCOPOD][i].last;
        if (!damaged) return;
    }
    P_SetMobjState(building, first);
    /* 0x41856c precedes the idle action: its reset frame zero is shown
     * until the next native tick, then 0x423e0c advances to frame one. */
    building->core.tics = (66 * RTS_TICRATE + 500) / 1000;
}
