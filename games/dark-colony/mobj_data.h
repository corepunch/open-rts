#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

#include "m_vec.h"

enum { DROPSHIP_MAX_PAYLOAD_TYPES = 5 };
typedef struct { int type; int count; } DropshipPayload;

typedef struct {
    ivec2_t origin;
    DropshipPayload payload[DROPSHIP_MAX_PAYLOAD_TYPES];
    int payload_count;
    int payload_index;
    int released_count;
} dc_drop_t;

enum { DC_MAX_WAYPOINTS = 8 };
typedef struct {
    ivec2_t points[DC_MAX_WAYPOINTS];
    int count;
    int current;
} dc_waypoints_t;

/* DC.EXE 0x41434e stores the next 32 cell steps of a backward search. */
typedef struct {
    ivec2_t cells[32];
    int count, current;
    bool traveling;
} dc_route_t;

/* Game-owned object fields; vent indices refer to the active level. */
#define MOBJ_GAME_FIELDS \
    dc_drop_t drop; \
    int resource_vent_index; \
    dc_waypoints_t waypoints; \
    dc_route_t route; \
    uint32_t producer_id; \
    uint32_t detected_by; \
    int repair_wait; \
    uint8_t ability_charge; /* Native object +0x0a, shared by special abilities. */

#define MOBJ_GAME_CHECKSUM(HASH, actor) do { \
    HASH((actor)->ability_charge); \
    HASH((actor)->repair_wait); \
    HASH((actor)->detected_by); \
} while (0)

#endif
