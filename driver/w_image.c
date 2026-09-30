#include "engine.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int indexed_match(const uint32_t *palette, int count, uint32_t color) {
    color |= 0xff000000u;
    for (int i = 0; i < count; ++i)
        if ((palette[i] | 0xff000000u) == color) return i;
    return -1;
}

static uint8_t indexed_nearest(const uint32_t *palette, int count, uint32_t color) {
    int best = 0, best_distance = 1 << 30;
    int r = (int)((color >> 16) & 255);
    int g = (int)((color >> 8) & 255);
    int b = (int)(color & 255);
    for (int i = 0; i < count; ++i) {
        int dr = r - (int)((palette[i] >> 16) & 255);
        int dg = g - (int)((palette[i] >> 8) & 255);
        int db = b - (int)(palette[i] & 255);
        int distance = dr * dr + dg * dg + db * db;
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
            if (distance == 0) break;
        }
    }
    return (uint8_t)best;
}

/* Engine image decoding. PCX rows include their authored padding bytes. */
SDL_Surface *W_LoadImage(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".bmp")) return SDL_LoadBMP(path);
    if (!ext || strcasecmp(ext, ".pcx")) return NULL;
    blob_t file;
    if (!W_ReadFile(path, &file)) return NULL;
    SDL_Surface *surface = NULL;
    const uint8_t *p = file.bytes;
    if (file.size < 128 + 769 || p[0] != 10 || p[2] != 1 || p[3] != 8 ||
        p[65] != 1 || p[file.size - 769] != 12) goto done;
    int width = read_u16_le(p + 8) - read_u16_le(p + 4) + 1;
    int height = read_u16_le(p + 10) - read_u16_le(p + 6) + 1;
    int stride = read_u16_le(p + 66);
    if (width <= 0 || height <= 0 || stride < width) goto done;
    surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 8, SDL_PIXELFORMAT_INDEX8);
    if (!surface) goto done;
    SDL_Color colors[256];
    const uint8_t *palette = p + file.size - 768;
    for (int i = 0; i < 256; ++i)
        colors[i] = (SDL_Color){palette[i*3], palette[i*3+1], palette[i*3+2], 255};
    SDL_SetPaletteColors(surface->format->palette, colors, 0, 256);
    const uint8_t *end = p + file.size - 769;
    p += 128;
    for (int y = 0; y < height; ++y) {
        uint8_t *row = (uint8_t *)surface->pixels + y * surface->pitch;
        for (int x = 0; x < stride;) {
            if (p == end) goto invalid;
            int value = *p++, count = 1;
            if ((value & 0xc0) == 0xc0) {
                count = value & 0x3f;
                if (!count || p == end) goto invalid;
                value = *p++;
            }
            if (count > stride - x) goto invalid;
            for (int i = 0; i < count; ++i, ++x)
                if (x < width) row[x] = value;
        }
    }
    goto done;
invalid:
    SDL_FreeSurface(surface);
    surface = NULL;
done:
    W_FreeFile(&file);
    return surface;
}

bool W_LoadIndexedSheet(const char *path, spritesheet_t *out) {
    if (!path || !out) return false;
    memset(out, 0, sizeof(*out));
    SDL_Surface *surface = W_LoadImage(path);
    if (!surface || surface->w <= 0 || surface->h <= 0) {
        SDL_FreeSurface(surface);
        return false;
    }
    int w = surface->w, h = surface->h;
    uint8_t *indices = calloc((size_t)w * (size_t)h, 1);
    if (!indices) {
        SDL_FreeSurface(surface);
        return false;
    }
    if (surface->format->format == SDL_PIXELFORMAT_INDEX8 && surface->format->palette) {
        SDL_Palette *pal = surface->format->palette;
        int n = pal->ncolors < 256 ? pal->ncolors : 256;
        for (int i = 0; i < n; ++i) {
            SDL_Color c = pal->colors[i];
            out->palette[i] = 0xff000000u | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
            out->source_palette[i] = out->palette[i];
        }
        for (int y = 0; y < h; ++y) {
            const uint8_t *row = (const uint8_t *)surface->pixels + (size_t)y * (size_t)surface->pitch;
            memcpy(indices + (size_t)y * (size_t)w, row, (size_t)w);
        }
    } else {
        SDL_Surface *rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
        if (!rgba) {
            free(indices);
            SDL_FreeSurface(surface);
            return false;
        }
        int count = 0;
        for (int y = 0; y < h; ++y) {
            const uint32_t *row = (const uint32_t *)((uint8_t *)rgba->pixels + (size_t)y * (size_t)rgba->pitch);
            for (int x = 0; x < w; ++x) {
                uint32_t color = row[x] | 0xff000000u;
                int index = indexed_match(out->palette, count, color);
                if (index < 0) {
                    if (count < 256) {
                        out->palette[count] = color;
                        index = count++;
                    } else {
                        index = indexed_nearest(out->palette, count, color);
                    }
                }
                indices[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)index;
            }
        }
        memcpy(out->source_palette, out->palette, sizeof(out->source_palette));
        SDL_FreeSurface(rgba);
    }
    if (!R_AllocSpriteCells(out, 1)) {
        free(indices);
        SDL_FreeSurface(surface);
        return false;
    }
    out->lumps[0].indices = indices;
    out->cells[0].rect = (irect_t){0, 0, w, h};
    out->cells[0].bounds = out->cells[0].rect;
    out->frame_size = (isize2_t){w, h};
    out->indexed = true;
    SDL_FreeSurface(surface);
    return true;
}
