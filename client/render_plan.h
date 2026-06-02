#ifndef OPEN_RTS_RENDER_PLAN_H
#define OPEN_RTS_RENDER_PLAN_H

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

typedef struct {
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
        const MapDecoration *decoration;
        const Unit *unit;
    } ref;
} DrawCommand;

#endif
