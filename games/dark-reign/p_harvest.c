#include "dark-reign.h"

#include "info.h"
#include "engine.h"

bool DR_HarvestDropoffMatches(const mobj_t *unit,
                              int resource_type, const mobj_t *base,
                              fvec2_t *position) {
    (void)unit;
    const dr_mission_t *mission = level.mission;
    if (!mission || !base || base->type_id >= gameinfo->mobj_type_count) return false;
    ivec2_t bay = mission->bays[base->type_id];
    if (bay.x < 0 || bay.y < 0) return false;
    *position = fvec2_add(fixed3_xy_to_fvec2(base->core.position), fvec2_cell_center(bay));

    switch (resource_type) {
        case 0:
            return base->type_id == MT_FG_LIFE_PLANT ||
                   base->type_id == MT_IMP_LIFE_PLANT;
        case 1:
            return base->type_id == MT_FG_POWER_PLANT ||
                   base->type_id == MT_IMP_POWER_PLANT;
        default:
            return false;
    }
}
