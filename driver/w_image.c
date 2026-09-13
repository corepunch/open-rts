#include "engine.h"
#include <strings.h>

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
