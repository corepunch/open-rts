#define _DEFAULT_SOURCE
#include "p_local.h"

SDL_Texture *rgba_texture(SDL_Renderer *renderer, const uint32_t *pixels, int w, int h, bool blend) {
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture %dx%d ARGB8888: %s\n", w, h, SDL_GetError());
        return NULL;
    }
    if (SDL_UpdateTexture(texture, NULL, pixels, w * (int)sizeof(uint32_t)) != 0) {
        fprintf(stderr, "SDL_UpdateTexture %dx%d ARGB8888: %s\n", w, h, SDL_GetError());
        SDL_DestroyTexture(texture);
        return NULL;
    }
    if (SDL_SetTextureBlendMode(texture, blend ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE) != 0) {
        fprintf(stderr, "SDL_SetTextureBlendMode: %s\n", SDL_GetError());
    }
    if (SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest) != 0) {
        fprintf(stderr, "SDL_SetTextureScaleMode: %s\n", SDL_GetError());
    }
    return texture;
}

bool tileset_add_animation(Tileset *tileset, int value, const int *frames,
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

int font_text_width(const BitmapFont *font, const char *text, int scale) {
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
        int advance = font->glyph_width[ch] > 0 ? font->glyph_width[ch] : font->glyph_w;
        line_width += advance * scale;
    }
    return line_width > width ? line_width : width;
}

void font_draw_text(SDL_Renderer *renderer, const BitmapFont *font, int x, int y,
                        const char *text, SDL_Color color, int scale) {
    if (!renderer || !font || !font->sprite.texture || !text || scale <= 0) return;
    SDL_SetTextureColorMod(font->sprite.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(font->sprite.texture, color.a);
    int cx = x, cy = y;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '\r') continue;
        if (*p == '\n') {
            cx = x;
            cy += (font->line_h > 0 ? font->line_h : font->glyph_h) * scale;
            continue;
        }
        unsigned char ch = *p;
        if (ch >= 128 || font->glyph_index[ch] < 0) ch = '?';
        int frame = font->glyph_index[ch];
        int advance = font->glyph_width[ch] > 0 ? font->glyph_width[ch] : font->glyph_w;
        if (frame >= 0 && frame < font->sprite.frame_count) {
            SDL_Rect src = font->sprite.frames[frame];
            if (font->sprite.frame_bounds && font->sprite.frame_bounds[frame].w > 0 &&
                font->sprite.frame_bounds[frame].h > 0) {
                SDL_Rect bounds = font->sprite.frame_bounds[frame];
                src.x += bounds.x;
                src.y += bounds.y;
                src.w = bounds.w;
                src.h = bounds.h;
            }
            if (src.w > 0 && src.h > 0) {
                int divisor = font->draw_divisor > 0 ? font->draw_divisor : 1;
                SDL_Rect dst = {
                    cx,
                    cy,
                    (src.w * scale + divisor - 1) / divisor,
                    (src.h * scale + divisor - 1) / divisor,
                };
                SDL_RenderCopy(renderer, font->sprite.texture, &src, &dst);
            }
        }
        cx += advance * scale;
    }
    SDL_SetTextureColorMod(font->sprite.texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(font->sprite.texture, 255);
}

void font_draw_text_wrapped(SDL_Renderer *renderer, const BitmapFont *font, int x, int y,
                                int max_w, const char *text, SDL_Color color, int scale) {
    if (!renderer || !font || !text || max_w <= 0 || scale <= 0) return;
    char line[256] = { 0 };
    int line_len = 0;
    int cy = y;
    const char *word = text;
    while (*word) {
        while (*word == ' ' || *word == '\r' || *word == '\n') {
            if (*word == '\n' && line_len > 0) {
                font_draw_text(renderer, font, x, cy, line, color, scale);
                cy += (font->line_h > 0 ? font->line_h : font->glyph_h) * scale;
                line[0] = '\0';
                line_len = 0;
            }
            word++;
        }
        if (!*word) break;
        const char *end = word;
        while (*end && *end != ' ' && *end != '\r' && *end != '\n') end++;
        size_t word_len = (size_t)(end - word);
        if (word_len >= sizeof(line)) word_len = sizeof(line) - 1;
        char candidate[256];
        if (line_len > 0)
            snprintf(candidate, sizeof(candidate), "%s %.*s", line, (int)word_len, word);
        else
            snprintf(candidate, sizeof(candidate), "%.*s", (int)word_len, word);
        if (line_len > 0 && font_text_width(font, candidate, scale) > max_w) {
            font_draw_text(renderer, font, x, cy, line, color, scale);
            cy += (font->line_h > 0 ? font->line_h : font->glyph_h) * scale;
            snprintf(line, sizeof(line), "%.*s", (int)word_len, word);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }
        line_len = (int)strlen(line);
        word = end;
    }
    if (line_len > 0) font_draw_text(renderer, font, x, cy, line, color, scale);
}
