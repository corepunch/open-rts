#ifndef __W_SPR__
#define __W_SPR__
#include "m_vec.h"
#include <stdbool.h>

#define DC_VENT_SMOKE_MAX_FRAMES 32
#define DC_DROPSHIP_MAX_FRAMES 16
#define DC_DROPSHIP_MAX_PARTS 24

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

typedef struct {
    char sprite_name[32];
    ivec2_t offset;
    int sprite_frame;
    int render_remap;
    int render_intensity;
    int render_selector;
    int flags;
} DarkColonyDropshipPart;

typedef struct {
    int duration_ms;
    int part_count;
    DarkColonyDropshipPart parts[DC_DROPSHIP_MAX_PARTS];
} DarkColonyDropshipFrame;

typedef struct {
    bool valid;
    int frame_count;
    int duration_ms;
    DarkColonyDropshipFrame frames[DC_DROPSHIP_MAX_FRAMES];
} DarkColonyDropshipAnimation;

bool dark_colony_vent_placement_from_sprites(const char *map_path, DarkColonyVentPlacement *out);
bool dark_colony_dropship_animation_from_sprites(const char *map_path,
                                                 DarkColonyDropshipAnimation *out);
bool dark_colony_load_render_tables(const char *data_root, const char *tileset_name);
#endif
