#define _DEFAULT_SOURCE
#include "p_local.h"

#include <stdlib.h>

const uint8_t *R_PaletteMap(const spritesheet_t *sprite, int id) {
    if (!sprite || id < 0) return NULL;
    for (int i = 0; i < sprite->palette_map_count; ++i)
        if (sprite->palette_maps[i].id == id) return sprite->palette_maps[i].indices;
    return NULL;
}

bool R_AddTileAnim(tileset_t *tileset, int value, const int *frames,
                           int frame_count, uint16_t frame_ms) {
    if (!tileset || !frames || frame_count < 2 || frame_count > MAX_TILE_ANIMATION_FRAMES || frame_ms == 0) {
        return false;
    }
    TileAnimation *animations = realloc(tileset->animations,
                                        (size_t)(tileset->animation_count + 1) * sizeof(TileAnimation));
    if (!animations) return false;
    tileset->animations = animations;

    TileAnimation *anim = &tileset->animations[tileset->animation_count++];
    memset(anim, 0, sizeof(*anim));
    anim->value = value;
    anim->frame_count = frame_count;
    anim->frame_ms = frame_ms;
    for (int i = 0; i < frame_count; ++i) anim->frames[i] = frames[i];
    return true;
}

int HU_TextWidth(const bitmapfont_t *font, const char *text, int scale) {
    if (!font || !text || scale <= 0) return 0;
    int width = 0, line_width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '\r') continue;
        if (*p == '\n') {
            if (line_width > width) width = line_width;
            line_width = 0;
            continue;
        }
        unsigned char ch = *p;
        if (ch >= 128 || font->glyph_index[ch] < 0) ch = '?';
        int advance = font->glyph_width[ch] > 0 ?
            font->glyph_width[ch] : font->glyph_size.w;
        line_width += advance * scale;
    }
    return line_width > width ? line_width : width;
}

void HU_DrawText(ivec2_t at, const bitmapfont_t *font, const char *text,
                 const uint8_t *remap, int scale) {
    V_DrawTextScaled(at, font, text, remap, scale);
}

void HU_DrawTextWrapped(irect_t box, const bitmapfont_t *font, const char *text,
                        const uint8_t *remap, int scale) {
    (void)scale;
    V_DrawTextWrapped(box, font, text, remap, 0);
}
