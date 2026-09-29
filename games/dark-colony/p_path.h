#ifndef __P_PATH__
#define __P_PATH__

#include "engine.h"

bool DC_LoadPaths(level_t *map, const char *path);
void DC_FreePaths(level_t *map);
int DC_FindPath(const level_t *map, ivec2_t start, ivec2_t goal,
                const mobj_t *mover, bool occupied, ivec2_t *route, int capacity);
bool DC_MoveUnitTo(const level_t *map, mobj_t *unit, fvec2_t goal);
bool DC_MoveTarget(const level_t *map, mobj_t *unit, fvec2_t *target, bool *final);
bool DC_CheckStep(const level_t *map, const mobj_t *unit, fvec2_t from, fvec2_t to);

#endif
