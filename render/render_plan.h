#ifndef __RENDER_PLAN__
#define __RENDER_PLAN__

#include "actor.h"
#include "map.h"

typedef enum {
    RENDER_LAYER_TERRAIN = 0,
    RENDER_LAYER_TERRAIN_OVERLAY = 10,
    RENDER_LAYER_DECORATION = 20,
    RENDER_LAYER_UNIT = 30,
    RENDER_LAYER_EFFECT = 40,
    RENDER_LAYER_UI = 100,
} RenderLayer;

typedef enum {
    DRAW_COMMAND_TILE_OVERLAY,
    DRAW_COMMAND_DECORATION,
    DRAW_COMMAND_UNIT,
} DrawCommandKind;

typedef struct drawcommand_s {
    DrawCommandKind kind;
    RenderLayer layer;
    float sort_y;
    int stable_index;
    union {
        struct {
            int x;
            int y;
            int layer;
        } tile_overlay;
        const mapdecoration_t *decoration;
        const mobj_t *unit;
    } ref;
} drawcommand_t;

#endif
