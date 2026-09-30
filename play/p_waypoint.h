#ifndef __P_WAYPOINT__
#define __P_WAYPOINT__

#include "m_vec.h"

/* DC has eight route destinations. DR's native traversal modes are 0/1/2. */
enum { MAXWAYPOINTS = 8 };
typedef enum { WP_BACKTRACK, WP_LOOP, WP_ONCE } waypointmode_t;
typedef struct {
    ivec2_t points[MAXWAYPOINTS];
    int count, current;
    waypointmode_t mode;
    bool backwards;
} waypoints_t;

#endif
