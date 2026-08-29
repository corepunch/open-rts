#ifndef OPEN_RTS_UI_DEFINITION_H
#define OPEN_RTS_UI_DEFINITION_H

#include <SDL.h>
#define RTS_UI_MAX_LAYERS 16

typedef struct {
    const char *asset_path;
    SDL_Rect source;
    SDL_Rect destination;
} GameUiImage;

/* Games describe native assets and layout; the client owns loading and rendering. */
typedef struct {
    int logical_width;
    int logical_height;
    SDL_Rect world_viewport;
    SDL_Rect minimap;
    SDL_Rect command_grid;
    int command_columns;
    int command_rows;
    SDL_Point resource_text;
    SDL_Color resource_color;
    const GameUiImage *images;
    int image_count;
} GameUiDefinition;

#endif
