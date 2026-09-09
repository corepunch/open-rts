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

/* Game-owned object fields; vent indices refer to the active level. */
#define MOBJ_GAME_FIELDS dc_drop_t drop; int resource_vent_index;

#endif
