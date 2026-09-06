#ifndef __SPRITES__
#define __SPRITES__

#include "engine_config.h"
#include "facing.h"
#include "m_vec.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct app_s app_t;
struct spritesheet_s;

typedef enum {
    RTS_COMPOSE_NONE,
    RTS_COMPOSE_INDEXED_TABLE,
} rts_composition_kind_t;

typedef struct {
    rts_composition_kind_t kind;
    const uint8_t *source_indices;
    int source_stride;
    const uint32_t *palette;
    const uint8_t *lookup_table;
} rts_composition_t;

typedef struct {
    int value;
    int frames[MAX_TILE_ANIMATION_FRAMES];
    int frame_count;
    uint16_t frame_ms;
} TileAnimation;

typedef struct tileset_s {
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
} tileset_t;

typedef struct spriteframe_s {
    int image_index[MAX_SEQUENCE_FACINGS];
} spriteframe_t;

typedef struct spritesequence_s {
    char name[16];
    int facings;
    int length;
    int frame_stride;
    int tick_ms;
    int frame_starts[MAX_SEQUENCE_FACINGS];
    angle_t rotation_angles[MAX_SEQUENCE_FACINGS];
    spriteframe_t *frames;
} spritesequence_t;

typedef struct spritesheet_s {
    SDL_Texture *texture;
    SDL_Texture *remap_textures[8];
    irect_t *frames;
    irect_t *frame_bounds;
    SDL_Point *frame_ground_points;
    SDL_Point *frame_displacements;
    int frame_count;
    int frame_w;
    int frame_h;
    int rotations;
    int primary_frames_per_rotation;
    spritesequence_t sequences[MAX_SPRITE_SEQUENCES];
    int sequence_count;
    void *native_data;
    void (*destroy_native_data)(void *);
    bool (*resolve_composition)(const struct spritesheet_s *sprite, int selector,
                                rts_composition_t *out);
} spritesheet_t;

typedef struct cachedsprite_s {
    char name[32];
    spritesheet_t sprite;
} cachedsprite_t;

typedef struct spritecache_s {
    cachedsprite_t entries[MAX_DECORATION_SPRITES];
    int count;
} spritecache_t;

typedef struct bitmapfont_s {
    spritesheet_t sprite;
    int glyph_index[128];
    uint8_t glyph_width[128];
    int glyph_w;
    int glyph_h;
    int line_h;
    int draw_divisor;
} bitmapfont_t;

#define RTS_MAX_HUD_MESSAGES 8

typedef struct {
    char text[256];
    int ttl_ms;
} HudMessage;

typedef struct hudtext_s {
    HudMessage messages[RTS_MAX_HUD_MESSAGES];
    int count;
} hudtext_t;

#endif
