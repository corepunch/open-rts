#ifndef __APP__
#define __APP__

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct app_s {
    SDL_Window *window;
    SDL_Renderer *renderer;
    int win_w;
    int win_h;
    int cell_w;
    int cell_h;
    float cam_x;
    float cam_y;
    bool show_grid;
    bool show_blocked;
    bool running;
    bool dragging_select;
    bool panning;
    int mouse_down_x;
    int mouse_down_y;
    int mouse_x;
    int mouse_y;
    uint32_t ticks_ms;
    SDL_Rect selection_rect;
} app_t;

#endif
