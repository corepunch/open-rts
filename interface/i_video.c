#include "renderer.h"

#include <stdio.h>
#include <string.h>

static bool sdl_renderer_create(Renderer *renderer, const char *title, int width, int height,
                                bool hidden, bool software) {
    memset(renderer, 0, sizeof(*renderer));
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }

    renderer->width = width;
    renderer->height = height;
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : 0);
    renderer->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                        width, height, window_flags);
    Uint32 renderer_flags = software ? SDL_RENDERER_SOFTWARE :
                                      (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    renderer->sdl = renderer->window ? SDL_CreateRenderer(renderer->window, -1, renderer_flags) : NULL;
    if (!renderer->window || !renderer->sdl) {
        fprintf(stderr, "SDL window/renderer: %s\n", SDL_GetError());
        if (renderer->sdl) SDL_DestroyRenderer(renderer->sdl);
        if (renderer->window) SDL_DestroyWindow(renderer->window);
        SDL_Quit();
        memset(renderer, 0, sizeof(*renderer));
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer->sdl, SDL_BLENDMODE_BLEND);
    SDL_GetRendererOutputSize(renderer->sdl, &renderer->width, &renderer->height);
    return true;
}

static void sdl_renderer_destroy(Renderer *renderer) {
    if (renderer->sdl) SDL_DestroyRenderer(renderer->sdl);
    if (renderer->window) SDL_DestroyWindow(renderer->window);
    SDL_Quit();
    memset(renderer, 0, sizeof(*renderer));
}

static void sdl_renderer_begin_frame(Renderer *renderer, SDL_Color clear) {
    SDL_SetRenderDrawColor(renderer->sdl, clear.r, clear.g, clear.b, clear.a);
    SDL_RenderClear(renderer->sdl);
}

static void sdl_renderer_end_frame(Renderer *renderer) {
    SDL_RenderPresent(renderer->sdl);
}

static bool sdl_renderer_save_screenshot(Renderer *renderer, const char *path) {
    int width = renderer->width;
    int height = renderer->height;
    SDL_GetRendererOutputSize(renderer->sdl, &width, &height);
    if (width <= 0 || height <= 0) {
        width = renderer->width;
        height = renderer->height;
    }
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height,
                                                          32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        fprintf(stderr, "SDL_CreateRGBSurfaceWithFormat: %s\n", SDL_GetError());
        return false;
    }

    bool ok = false;
    if (SDL_RenderReadPixels(renderer->sdl, NULL, SDL_PIXELFORMAT_ARGB8888,
                             surface->pixels, surface->pitch) != 0) {
        fprintf(stderr, "SDL_RenderReadPixels: %s\n", SDL_GetError());
    } else if (SDL_SaveBMP(surface, path) != 0) {
        fprintf(stderr, "SDL_SaveBMP %s: %s\n", path, SDL_GetError());
    } else {
        ok = true;
    }
    SDL_FreeSurface(surface);
    return ok;
}

const RendererBackend *sdl_renderer_backend(void) {
    static const RendererBackend backend = {
        .id = "sdl",
        .name = "SDL Renderer",
        .create = sdl_renderer_create,
        .destroy = sdl_renderer_destroy,
        .begin_frame = sdl_renderer_begin_frame,
        .end_frame = sdl_renderer_end_frame,
        .save_screenshot = sdl_renderer_save_screenshot,
    };
    return &backend;
}

const RendererBackend *renderer_backend_by_id(const char *id) {
    const RendererBackend *backend = sdl_renderer_backend();
    if (!id || strcmp(id, backend->id) == 0) return backend;
    return NULL;
}

bool renderer_create(Renderer *renderer, const RendererBackend *backend,
                         const char *title, int width, int height,
                         bool hidden, bool software) {
    if (!backend) backend = sdl_renderer_backend();
    renderer->backend = backend;
    if (!backend->create(renderer, title, width, height, hidden, software)) return false;
    renderer->backend = backend;
    return true;
}

void renderer_destroy(Renderer *renderer) {
    if (renderer->backend && renderer->backend->destroy) renderer->backend->destroy(renderer);
}

void renderer_begin_frame(Renderer *renderer, SDL_Color clear) {
    renderer->backend->begin_frame(renderer, clear);
}

void renderer_end_frame(Renderer *renderer) {
    renderer->backend->end_frame(renderer);
}

bool renderer_save_screenshot(Renderer *renderer, const char *path) {
    return renderer->backend->save_screenshot(renderer, path);
}
