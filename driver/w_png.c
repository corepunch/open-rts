#include "engine.h"
#include <png.h>

/* Keep palette indices: OpenKrush deliberately replaces the PNG's palette. */
SDL_Surface *W_LoadPNG(const char *path) {
    png_image image = {.version = PNG_IMAGE_VERSION};
    if (!png_image_begin_read_from_file(&image, path)) return NULL;
    if (!image.width || !image.height || image.width > 8192 || image.height > 8192) {
        png_image_free(&image);
        return NULL;
    }
    bool indexed = (image.format & PNG_FORMAT_FLAG_COLORMAP) != 0;
    image.format = indexed ? PNG_FORMAT_RGBA_COLORMAP : PNG_FORMAT_RGBA;
    uint8_t colors[256 * 4];
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, image.width, image.height,
        indexed ? 8 : 32, indexed ? SDL_PIXELFORMAT_INDEX8 : SDL_PIXELFORMAT_RGBA32);
    if (surface && !png_image_finish_read(&image, NULL, surface->pixels, surface->pitch, colors)) {
        SDL_FreeSurface(surface);
        surface = NULL;
    }
    if (surface && indexed) {
        SDL_Color palette[256] = {0};
        for (unsigned i = 0; i < image.colormap_entries; ++i)
            palette[i] = (SDL_Color){colors[i*4],colors[i*4+1],colors[i*4+2],colors[i*4+3]};
        SDL_SetPaletteColors(surface->format->palette, palette, 0, image.colormap_entries);
    }
    png_image_free(&image);
    return surface;
}

bool W_SetPNGPalette(SDL_Surface *surface, const char *path) {
    SDL_Surface *palette = W_LoadPNG(path);
    if (!palette) return false;
    bool ok = surface->format->palette && palette->format->palette &&
        SDL_SetPaletteColors(surface->format->palette, palette->format->palette->colors,
                              0, palette->format->palette->ncolors) == 0;
    if (ok) SDL_SetColorKey(surface, SDL_TRUE, 0);
    SDL_FreeSurface(palette);
    return ok;
}

bool W_LoadMenuPNG(SDL_Renderer *renderer, const char *root,
                    const char *name, const char *palette, spritesheet_t *out) {
    char path[1024];
    M_PathJoin(path, sizeof(path), root, name);
    SDL_Surface *surface = W_LoadPNG(path);
    if (!surface) return false;
    if ((palette && !W_SetPNGPalette(surface, palette)) || !R_AllocSpriteCells(out, 1)) {
        SDL_FreeSurface(surface);
        return false;
    }
    out->cells[0].rect = out->cells[0].bounds = (irect_t){0,0,surface->w,surface->h};
    out->lumps[0].texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (!out->lumps[0].texture) { R_FreeSprite(out); return false; }
    return true;
}
