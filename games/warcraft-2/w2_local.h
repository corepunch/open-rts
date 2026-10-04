#ifndef __W2_LOCAL__
#define __W2_LOCAL__

#include "engine.h"

#include <stddef.h>
#include <stdint.h>

#define W2_TYPE_COUNT 105
#define W2_SPRITE_COUNT (W2_TYPE_COUNT + 4)
#define W2_WORK_STATE(pud) (1 + W2_TYPE_COUNT * 2 + ((pud) - 2) * 8)
#define W2_WAIT_STATE(pud) (W2_WORK_STATE(pud) + 7)
#define W2_CARRY_STATE(variant) (1 + W2_TYPE_COUNT * 2 + 16 + (variant) * 2)
#define W2_STATE_COUNT (1 + W2_TYPE_COUNT * 2 + 16 + 8)
#define W2_MOBJ_COUNT (W2_TYPE_COUNT + 1)
#define W2_TILE_LOOKUP 0x9E0
/* Stratagus Speed is not cells per second. This divisor is an engine
 * presentation choice so a footman (Speed 10) walks at 1.25 cells/s. */
#define W2_SPEED_DIVISOR 8.0f
#define W2_WALK_TICS 4
#define W2_HARVEST_GOLD 100
#define W2_ENTRY_LIMIT (16u * 1024u * 1024u)

enum {
    W2_SKIP = 1 << 0,
    W2_MOBILE = 1 << 1,
    W2_SEA = 1 << 2,
    W2_AIR = 1 << 3,
    W2_STRUCTURE = 1 << 4,
    W2_HALL = 1 << 5,
    W2_HARVEST = 1 << 6,
    W2_COMBAT = 1 << 7,
    W2_CRITTER = 1 << 8,
};

/* Index is the PUD type byte. grp[] is forest, winter, wasteland, swamp.
 * A later-era 0 reuses the forest entry. Entries past this MAINDAT fall back. */
typedef struct {
    const char *name;
    uint16_t flags;
    uint8_t tw, th;
    uint16_t hp;
    uint8_t speed;
    uint8_t damage;
    uint8_t range;
    uint8_t sight;
    uint16_t grp[4];
    uint8_t armor, damage_min;
    const char *label;
} w2_unit_t;

typedef struct {
    uint8_t *data;
    size_t size;
} w2_blob_t;

typedef struct {
    uint8_t *file;
    size_t file_size;
    int count;
    uint32_t *offsets;
} w2_archive_t;

typedef struct {
    uint16_t x, y;
    uint8_t type, player;
    uint16_t data;
} w2_pud_unit_t;

typedef struct {
    int era;
    int ver;
    uint8_t owners[16];
    uint8_t sides[16];
    w2_pud_unit_t *units;
    int unit_count;
    int view_player;
} w2_pud_t;

extern const w2_unit_t w2_units[W2_TYPE_COUNT];

void w2_build_info(void);
void w2_limit_walk(int pud, int phases);
int w2_pud_named(const char *name);

bool w2_archive_open(w2_archive_t *arc, const char *path);
void w2_archive_close(w2_archive_t *arc);
bool w2_archive_extract(const w2_archive_t *arc, int index, w2_blob_t *out);
void w2_blob_free(w2_blob_t *blob);

bool w2_decode_palette(const w2_blob_t *entry, uint32_t palette[256]);
bool w2_decode_tileset(const w2_archive_t *arc, int era, tileset_t *out);
bool w2_decode_grp(const w2_blob_t *entry, const uint32_t palette[256],
                   spritesheet_t *out, bool directional, int *phases);
int w2_grp_entry(const w2_unit_t *unit, int era, int archive_count);
int w2_install_team_colors(spritesheet_t *sprite, const uint32_t palette[256]);

bool w2_load_pud(const char *path, level_t *out);
int w2_spawn_units(void);
bool w2_init_resources(level_t *map);
bool w2_load_carriers(const w2_archive_t *arc, const uint32_t palette[256], spritecache_t *cache);
bool w2_load_assets(const char *data_root, const level_t *map, const char *sprite_name,
                    tileset_t *tileset, spritesheet_t *unit_sprite);
bool w2_load_runtime_sprites(const char *data_root, const level_t *map,
                             mobj_t *const *units, int unit_count, spritecache_t *cache);
int w2_era_palette(int era);
void w2_mark_footprint(int x, int y, isize2_t foot);
bool w2_cache_unit_sprite(const char *root, spritecache_t *cache, int pud);

/* Menu chrome is REZDAT. The in-game panel is MAINDAT. Each screen owns a font. */
typedef struct {
    spritesheet_t widgets[2]; /* 0 human, 1 orc */
    spritesheet_t panel[2];
    spritesheet_t title;
    bitmapfont_t font;
    bool ready;
} w2_menu_art_t;

typedef struct {
    spritesheet_t menu_button, minimap, info, buttons, resource, status, filler, icons, resource_icons;
    bitmapfont_t font, small_font;
    bool orc;
    bool ready;
} w2_hud_art_t;

bool w2_load_menu_art(const char *root, w2_menu_art_t *art);
void w2_free_menu_art(w2_menu_art_t *art);
bool w2_load_hud_art(const char *root, int era, bool orc, w2_hud_art_t *art);
void w2_free_hud_art(w2_hud_art_t *art);

#endif
