#ifndef __UI_DEFINITION__
#define __UI_DEFINITION__

#include "m_vec.h"

#include <SDL.h>
#define RTS_UI_MAX_LAYERS 16

typedef struct uiimage_s {
    const char *asset_path;
    irect_t source;
    irect_t destination;
} uiimage_t;

/* Games describe native assets and layout; the client owns loading and rendering. */
typedef struct uidefinition_s {
    int logical_width;
    int logical_height;
    irect_t world_viewport;
    irect_t minimap;
    irect_t command_grid;
    int command_columns;
    int command_rows;
    SDL_Point resource_text;
    SDL_Color resource_color;
    const uiimage_t *images;
    int image_count;
} uidefinition_t;

#endif
