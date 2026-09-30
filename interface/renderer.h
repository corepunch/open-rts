#ifndef __RENDERER__
#define __RENDERER__

#include <SDL.h>

#include <stdbool.h>

typedef struct renderer_s renderer_t;

typedef struct rendererbackend_s {
    const char *id;
    const char *name;
    bool (*create)(renderer_t *renderer, const char *title, int width, int height,
                   bool hidden, bool software);
    void (*destroy)(renderer_t *renderer);
    void (*begin_frame)(renderer_t *renderer, SDL_Color clear);
    void (*end_frame)(renderer_t *renderer);
    bool (*save_screenshot)(renderer_t *renderer, const char *path);
} rendererbackend_t;

struct renderer_s {
    const rendererbackend_t *backend;
    SDL_Window *window;
    SDL_Renderer *sdl;
    int width;
    int height;
};

const rendererbackend_t *sdl_renderer_backend(void);
const rendererbackend_t *renderer_backend_by_id(const char *id);

bool renderer_create(renderer_t *renderer, const rendererbackend_t *backend,
                         const char *title, int width, int height,
                         bool hidden, bool software);
void renderer_destroy(renderer_t *renderer);
void renderer_begin_frame(renderer_t *renderer, SDL_Color clear);
void renderer_end_frame(renderer_t *renderer);
bool renderer_save_screenshot(renderer_t *renderer, const char *path);

#endif
