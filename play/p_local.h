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

enum {
    HARVEST_PHASE_NONE = 0,
    HARVEST_PHASE_TO_MINE = 1,
    HARVEST_PHASE_MINING = 2,
    HARVEST_PHASE_TO_BASE = 3,
    HARVEST_PHASE_TURNING = 4,
};

void debug_effects_log(const char *fmt, ...);

float P_MobjRadius(const mobj_t *unit);

static inline isize2_t P_ResourceVentFootprint(const resourcevent_t *vent) {
    int w = vent && vent->footprint.w > 0 ? vent->footprint.w : 1;
    int h = vent && vent->footprint.h > 0 ? vent->footprint.h : 1;
    return (isize2_t){ w, h };
}

static inline bool P_ResourceVentContainsCell(const resourcevent_t *vent, ivec2_t cell) {
    if (!vent) return false;
    isize2_t fp = P_ResourceVentFootprint(vent);
    return cell.x >= vent->cell.x && cell.x < vent->cell.x + fp.w &&
           cell.y >= vent->cell.y && cell.y < vent->cell.y + fp.h;
}

static inline float P_ResourceVentRadius(const resourcevent_t *vent) {
    isize2_t fp = P_ResourceVentFootprint(vent);
    float hx = (float)fp.w * 0.5f, hy = (float)fp.h * 0.5f;
    float r = sqrtf(hx * hx + hy * hy);
    return r > 1.45f ? r : 1.45f;
}
bool P_CheckPosition(const level_t *map, const mobj_t *unit, float gx, float gy);
bool P_TryMove(mobj_t *unit, fixed3_t position);
void P_ClampToLevel(const level_t *map, mobj_t *unit);
bool P_FlowFieldTarget(const level_t *map, const flowfield_t *field,
                       fvec2_t position, fvec2_t goal, float radius,
                       fvec2_t *target, bool *final);
void P_FreeFlowFields(level_t *map);
void P_MoveUnitsAt(const level_t *map, mobj_t *const *units, int count, fvec2_t goal);
bool P_HarvestUnitsAt(const level_t *map, mobj_t *const *units, int count, fvec2_t goal);

void P_MoveOrderAt(const level_t *map, mobj_t *const *units, int unit_count,
                         fvec2_t goal_position);
bool P_MoveUnitTo(const level_t *map, mobj_t *unit, fvec2_t goal_position);
bool P_HarvestOrderAt(const level_t *map, mobj_t *const *units, int unit_count,
                             fvec2_t position);
bool P_HarvestUnitTo(const level_t *map, mobj_t *unit, fvec2_t position);

#endif
