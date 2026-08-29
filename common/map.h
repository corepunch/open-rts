#ifndef OPEN_RTS_MAP_H
#define OPEN_RTS_MAP_H

#include "engine_config.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct App App;
typedef struct Tileset Tileset;

typedef enum {
    RTS_DIRECTION_COMPASS_16 = 0,
    RTS_DIRECTION_DARK_COLONY_8 = 1,
    RTS_DIRECTION_DARK_COLONY_16 = 2,
    RTS_DIRECTION_DARK_REIGN_8 = 3,
} DirectionMode;

typedef struct {
    int x;
    int y;
} Cell;

enum {
    MAP_RENDER_USE_CELL_COLORS = 1 << 0,
    MAP_RENDER_SMOOTH_TRANSITIONS = 1 << 1,
    MAP_RENDER_SKIP_ZERO_TILES = 1 << 2,
    MAP_RENDER_INTERLEAVED_OVERLAYS = 1 << 3,
};

typedef enum {
    MAP_EXTRA_DECORATION,
    MAP_EXTRA_RESOURCE_VENT,
    MAP_EXTRA_OVERLAY,
} MapExtraKind;

typedef struct {
    MapExtraKind kind;
    int gx;
    int gy;
    int layer;
    int value;
    uint32_t flags;
} MapExtra;

typedef struct {
    int gx;
    int gy;
    int footprint_w;
    int footprint_h;
    bool solid;
    bool center_anchor;
    int frame_index;
    int frame2_index;
    int frame3_index;
    int facing_code;
    uint32_t render_flags;
    uint32_t render2_flags;
    uint32_t render3_flags;
    char sprite_name[32];
    char sprite2_name[32];
    char sprite3_name[32];
    char shadow_name[32];
    char sequence_name[16];
} MapDecoration;

typedef struct {
    int gx;
    int gy;
    int amount;
    int rate;
    bool active;
} MapResourceVent;

typedef struct GameMap {
    int width;
    int height;
    uint16_t *tile_ids;
    uint16_t *tile_overlays[MAX_TILE_OVERLAYS];
    uint8_t *tile_flip_flags[MAX_TILE_OVERLAYS + 1];
    int tile_overlay_count;
    uint8_t *blocked;
    uint32_t *cell_colors;
    uint32_t render_features;
    bool bottom_up_coordinates;
    DirectionMode direction_mode;
    MapDecoration *decorations;
    int decoration_count;
    MapResourceVent *resource_vents;
    int resource_vent_count;
    MapExtra *extras;
    int extra_count;
    bool has_camera;
    float camera_gx;
    float camera_gy;
    int player_resources[8];
    char tileset_name[32];
    void *native_data;
    void (*destroy_native_data)(void *);
    void (*render_transitions)(App *app, const struct GameMap *map, const Tileset *tileset,
                               int x, int y, int dx, int dy);
} GameMap;

static inline int map_screen_y_for_cell(const GameMap *map, int y) {
    return map && map->bottom_up_coordinates ? map->height - 1 - y : y;
}

static inline float map_screen_y_for_point(const GameMap *map, float y) {
    return map && map->bottom_up_coordinates ? (float)map->height - y : y;
}

static inline float map_world_y_from_screen_point(const GameMap *map, float y) {
    return map && map->bottom_up_coordinates ? (float)map->height - y : y;
}

#endif
