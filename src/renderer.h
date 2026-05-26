#ifndef OPEN_RTS_RENDERER_H
#define OPEN_RTS_RENDERER_H

#include <SDL.h>

#include <stdbool.h>

typedef struct RtsRenderer RtsRenderer;

typedef struct RtsRendererBackend {
    const char *id;
    const char *name;
    bool (*create)(RtsRenderer *renderer, const char *title, int width, int height,
                   bool hidden, bool software);
    void (*destroy)(RtsRenderer *renderer);
    void (*begin_frame)(RtsRenderer *renderer, SDL_Color clear);
    void (*end_frame)(RtsRenderer *renderer);
    bool (*save_screenshot)(RtsRenderer *renderer, const char *path);
} RtsRendererBackend;

struct RtsRenderer {
    const RtsRendererBackend *backend;
    SDL_Window *window;
    SDL_Renderer *sdl;
    int width;
    int height;
};

const RtsRendererBackend *rts_sdl_renderer_backend(void);
const RtsRendererBackend *rts_renderer_backend_by_id(const char *id);

bool rts_renderer_create(RtsRenderer *renderer, const RtsRendererBackend *backend,
                         const char *title, int width, int height,
                         bool hidden, bool software);
void rts_renderer_destroy(RtsRenderer *renderer);
void rts_renderer_begin_frame(RtsRenderer *renderer, SDL_Color clear);
void rts_renderer_end_frame(RtsRenderer *renderer);
bool rts_renderer_save_screenshot(RtsRenderer *renderer, const char *path);

#endif
