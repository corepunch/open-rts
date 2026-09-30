#include "dark-colony.h"
#include "engine.h"

ivec2_t DC_OccupiedPosition(const mobj_t *unit) {
    return fvec2_cell(fixed3_xy_to_fvec2(unit->core.position));
}

mobj_t *DC_Occupant(ivec2_t cell, bool airborne, bool buried) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (unit->remove || unit->hp <= 0 || (unit->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
            !!(unit->traits & MF_FLY) != airborne ||
            !!(unit->traits & MF_LANDMINE) != buried) continue;
        if (ivec2_equal(cell, DC_OccupiedPosition(unit))) return unit;
    }
    return NULL;
}
