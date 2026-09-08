#ifndef __W_DROP__
#define __W_DROP__

#include "m_vec.h"
#include <stdbool.h>

#define DROPSHIP_MAX_FRAMES 16
#define DROPSHIP_MAX_PARTS 24

typedef struct {
    char sprite_name[32];
    ivec2_t offset;
    int sprite_frame;
    int render_remap;
    int render_intensity;
    int render_selector;
    int flags;
} DropshipPart;

typedef struct {
    int duration_ms;
    int part_count;
    DropshipPart parts[DROPSHIP_MAX_PARTS];
} DropshipFrame;

typedef struct {
    bool valid;
    int frame_count;
    int duration_ms;
    DropshipFrame frames[DROPSHIP_MAX_FRAMES];
} DropshipAnimation;

typedef struct {
    DropshipAnimation move;
    DropshipAnimation stand;
    DropshipAnimation unload;
} DropshipAnimations;

bool DC_LoadDropshipAnimations(const char *map_path,
                                                 DropshipAnimations *out);

#endif
