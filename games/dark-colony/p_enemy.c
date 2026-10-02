#include "engine.h"
#include "info.h"
#include "dark-colony.h"

static int reaper_death_state_for_angle(angle_t angle) {
    /* Preserve the existing sparse-direction choice; retail selection is unknown. */
    int code = dc_angle_to_direction(angle);
    for (int distance = 0; distance <= 8; ++distance) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (distance == 0 && sign > 0) continue;
            int candidate = (code + sign * distance) & 15;
            int suffix = (16 - candidate) & 15;
            if (suffix == 14) return S_REAP_DIEA14_1;
            if (suffix == 10) return S_REAP_DIEA10_1;
            if (suffix == 6) return S_REAP_DIEA6_1;
            if (suffix == 2) return S_REAP_DIEA2_1;
        }
    }
    return S_NULL;
}

void A_DC_ReaperDeath(mobj_t *unit) {
    if (!unit) return;
    P_SetMobjState(unit, reaper_death_state_for_angle(unit->core.angle));
}

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
    if (actor->core.state_id != state) P_SetMobjState(actor, state);
}

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
        first = sequences[band].state;
        if (building->core.state_id == first) return;
    } else {
        bool damaged = false;
        for (int i = SCRCH; i <= BURN; ++i)
            damaged |= building->core.state_id == sequences[i].state;
        if (!damaged) return;
    }
    P_SetMobjState(building, first);
    /* 0x41856c precedes the idle action: its reset frame zero is shown
     * until the next native tick, then 0x423e0c advances to frame one. */
    building->core.tics = (66 * RTS_TICRATE + 500) / 1000;
}
