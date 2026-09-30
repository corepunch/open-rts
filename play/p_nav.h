#ifndef __P_NAV__
#define __P_NAV__

#include "engine.h"

/* One planner for every game: integer A* over the level's eight-connected
 * walkability grid, followed by a radius-aware string pull. Units keep the
 * resulting waypoints and steer between them (see p_steer.c). */

struct nav_s;

/* Plan from a world position to a world goal for a disc of the given radius.
 * An unreachable goal is relocated to the nearest reachable cell in the
 * start's region. Returns false when start and goal share no region. */
bool P_NavPlan(const level_t *map, float radius, fvec2_t from, fvec2_t goal,
               navpath_t *out);

/* Connected-component test on the same grid the planner searches. */
bool P_NavReachable(const level_t *map, ivec2_t from, ivec2_t to);

/* True if a disc of the given radius can travel from a to b unobstructed. */
bool P_NavLineClear(const level_t *map, fvec2_t a, fvec2_t b, float radius);

/* Nearest cell to `wanted` in the same region as `from`, within `radius` cells. */
bool P_NavNearestReachable(const level_t *map, ivec2_t from, ivec2_t wanted,
                           int radius, ivec2_t *out);

void P_NavFree(level_t *map);

/* Planner statistics for tests and profiling. */
typedef struct { uint64_t searches, expansions; } navstats_t;
navstats_t P_NavStats(void);

#endif
