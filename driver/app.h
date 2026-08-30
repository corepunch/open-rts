#ifndef OPEN_RTS_APP_H
#define OPEN_RTS_APP_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct App {
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
} App;

#endif
