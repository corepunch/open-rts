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
    bool rotate;
    int lump[MAX_SPRITE_ROTATIONS];
    uint8_t flip[MAX_SPRITE_ROTATIONS];
} spriteframe_t;

typedef struct spritedef_s {
    int numframes;
    int rotations;
    angle_t first_angle;
    bool clockwise;
    spriteframe_t *spriteframes;
} spritedef_t;

typedef struct spritelump_s {
    irect_t rect;
    irect_t bounds;
    ivec2_t ground_point;
    ivec2_t displacement;
} spritelump_t;

typedef struct spritesheet_s {
    SDL_Texture *textures[9];
    spritelump_t *lumps;
    int numlumps;
    int frame_w;
    int frame_h;
    spritedef_t spritedef;
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
