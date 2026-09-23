#ifndef __P_HARVEST__
#define __P_HARVEST__

#include "actor.h"

bool DR_HarvestDropoffMatches(const mobj_t *unit,
                              const resourcevent_t *vent,
                              const mobj_t *base);

#endif
