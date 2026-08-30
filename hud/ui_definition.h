#ifndef __UI_DEFINITION__
#define __UI_DEFINITION__

#include "m_vec.h"

#include <SDL.h>
#define RTS_UI_MAX_LAYERS 16
#define RTS_UI_MAX_RESOURCES 8

typedef struct uiimage_s {
    const char *asset_path;
    irect_t source;
    irect_t destination;
} uiimage_t;

typedef struct uiresource_s {
    SDL_Point text; /* right-aligned amount anchor in logical UI coordinates */
    SDL_Color color;
} uiresource_t;

typedef struct uipanel_s {
    irect_t rect;
    SDL_Color fill;
    SDL_Color border;
} uipanel_t;

/* Games describe native assets and layout; the client owns loading and rendering. */
typedef struct uidefinition_s {
    int logical_width;
    int logical_height;
    irect_t world_viewport;
    irect_t minimap;
    irect_t command_grid;
    int command_columns;
    int command_rows;
    uiresource_t resources[RTS_UI_MAX_RESOURCES];
    int resource_count;
    uipanel_t status_panel;
    bool status_elapsed_time;
    uipanel_t sidebar_panel;
    int sidebar_cell_size;
    const uiimage_t *images;
    int image_count;
} uidefinition_t;

#endif
