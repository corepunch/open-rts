#include "game.h"
#include "info.h"

/* DC.EXE 0x41368a–0x4136e6: idle city animation follows remaining HP.
 * Main-channel construction finishes before this selection resumes. */
void A_DC_BuildingStand(mobj_t *building) {
    if (building->hp <= 0 || building->type_id < MT_EXCOPOD ||
        building->type_id > MT_RSCHPOD || states[building->core.state_id].group != 1)
        return;
    enum { SCRCH, BURN };
    const dc_building_sequence_t *sequences = dc_building_sequences[building->type_id - MT_EXCOPOD];
    int band = building->hp > (building->max_hp * 11 >> 4) ? -1 :
               building->hp > (building->max_hp * 5 >> 4) ? BURN : SCRCH;
    int first = mobjinfo[building->type_id].spawnstate;
    if (band >= 0) {
        first = sequences[band].first;
        int last = sequences[band].last;
        if (building->core.state_id >= first && building->core.state_id <= last) return;
    } else {
        bool damaged = false;
        for (int i = SCRCH; i <= BURN; ++i)
            damaged |= building->core.state_id >= sequences[i].first &&
                       building->core.state_id <= sequences[i].last;
        if (!damaged) return;
    }
    P_SetMobjState(building, first);
    /* 0x41856c precedes the idle action: its reset frame zero is shown
     * until the next native tick, then 0x423e0c advances to frame one. */
    building->core.tics = (66 * RTS_TICRATE + 500) / 1000;
}
