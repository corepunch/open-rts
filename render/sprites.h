#ifndef __SPRITES__
#define __SPRITES__

#include "engine_config.h"
#include "facing.h"
#include "m_vec.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct app_s app_t;

typedef struct {
    int id;
    SDL_Texture *texture;
} spritetranslation_t;

typedef struct {
    int value;
    int frames[MAX_TILE_ANIMATION_FRAMES];
    int frame_count;
    uint16_t frame_ms;
} TileAnimation;

typedef struct spritelayer_s {
    ivec2_t offset;
    uint16_t lump;
    uint8_t remap; /* Native FIN drawing mode, not a team/palette ID. */
    uint8_t intensity;
    uint8_t layer;
    uint8_t flags;
    char sprite_name[9];
} spritelayer_t;

typedef struct spritedirection_s {
    int ticks;
    spritelayer_t *layers;
} spritedirection_t;

typedef struct spriteframe_s {
    int rotations; /* 1 is nondirectional; slots start north, counterclockwise. */
    char frame_name[17];
    spritedirection_t directions[MAX_SPRITE_ROTATIONS];
} spriteframe_t;

typedef struct spritedef_s {
    int numframes;
    spriteframe_t *spriteframes;
} spritedef_t;

typedef struct spritelump_s {
    SDL_Texture *texture;
    spritetranslation_t *translations;
    int translation_count;
    uint8_t *indices;
} spritelump_t;

/* Sprite geometry, indexed by the layer cell number; no renderer resources. */
typedef struct spritecell_s {
    irect_t rect;
    irect_t bounds;
    ivec2_t ground_point;
    ivec2_t displacement;
} spritecell_t;

typedef struct spritesheet_s {
    spritecell_t *cells;
    spritelump_t *lumps;
    int numlumps;
    isize2_t frame_size;
    spritedef_t spritedef;
    bool indexed;
    uint32_t palette[256];
    int indexed_blend_selector;
    const uint8_t *indexed_blend_table;
    const uint8_t *shadowmap; /* Destination colormap for native projected shadows. */
} spritesheet_t;

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

typedef struct cachedsprite_s {
    char name[32];
    spritesheet_t sprite;
} cachedsprite_t;

typedef struct spritecache_s {
    cachedsprite_t entries[MAX_DECORATION_SPRITES];
    int count;
    const spritesheet_t **sprites; /* Borrowed sheets, indexed by state sprite ID. */
    int numsprites;
    struct spritecache_s *ui; /* Owned images keyed by path, outside the state registry. */
} spritecache_t;

typedef struct bitmapfont_s {
    spritesheet_t sprite;
    int glyph_index[128];
    uint8_t glyph_width[128];
    isize2_t glyph_size;
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
