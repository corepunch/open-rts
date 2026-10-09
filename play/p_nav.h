#ifndef __P_NAV__
#define __P_NAV__

#include "engine.h"

/* One planner for every game: integer A* over the level's eight-connected
 * grid, followed by a radius-aware string pull. Units keep the resulting
 * waypoints and steer between them (see p_steer.c).
 *
 * Every call takes a movement class (terrainspeeds_t). Class 0 is plain
 * blocked[]; classes 1.. scale the cost of entering a cell by 100/speed, so a
 * 25% swamp costs four times a road and 0% is a wall. */

/* Plan from a world position to a world goal for a disc of the given radius.
 * An unreachable goal is relocated to the nearest reachable cell in the
 * start's region. `soft` is an optional per-cell bitmap of cells occupied by
 * idle units: they are not walls, but routes pay to cross them. */
bool P_NavPlan(const level_t *map, int move_class, float radius, fvec2_t from,
               fvec2_t goal, const uint8_t *soft, navpath_t *out);

/* Connected-component test on the same grid the planner searches. */

/* True if a disc of the given radius can travel from a to b unobstructed. */
bool P_NavLineClear(const level_t *map, int move_class, fvec2_t a, fvec2_t b,
                    float radius);

void P_NavFree(level_t *map);

/* Planner work counters. Expansions drive the per-tick planning budget, so the
 * schedule is identical on every peer. */
typedef struct { uint64_t searches, expansions; } navstats_t;
navstats_t P_NavStats(void);

#endif
