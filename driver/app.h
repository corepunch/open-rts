#ifndef __APP__
#define __APP__

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "m_vec.h"

typedef struct app_s {
    SDL_Window *window;
    SDL_Renderer *renderer;
    isize2_t win;
    isize2_t cell;
    fvec2_t  cam;
    bool show_grid;
    bool show_blocked;
    bool running;
    bool dragging_select;
    bool panning;
    ivec2_t  mouse_down;
    ivec2_t  mouse;
    uint32_t ticks_ms;
    irect_t selection_rect;
} app_t;

#endif
