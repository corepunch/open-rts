#ifndef __W_SPR__
#define __W_SPR__
#include "m_vec.h"
#include <stdbool.h>

#define DC_VENT_SMOKE_MAX_FRAMES 32

typedef struct {
    ivec2_t pivot;
    int sprite_frame;
    int duration_ms;
} DarkColonyVentSmokeFrame;

typedef struct {
    bool valid;
    int glow_left;
    int glow_top;
    int smoke_frame_count;
    DarkColonyVentSmokeFrame smoke_frames[DC_VENT_SMOKE_MAX_FRAMES];
} DarkColonyVentPlacement;
bool dark_colony_vent_placement_from_sprites(const char *map_path, DarkColonyVentPlacement *out);
#endif
