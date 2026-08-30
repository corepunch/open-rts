#ifndef OPEN_RTS_ENGINE_INTERNAL_H
#define OPEN_RTS_ENGINE_INTERNAL_H

#include "engine.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void debug_effects_log(const char *fmt, ...);

float unit_radius_cells(const Mobj *unit);
bool unit_position_walkable(const GameMap *map, const Mobj *unit, float gx, float gy);
void clamp_unit_position_to_map(const GameMap *map, Mobj *unit);

void issue_move_order_at(const GameMap *map, Mobj *units, int unit_count,
                             float goal_gx, float goal_gy);
bool issue_harvest_order_at(const GameMap *map, Mobj *units, int unit_count,
                                float gx, float gy);

#endif
