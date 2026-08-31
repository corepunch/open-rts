#ifndef __MAP__
#define __MAP__

#include "engine_config.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct app_s app_t;
typedef struct tileset_s tileset_t;

typedef struct cell_s {
    int x;
    int y;
} cell_t;

enum {
    MAP_RENDER_CAP_CELL_COLORS = 1 << 0,
    MAP_RENDER_CAP_TERRAIN_TRANSITIONS = 1 << 1,
    MAP_RENDER_CAP_ZERO_TILE_EMPTY = 1 << 2,
    MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS = 1 << 3,
    MAP_RENDER_CAP_TILE_TRANSFORMS = 1 << 4,
};

enum {
    MAP_TILE_TRANSFORM_FLIP_X = 1 << 0,
    MAP_TILE_TRANSFORM_FLIP_Y = 1 << 1,
};

typedef enum {
    MAP_EXTRA_DECORATION,
    MAP_EXTRA_RESOURCE_VENT,
    MAP_EXTRA_OVERLAY,
} MapExtraKind;

typedef struct mapextra_s {
    MapExtraKind kind;
    int gx;
    int gy;
    int layer;
    int value;
    uint32_t flags;
} mapextra_t;

typedef struct mapdecoration_s {
    int gx;
    int gy;
    int footprint_w;
    int footprint_h;
    bool solid;
    bool hidden;
    bool center_anchor;
    /* Optional authored sprite pivot.  When set, (pivot_x, pivot_y) in the
       sprite canvas is attached directly to the decoration's (gx, gy) world
       point.  This lets a game plugin preserve its native placement convention
       without deriving an origin from opaque bounds or a guessed footprint. */
    bool has_sprite_pivot;
    int sprite_pivot_x;
    int sprite_pivot_y;
    int frame_interval_ms;
    int frame_index;
    int frame2_index;
    int frame3_index;
    angle_t angle;
    uint32_t render_flags;
    uint32_t render2_flags;
    uint32_t render3_flags;
    char sprite_name[32];
    char sprite2_name[32];
    char sprite3_name[32];
    char shadow_name[32];
    char sequence_name[16];
} mapdecoration_t;

#define RTS_MAX_RESOURCES 8

typedef struct resourcevent_s {
    int gx;
    int gy;
    /* Visual/interaction attachment point inside the authored vent stamp.
       gx/gy remain the integer scenario coordinates used by scripts. */
    float attach_gx;
    float attach_gy;
    int amount;
    int rate;
    bool active;
    int resource_type; /* 0-based index into player_resources[][resource_type] */
    int decoration_index; /* active mine visual, or -1 when not represented */
    int smoke_decoration_index; /* unattached vent smoke, or -1 when not represented */
} resourcevent_t;

typedef struct level_s {
    int width;
    int height;
    uint16_t *tile_ids;
    uint16_t *tile_overlays[MAX_TILE_OVERLAYS];
    uint8_t *tile_transforms[MAX_TILE_OVERLAYS + 1];
    int tile_overlay_count;
    uint8_t *blocked;
    uint32_t *cell_colors;
    uint32_t render_capabilities;
    bool bottom_up_coordinates;
    mapdecoration_t *decorations;
    int decoration_count;
    resourcevent_t *resource_vents;
    int resource_vent_count;
    mapextra_t *extras;
    int extra_count;
    bool has_camera;
    float camera_gx;
    float camera_gy;
    int player_resources[8][RTS_MAX_RESOURCES];
    char tileset_name[32];
    void *native_data;
    void (*destroy_native_data)(void *);
    void (*render_transitions)(app_t *app, const struct level_s *map, const tileset_t *tileset,
                               int x, int y, int dx, int dy);
} level_t;

static inline int L_ScreenY(const level_t *map, int y) {
    return map && map->bottom_up_coordinates ? map->height - 1 - y : y;
}

static inline float L_ScreenYF(const level_t *map, float y) {
    return map && map->bottom_up_coordinates ? (float)map->height - y : y;
}

static inline float L_WorldYF(const level_t *map, float y) {
    return map && map->bottom_up_coordinates ? (float)map->height - y : y;
}

#endif
