#ifndef __P_LOCAL__
#define __P_LOCAL__

#include "engine.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void debug_effects_log(const char *fmt, ...);

float P_MobjRadius(const mobj_t *unit);
bool P_CheckPosition(const level_t *map, const mobj_t *unit, float gx, float gy);
void P_ClampToLevel(const level_t *map, mobj_t *unit);

void P_MoveOrderAt(const level_t *map, mobj_t *units, int unit_count,
                         fvec2_t goal_position);
bool P_MoveUnitTo(const level_t *map, mobj_t *unit, fvec2_t goal_position);
bool P_HarvestOrderAt(const level_t *map, mobj_t *units, int unit_count,
                             fvec2_t position);

#endif
