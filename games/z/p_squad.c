#include "z_local.h"

/* Native 0x77b97 promotes the first occupied follower slot and transfers
 * the leader's order. Replan from the survivor, not the departed path origin. */
void Z_SquadLeave(mobj_t *unit) {
    if (!unit || !unit->z.squad || !unit->z.squad_slot) return;
    mobj_t *next = NULL;
    if (unit->z.squad_slot == UINT8_MAX) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *mo = (mobj_t *)th;
            if (mo == unit || mo->remove || mo->hp <= 0 || mo->team != unit->team ||
                mo->z.squad != unit->z.squad || !mo->z.squad_slot || mo->z.squad_slot == UINT8_MAX ||
                !Z_IsRobot(Z_UnitType(mo))) continue;
            if (!next || mo->z.squad_slot < next->z.squad_slot) next = mo;
        }
    }
    if (next) {
        next->z.squad_slot = UINT8_MAX;
        next->z.board = 0;
        next->z.service = 0;
        P_ClearMove(next);
        next->z.route = unit->z.route;
        next->waypoints = unit->waypoints;
        next->move_only = unit->move_only;
        next->attack.target = unit->attack.target;
        if (P_HasMoveOrder(unit)) P_MoveUnitTo(&level, next, unit->movement.goal);
        if (P_MobjIsSelected(unit)) P_MobjSetSelected(next, true);
    }
    unit->z.squad = unit->z.squad_slot = 0;
}

/* Crew can leave at different times, and the departing leader may have
 * promoted a member still outside. Reuse only slots not occupied there. */
void Z_SquadEjected(mobj_t **units, int count) {
    if (!count || !units[0]->z.squad) return;
    bool used[Z_CREW_MAX] = {false}, leader = false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (mo->remove || mo->hp <= 0 || mo->team != units[0]->team ||
            mo->z.squad != units[0]->z.squad || !Z_IsRobot(Z_UnitType(mo))) continue;
        if (mo->z.squad_slot == UINT8_MAX) leader = true;
        else if (mo->z.squad_slot < Z_CREW_MAX) used[mo->z.squad_slot] = true;
    }
    for (int i = 0; i < count; ++i) {
        if (!leader) { units[i]->z.squad_slot = UINT8_MAX; leader = true; continue; }
        int slot = 1;
        while (slot < Z_CREW_MAX && used[slot]) ++slot;
        if (slot == Z_CREW_MAX) units[i]->z.squad = units[i]->z.squad_slot = 0;
        else { units[i]->z.squad_slot = (uint8_t)slot; used[slot] = true; }
    }
}
