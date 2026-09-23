#include "p_harvest.h"

#include "info.h"

bool DR_HarvestDropoffMatches(const mobj_t *unit,
                              const resourcevent_t *vent,
                              const mobj_t *base) {
    (void)unit;
    if (!vent || !base) return false;

    switch (vent->resource_type) {
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
