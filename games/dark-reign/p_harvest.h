#ifndef __P_HARVEST__
#define __P_HARVEST__

#include "actor.h"

bool DR_HarvestDropoffMatches(const mobj_t *unit,
                              int resource_type, const mobj_t *base,
                              fvec2_t *position);

#endif
