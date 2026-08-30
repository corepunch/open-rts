#ifndef OPEN_RTS_RENDERER_H
#define OPEN_RTS_RENDERER_H

#include <SDL.h>

#include <stdbool.h>

typedef struct Renderer Renderer;

typedef struct RendererBackend {
    const char *id;
    const char *name;
    bool (*create)(Renderer *renderer, const char *title, int width, int height,
                   bool hidden, bool software);
    void (*destroy)(Renderer *renderer);
    void (*begin_frame)(Renderer *renderer, SDL_Color clear);
    void (*end_frame)(Renderer *renderer);
    bool (*save_screenshot)(Renderer *renderer, const char *path);
} RendererBackend;

struct Renderer {
    const RendererBackend *backend;
    SDL_Window *window;
    SDL_Renderer *sdl;
    int width;
    int height;
};

const RendererBackend *sdl_renderer_backend(void);
const RendererBackend *renderer_backend_by_id(const char *id);

bool renderer_create(Renderer *renderer, const RendererBackend *backend,
                         const char *title, int width, int height,
                         bool hidden, bool software);
void renderer_destroy(Renderer *renderer);
void renderer_begin_frame(Renderer *renderer, SDL_Color clear);
void renderer_end_frame(Renderer *renderer);
bool renderer_save_screenshot(Renderer *renderer, const char *path);

#endif
