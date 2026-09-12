#include "p_ai.h"
#include "dc_types.h"
#include "d_net.h"
#include <math.h>

static int ai_nearest_vent(const level_t *map, fvec2_t position) {
    int best = -1;
    float best_distance2 = INFINITY;
    if (!map || !map->resource_vents) return best;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->amount <= 0 || vent->rate <= 0) continue;
        float distance2 = fvec2_distance_squared(position, vent->attachment);
        if (distance2 < best_distance2) {
            best_distance2 = distance2;
            best = i;
        }
    }
    return best;
}

static void update_ai_economy(const level_t *map, mobj_t *const *units,
                                          int unit_count) {
    if (!map || !units) return;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *unit = units[i];
        if (unit->remove || unit->hp <= 0 || D_PlayerIsHuman(unit->owner) ||
            (unit->traits & (MF_MOBILE | MF_HARVESTER)) !=
                (MF_MOBILE | MF_HARVESTER) || unit->harvest.target >= 0) continue;
        int vent_index = ai_nearest_vent(
            map, fixed3_xy_to_fvec2(unit->core.position));
        if (vent_index >= 0)
            P_HarvestUnitTo(map, unit, map->resource_vents[vent_index].attachment);
    }
}

/* Scripted routes repeat in DC.EXE 0x415364. Ordinary mobjs already acquire
 * nearby enemies through A_Look/P_MobjThinker; an AI-enabled team is not an
 * order for every unit to pursue targets across the map. */
void DC_UpdateAI(const level_t *map, mobj_t *const *units, int unit_count) {
    if (!map || !units) return;
    if (map_has_ai(map, 1)) update_ai_economy(map, units, unit_count);
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *actor = units[i];
        if (netgame && D_PlayerIsHuman(actor->owner)) continue;
        if (actor->remove || actor->hp <= 0 || actor->waypoints.count == 0) continue;
        mobj_t *target = actor->attack.target;
        float range = actor->info ? actor->info->attack.range : 0;
        if (target && !target->remove && target->hp > 0 && !P_IsAlly(actor, target) &&
            !(target->traits & MF_NOBLOCKMAP) &&
            fvec2_distance_squared(fixed3_xy_to_fvec2(actor->core.position),
                                   fixed3_xy_to_fvec2(target->core.position)) <= range * range)
            continue;
        actor->attack.target = NULL;
        if (actor->movement.order_arrived) {
            actor->waypoints.current = (actor->waypoints.current + 1) % actor->waypoints.count;
            actor->movement.order_arrived = false;
            actor->movement.flow_field = NULL;
        }
        if (!actor->movement.flow_field)
            P_MoveUnitTo(map, actor, fvec2_cell_center(actor->waypoints.points[actor->waypoints.current]));
    }
}
