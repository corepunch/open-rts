#ifndef OPEN_RTS_SPRITES_H
#define OPEN_RTS_SPRITES_H

#include "engine_config.h"

#include <SDL.h>
#include <stdint.h>

typedef struct {
    int value;
    int frames[MAX_TILE_ANIMATION_FRAMES];
    int frame_count;
    uint16_t frame_ms;
} TileAnimation;

typedef struct Tileset {
    SDL_Texture *texture;
    int *tile_lookup;
    int tile_lookup_count;
    TileAnimation *animations;
    int animation_count;
    int count;
    int atlas_cols;
    int tile_w;
    int tile_h;
    int draw_y_offset;
} Tileset;

typedef struct {
    char name[16];
    int facings;
    int length;
    int frame_stride;
    int tick_ms;
    int frame_starts[MAX_SEQUENCE_FACINGS];
    int direction_codes[MAX_SEQUENCE_FACINGS];
} SpriteSequence;

typedef struct SpriteSheet {
    SDL_Texture *texture;
    SDL_Rect *frames;
    SDL_Rect *frame_bounds;
    int frame_count;
    int frame_w;
    int frame_h;
    int rotations;
    int primary_frames_per_rotation;
    SpriteSequence sequences[MAX_SPRITE_SEQUENCES];
    int sequence_count;
} SpriteSheet;

typedef struct {
    char name[32];
    SpriteSheet sprite;
} CachedSprite;

typedef struct {
    CachedSprite entries[MAX_DECORATION_SPRITES];
    int count;
} SpriteCache;

typedef struct {
    SpriteSheet sprite;
    int glyph_index[128];
    uint8_t glyph_width[128];
    int glyph_w;
    int glyph_h;
    int line_h;
    int draw_divisor;
} BitmapFont;

#define RTS_MAX_HUD_MESSAGES 8

typedef struct {
    char text[256];
    int ttl_ms;
} HudMessage;

typedef struct {
    HudMessage messages[RTS_MAX_HUD_MESSAGES];
    int count;
} HudText;

#endif
