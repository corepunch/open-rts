#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window *video_window;
static SDL_Renderer *video_renderer;
static SDL_Texture *video_texture;
static bool video_linear;

void I_SetScaleMode(bool linear) {
    video_linear = linear;
    if (video_texture)
        SDL_SetTextureScaleMode(video_texture, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
}

static bool resize_screen(isize2_t size) {
    if (size.w <= 0 || size.h <= 0) return false;
    isize2_t current = {0};
    if (video_texture && !SDL_QueryTexture(video_texture, NULL, NULL, &current.w, &current.h) &&
        current.w == size.w && current.h == size.h) return true;
    SDL_Texture *texture = SDL_CreateTexture(video_renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, size.w, size.h);
    if (!texture) {
        fprintf(stderr, "screen texture: %s\n", SDL_GetError());
        return false;
    }
    V_AllocScreen(size.w, size.h);
    if (!screens[0].pixels || screens[0].w != size.w || screens[0].h != size.h) {
        SDL_DestroyTexture(texture);
        return false;
    }
    if (video_texture) SDL_DestroyTexture(video_texture);
    video_texture = texture;
    I_SetScaleMode(video_linear);
    return true;
}

bool I_InitGraphics(app_t *app, int window_width, int window_height, bool hidden, bool software) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : 0);
    video_window = SDL_CreateWindow("open-rts", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    window_width, window_height, window_flags);
    Uint32 renderer_flags = software ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;
    video_renderer = video_window ? SDL_CreateRenderer(video_window, -1, renderer_flags) : NULL;
    if (!video_window || !video_renderer) {
        fprintf(stderr, "SDL window/renderer: %s\n", SDL_GetError());
        if (video_renderer) SDL_DestroyRenderer(video_renderer);
        if (video_window) SDL_DestroyWindow(video_window);
        video_renderer = NULL;
        video_window = NULL;
        SDL_Quit();
        return false;
    }
    SDL_SetRenderDrawBlendMode(video_renderer, SDL_BLENDMODE_NONE);
    isize2_t size = app && app->win.w > 0 ? app->win : (isize2_t){SCREENWIDTH, SCREENHEIGHT};
#ifdef RTS_NATIVE_WORLD
    size = (isize2_t){window_width, window_height};
#endif
    if (!resize_screen(size)) {
        I_ShutdownGraphics();
        return false;
    }
    if (app) {
        app->win = size;
        app->window = video_window;
        app->renderer = video_renderer;
    }
    return true;
}

void I_ShutdownGraphics(void) {
    I_ShutdownSound();
    R_FreeSpriteBuffer();
    if (video_texture) SDL_DestroyTexture(video_texture);
    if (video_renderer) SDL_DestroyRenderer(video_renderer);
    if (video_window) SDL_DestroyWindow(video_window);
    video_texture = NULL;
    video_renderer = NULL;
    video_window = NULL;
    V_FreeScreen();
    SDL_Quit();
}

void I_FinishUpdate(void) {
    if (!video_renderer || !video_texture || !screens[0].pixels) return;
    if (!resize_screen((isize2_t){screens[0].w, screens[0].h})) return;
    void *pixels = NULL;
    int pitch = 0;
    if (SDL_LockTexture(video_texture, NULL, &pixels, &pitch) != 0) {
        fprintf(stderr, "SDL_LockTexture: %s\n", SDL_GetError());
        return;
    }
    for (int y = 0; y < screens[0].h; ++y) {
        uint32_t *row = (uint32_t *)((uint8_t *)pixels + (size_t)y * (size_t)pitch);
        const uint8_t *src = screens[0].pixels + (size_t)y * (size_t)screens[0].w;
        for (int x = 0; x < screens[0].w; ++x)
            row[x] = vpalette[src[x]] | 0xff000000u;
    }
    SDL_UnlockTexture(video_texture);
    SDL_RenderClear(video_renderer);
    SDL_RenderCopy(video_renderer, video_texture, NULL, NULL);
    SDL_RenderPresent(video_renderer);
}

bool I_SaveScreenshot(const char *path) {
    if (!path || !screens[0].pixels || screens[0].w <= 0 || screens[0].h <= 0) return false;
    int width = screens[0].w;
    int height = screens[0].h;
    int row_stride = (width + 3) & ~3;
    uint32_t pixel_bytes = (uint32_t)row_stride * (uint32_t)height;
    uint32_t palette_bytes = 256u * 4u;
    uint32_t header_bytes = 14u + 40u;
    uint32_t file_size = header_bytes + palette_bytes + pixel_bytes;
    uint8_t *file = calloc(file_size, 1);
    if (!file) return false;
    file[0] = 'B';
    file[1] = 'M';
    file[2] = (uint8_t)file_size;
    file[3] = (uint8_t)(file_size >> 8);
    file[4] = (uint8_t)(file_size >> 16);
    file[5] = (uint8_t)(file_size >> 24);
    uint32_t offset = header_bytes + palette_bytes;
    file[10] = (uint8_t)offset;
    file[11] = (uint8_t)(offset >> 8);
    file[12] = (uint8_t)(offset >> 16);
    file[13] = (uint8_t)(offset >> 24);
    file[14] = 40;
    file[18] = (uint8_t)width;
    file[19] = (uint8_t)(width >> 8);
    file[20] = (uint8_t)(width >> 16);
    file[21] = (uint8_t)(width >> 24);
    file[22] = (uint8_t)height;
    file[23] = (uint8_t)(height >> 8);
    file[24] = (uint8_t)(height >> 16);
    file[25] = (uint8_t)(height >> 24);
    file[26] = 1;
    file[28] = 8;
    for (int i = 0; i < 256; ++i) {
        uint8_t *entry = file + header_bytes + (size_t)i * 4u;
        entry[0] = (uint8_t)(vpalette[i]);
        entry[1] = (uint8_t)(vpalette[i] >> 8);
        entry[2] = (uint8_t)(vpalette[i] >> 16);
        entry[3] = 0;
    }
    uint8_t *pixels = file + offset;
    for (int y = 0; y < height; ++y) {
        const uint8_t *src = screens[0].pixels + (size_t)(height - 1 - y) * (size_t)width;
        memcpy(pixels + (size_t)y * (size_t)row_stride, src, (size_t)width);
    }
    FILE *out = fopen(path, "wb");
    bool ok = out && fwrite(file, 1, file_size, out) == file_size;
    if (out) fclose(out);
    if (!ok) fprintf(stderr, "I_SaveScreenshot %s failed\n", path);
    free(file);
    return ok;
}

static bool sdl_renderer_create(renderer_t *renderer, const char *title, int width, int height,
                                bool hidden, bool software) {
    memset(renderer, 0, sizeof(*renderer));
    app_t scratch = {.win = {SCREENWIDTH, SCREENHEIGHT}};
    if (!I_InitGraphics(&scratch, width, height, hidden, software)) return false;
    if (title) SDL_SetWindowTitle(video_window, title);
    renderer->window = video_window;
    renderer->sdl = video_renderer;
    renderer->width = screens[0].w;
    renderer->height = screens[0].h;
    return true;
}

static void sdl_renderer_destroy(renderer_t *renderer) {
    I_ShutdownGraphics();
    memset(renderer, 0, sizeof(*renderer));
}

static void sdl_renderer_begin_frame(renderer_t *renderer, SDL_Color clear) {
    (void)renderer;
    V_BeginFrame(0xff000000u | ((uint32_t)clear.r << 16) | ((uint32_t)clear.g << 8) | clear.b);
}

static void sdl_renderer_end_frame(renderer_t *renderer) {
    (void)renderer;
    I_FinishUpdate();
}

static bool sdl_renderer_save_screenshot(renderer_t *renderer, const char *path) {
    (void)renderer;
    return I_SaveScreenshot(path);
}

const rendererbackend_t *sdl_renderer_backend(void) {
    static const rendererbackend_t backend = {
        .id = "sdl",
        .name = "indexed framebuffer",
        .create = sdl_renderer_create,
        .destroy = sdl_renderer_destroy,
        .begin_frame = sdl_renderer_begin_frame,
        .end_frame = sdl_renderer_end_frame,
        .save_screenshot = sdl_renderer_save_screenshot,
    };
    return &backend;
}

const rendererbackend_t *renderer_backend_by_id(const char *id) {
    const rendererbackend_t *backend = sdl_renderer_backend();
    if (!id || strcmp(id, backend->id) == 0) return backend;
    return NULL;
}

bool renderer_create(renderer_t *renderer, const rendererbackend_t *backend,
                     const char *title, int width, int height,
                     bool hidden, bool software) {
    if (!backend) backend = sdl_renderer_backend();
    renderer->backend = backend;
    if (!backend->create(renderer, title, width, height, hidden, software)) return false;
    renderer->backend = backend;
    r_renderer = renderer->sdl;
    return true;
}

void renderer_destroy(renderer_t *renderer) {
    if (r_renderer == renderer->sdl) r_renderer = NULL;
    if (renderer->backend && renderer->backend->destroy) renderer->backend->destroy(renderer);
}

void renderer_begin_frame(renderer_t *renderer, SDL_Color clear) {
    renderer->backend->begin_frame(renderer, clear);
}

void renderer_end_frame(renderer_t *renderer) {
    renderer->backend->end_frame(renderer);
}

bool renderer_save_screenshot(renderer_t *renderer, const char *path) {
    return renderer->backend->save_screenshot(renderer, path);
}
