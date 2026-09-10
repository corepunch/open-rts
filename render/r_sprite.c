#include "engine.h"

#include <string.h>

/* One upload surface serves indexed sprites and terrain, never an image/team.
 * Locking flushes queued uses before its pixels are overwritten. */
static struct {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    isize2_t size;
} sprite_buffer;

void R_FreeSpriteBuffer(void) {
    if (sprite_buffer.texture) SDL_DestroyTexture(sprite_buffer.texture);
    memset(&sprite_buffer, 0, sizeof(sprite_buffer));
}

static SDL_Texture *indexed_upload(SDL_Renderer *renderer, const uint8_t *indices,
                                    int stride, const uint32_t colors[256], irect_t rect) {
    if (sprite_buffer.renderer != renderer) R_FreeSpriteBuffer();
    if (rect.w > sprite_buffer.size.w || rect.h > sprite_buffer.size.h) {
        isize2_t size = { SDL_max(rect.w, sprite_buffer.size.w),
                         SDL_max(rect.h, sprite_buffer.size.h) };
        SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, size.w, size.h);
        if (!texture) return NULL;
        R_FreeSpriteBuffer();
        sprite_buffer.renderer = renderer;
        sprite_buffer.texture = texture;
        sprite_buffer.size = size;
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    }
    void *pixels;
    int pitch;
    irect_t area = { 0, 0, rect.w, rect.h };
    if (SDL_LockTexture(sprite_buffer.texture, &area, &pixels, &pitch) != 0)
        return NULL;
    for (int y = 0; y < rect.h; ++y) {
        uint32_t *row = (uint32_t *)((uint8_t *)pixels + (size_t)y * pitch);
        const uint8_t *source = indices +
            (size_t)(rect.y + y) * stride + rect.x;
        V_IndexedToRGBA(row, source, rect.w, colors);
    }
    SDL_UnlockTexture(sprite_buffer.texture);
    return sprite_buffer.texture;
}

static bool draw_texture(SDL_Renderer *renderer, SDL_Texture *texture,
                          const irect_t *src, const irect_t *dst,
                          SDL_RendererFlip flip, SDL_Color color, SDL_BlendMode blend) {
    if (!texture) return false;
    SDL_BlendMode previous;
    SDL_GetTextureBlendMode(texture, &previous);
    SDL_SetTextureBlendMode(texture, blend);
    SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texture, color.a);
    bool ok = SDL_RenderCopyEx(renderer, texture, src, dst, 0, NULL, flip) == 0;
    SDL_SetTextureBlendMode(texture, previous);
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
    return ok;
}

static bool valid_source(irect_t rect, isize2_t size) {
    return rect.x >= 0 && rect.y >= 0 && rect.w > 0 && rect.h > 0 &&
           rect.w <= size.w && rect.h <= size.h &&
           rect.x <= size.w - rect.w && rect.y <= size.h - rect.h;
}

bool R_DrawIndexed(SDL_Renderer *renderer, const uint8_t *indices, isize2_t size,
                   const uint32_t palette[256], const irect_t *src, const irect_t *dst,
                   SDL_RendererFlip flip, SDL_Color color, SDL_BlendMode blend) {
    irect_t rect = src ? *src : (irect_t){0, 0, size.w, size.h};
    if (!renderer || !indices || !palette || !valid_source(rect, size)) return false;
    SDL_Texture *texture = indexed_upload(renderer, indices, size.w, palette, rect);
    rect.x = rect.y = 0;
    return draw_texture(renderer, texture, &rect, dst, flip, color, blend);
}

bool R_DrawSprite(SDL_Renderer *renderer, const spritesheet_t *sprite, int frame,
                  int palette, const irect_t *src, const irect_t *dst,
                  SDL_RendererFlip flip, SDL_Color color, SDL_BlendMode blend) {
    if (!renderer || !sprite || !sprite->cells || !sprite->lumps ||
        frame < 0 || frame >= sprite->numlumps) return false;
    irect_t cell = sprite->cells[frame].rect;
    irect_t rect = src ? *src : cell;
    isize2_t size = {cell.w, cell.h};
    if (sprite->lumps[frame].indices) {
        const uint8_t *map = NULL;
        for (int i = 0; i < sprite->palette_map_count; ++i)
            if (sprite->palette_maps[i].id == palette)
                map = sprite->palette_maps[i].indices;
        uint32_t colors[256];
        for (int i = 0; i < 256; ++i)
            colors[i] = sprite->source_palette[map ? map[i] : i];
        return R_DrawIndexed(renderer, sprite->lumps[frame].indices, size, colors,
                             &rect, dst, flip, color, blend);
    }
    if (!valid_source(rect, size)) return false;
    return draw_texture(renderer, sprite->lumps[frame].texture, &rect, dst, flip, color, blend);
}
