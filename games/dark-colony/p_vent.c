#include "game.h"
#include "info.h"
#include "p_local.h"

void A_DC_Vent(mobj_t *actor) {
    if (actor->resource_vent_index < 0 ||
        actor->resource_vent_index >= level.resource_vent_count) return;
    const resourcevent_t *vent = &level.resource_vents[actor->resource_vent_index];
    int state = S_VENT_EXHAUSTED;
    if (vent->active && vent->rate > 0 && vent->amount > 0) {
        state = S_VENT_ACTIVE1;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            const mobj_t *unit = (const mobj_t *)th;
            if (!unit->remove && unit->hp > 0 && (unit->traits & MF_HARVESTER) &&
                unit->harvest.target == actor->resource_vent_index &&
                unit->harvest.phase == HARVEST_PHASE_MINING) {
                state = S_VENT_ATTACHED;
                break;
            }
        }
    }
    P_MobjSetHidden(actor, state != S_VENT_ACTIVE1);
    if (state == S_VENT_ACTIVE1 && actor->core.state_id >= S_VENT_ACTIVE1 &&
        actor->core.state_id <= S_VENT_ACTIVE20) return;
    if (actor->core.state_id != state) P_SetMobjState(actor, state);
}
