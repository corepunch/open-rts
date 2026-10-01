#include "game.h"
#include "v_video.h"

#include <stdlib.h>
#include <string.h>

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

/* Destination darkening equivalent to SDL_BLENDMODE_MOD with gray light/255.
 * R_FogSample is 0..16; light is sample * 255 / 16, so 16 keeps the pixel,
 * 8 is 127/255, and 0 is black. */
static uint8_t fogmap[17][256];
static uint32_t fogmap_palette[256];
static bool fogmap_ready;

static void ensure_fogmap(void) {
    if (fogmap_ready && memcmp(fogmap_palette, vpalette, sizeof(vpalette)) == 0) return;
    memcpy(fogmap_palette, vpalette, sizeof(vpalette));
    for (int sample = 0; sample <= 16; ++sample) {
        int light = sample * 255 / 16;
        for (int i = 0; i < 256; ++i) {
            if (sample == 16) { fogmap[sample][i] = (uint8_t)i; continue; }
            int r = ((int)((vpalette[i] >> 16) & 255) * light) / 255;
            int g = ((int)((vpalette[i] >> 8) & 255) * light) / 255;
            int b = ((int)(vpalette[i] & 255) * light) / 255;
            fogmap[sample][i] = V_NearestIndex(0xff000000u | ((uint32_t)r << 16) |
                                               ((uint32_t)g << 8) | (uint32_t)b);
        }
    }
    fogmap_ready = true;
}

void R_DrawFog(app_t *app, const level_t *map) {
    if (!map->sight.cells || !screens[0].pixels || app->cell.w <= 0 || app->cell.h <= 0) return;
    ensure_fogmap();
    int width = G_WorldViewportWidth(app);
    if (width > screens[0].w) width = screens[0].w;
    int height = app->win.h < screens[0].h ? app->win.h : screens[0].h;
    ivec2_t first = {(int)floorf(-app->cam.x / app->cell.w),
                     (int)floorf(-app->cam.y / app->cell.h)};
    isize2_t tiles = {(width + app->cell.w - 1) / app->cell.w + 1,
                     (height + app->cell.h - 1) / app->cell.h + 1};
    irect_t dst = {(int)(first.x * app->cell.w + app->cam.x),
                   (int)(first.y * app->cell.h + app->cam.y),
                   tiles.w * app->cell.w, tiles.h * app->cell.h};
    int x0 = dst.x > 0 ? dst.x : 0;
    int y0 = dst.y > 0 ? dst.y : 0;
    int x1 = dst.x + dst.w < width ? dst.x + dst.w : width;
    int y1 = dst.y + dst.h < height ? dst.y + dst.h : height;
    int src_w = tiles.w * 32;
    int src_h = tiles.h * 32;
    if (dst.w <= 0 || dst.h <= 0 || src_w <= 0 || src_h <= 0 || x0 >= x1 || y0 >= y1) return;
    uint8_t *lights = malloc((size_t)src_w * (size_t)src_h);
    if (!lights) return;
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
            for (int ly = 0; ly < 32; ++ly) {
                uint8_t *sample = lights + ((size_t)(ty * 32 + ly) * (size_t)src_w + (size_t)tx * 32);
                for (int lx = 0; lx < 32; ++lx) {
                    int light = R_FogSample(corners, (ivec2_t){lx, ly});
                    sample[lx] = (uint8_t)(light < 0 ? 0 : light > 16 ? 16 : light);
                }
            }
        }
    }
    for (int y = y0; y < y1; ++y) {
        int sy = (y - dst.y) * src_h / dst.h;
        if (sy < 0) sy = 0;
        if (sy >= src_h) sy = src_h - 1;
        const uint8_t *sample = lights + (size_t)sy * (size_t)src_w;
        uint8_t *row = screens[0].pixels + (size_t)y * (size_t)screens[0].w;
        for (int x = x0; x < x1; ++x) {
            int sx = (x - dst.x) * src_w / dst.w;
            if (sx < 0) sx = 0;
            if (sx >= src_w) sx = src_w - 1;
            row[x] = fogmap[sample[sx]][row[x]];
        }
    }
    free(lights);
}
