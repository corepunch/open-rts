#include "game.h"
#include "engine.h"

/* DC.EXE 0x44ecd0/0x44ee68: quantize each vertical interpolation first,
 * then interpolate horizontally. A single bilinear float is not equivalent. */
int R_FogSample(const int corners[4], ivec2_t pixel) {
    int left = ((31 - pixel.y) * corners[0] + pixel.y * corners[2]) / 31;
    int right = ((31 - pixel.y) * corners[1] + pixel.y * corners[3]) / 31;
    return ((31 - pixel.x) * left + pixel.x * right) / 31;
}

static int corner_brightness(const level_t *map, ivec2_t corner) {
    int sum = 0;
    for (int y = corner.y - 1; y <= corner.y; ++y) {
        for (int x = corner.x - 1; x <= corner.x; ++x) {
            ivec2_t cell = { x, y };
            cell.x = cell.x < 0 ? 0 : cell.x >= map->width ? map->width - 1 : cell.x;
            cell.y = cell.y < 0 ? 0 : cell.y >= map->height ? map->height - 1 : cell.y;
            cell.y = L_ScreenY(map, cell.y);
            sum += P_SightBrightness(map, cell);
        }
    }
    return sum >> 2;
}

void R_DrawFog(app_t *app, const level_t *map) {
    if (!map->sight.cells || !app->renderer || app->cell.w <= 0 || app->cell.h <= 0) return;
    int width = G_WorldViewportWidth(app);
    ivec2_t first = {(int)floorf(-app->cam.x / app->cell.w),
                     (int)floorf(-app->cam.y / app->cell.h)};
    isize2_t tiles = {(width + app->cell.w - 1) / app->cell.w + 1,
                     (app->win.h + app->cell.h - 1) / app->cell.h + 1};
    isize2_t size = {tiles.w * 32, tiles.h * 32};
    isize2_t old = {0};
    if (app->fog_texture) SDL_QueryTexture(app->fog_texture, NULL, NULL, &old.w, &old.h);
    if (!app->fog_texture || old.w != size.w || old.h != size.h) {
        SDL_DestroyTexture(app->fog_texture);
        app->fog_texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, size.w, size.h);
        if (!app->fog_texture) {
            fprintf(stderr, "fog texture: %s\n", SDL_GetError());
            return;
        }
        SDL_SetTextureBlendMode(app->fog_texture, SDL_BLENDMODE_MOD);
        SDL_SetTextureScaleMode(app->fog_texture, SDL_ScaleModeNearest);
    }
    void *pixels;
    int pitch;
    if (SDL_LockTexture(app->fog_texture, NULL, &pixels, &pitch) != 0) return;
    for (int ty = 0; ty < tiles.h; ++ty) {
        for (int tx = 0; tx < tiles.w; ++tx) {
            ivec2_t cell = ivec2_add(first, (ivec2_t){tx, ty});
            int corners[4] = {0};
            if (L_Contains(map, cell.x, cell.y)) {
                corners[0] = corner_brightness(map, cell);
                corners[1] = corner_brightness(map, ivec2_add(cell, (ivec2_t){1, 0}));
                corners[2] = corner_brightness(map, ivec2_add(cell, (ivec2_t){0, 1}));
                corners[3] = corner_brightness(map, ivec2_add(cell, (ivec2_t){1, 1}));
            }
            for (int y = 0; y < 32; ++y) {
                uint32_t *row = (uint32_t *)((uint8_t *)pixels + (ty * 32 + y) * pitch) + tx * 32;
                for (int x = 0; x < 32; ++x) {
                    int light = R_FogSample(corners, (ivec2_t){x, y}) * 255 / 16;
                    row[x] = 0xff000000u | (uint32_t)light * 0x010101u;
                }
            }
        }
    }
    SDL_UnlockTexture(app->fog_texture);
    irect_t dst = {(int)(first.x * app->cell.w + app->cam.x),
                   (int)(first.y * app->cell.h + app->cam.y),
                   tiles.w * app->cell.w, tiles.h * app->cell.h};
    SDL_Rect previous;
    bool clipped = SDL_RenderIsClipEnabled(app->renderer);
    SDL_RenderGetClipRect(app->renderer, &previous);
    SDL_Rect clip = {0, 0, width, app->win.h};
    if (clipped) SDL_IntersectRect(&clip, &previous, &clip);
    SDL_RenderSetClipRect(app->renderer, &clip);
    SDL_RenderCopy(app->renderer, app->fog_texture, NULL, &dst);
    SDL_RenderSetClipRect(app->renderer, clipped ? &previous : NULL);
}
