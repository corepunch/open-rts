#ifndef __UI_DEFINITION__
#define __UI_DEFINITION__

#include <SDL.h>
#define RTS_UI_MAX_LAYERS 16

typedef struct uiimage_s {
    const char *asset_path;
    SDL_Rect source;
    SDL_Rect destination;
} uiimage_t;

/* Games describe native assets and layout; the client owns loading and rendering. */
typedef struct uidefinition_s {
    int logical_width;
    int logical_height;
    SDL_Rect world_viewport;
    SDL_Rect minimap;
    SDL_Rect command_grid;
    int command_columns;
    int command_rows;
    SDL_Point resource_text;
    SDL_Color resource_color;
    const uiimage_t *images;
    int image_count;
} uidefinition_t;

#endif
