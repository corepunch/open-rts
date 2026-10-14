#ifndef __Z_LOCAL__
#define __Z_LOCAL__

#include "engine.h"
#include "z.h"

enum {
    Z_WORLDS = 5,     /* desert, arctic, city, volcano, jungle */
    Z_NEUTRAL = 4,    /* Team of a territory nobody holds. */
    Z_MAX_TERRITORIES = 64,
    Z_MAX_FORTS = 2,
    Z_LEVELS = 51,    /* LEVELS.DAT entries */
    Z_MAX_GROUPS = 12, /* Starting groups per level, LEVELS.DAT +0x55 */
    Z_MAX_PLACED = 27, /* CPUPLRnn.DAT placements after the territories */
};

/* A group the computer starts with, CPUPLRnn.DAT +0x792. */
typedef struct {
    int group;
    ivec2_t at; /* Pixels, the first member's top-left. */
} zplaced_t;

typedef struct {
    ivec2_t min, max; /* Cells, max exclusive. */
    int team;
} zterritory_t;

/* What a level keeps of its native files after G_DoLoadLevel. */
typedef struct {
    char root[1024], dir[1024];
    char objects[16], buildings[16], robots[16]; /* LEVELS.DAT file names. */
    int number;                      /* LEVELS.DAT entry */
    int world;                       /* LEVELS.DAT +0x6e */
    uint8_t groups[Z_MAX_GROUPS];    /* ROBOTS.DAT groups, 0xff ends */
    zplaced_t placed[Z_MAX_PLACED];
    int placed_count;
    zterritory_t territories[Z_MAX_TERRITORIES];
    int territory_count;
    ivec2_t forts[Z_MAX_FORTS];      /* Pixels; the first is drawn with its gate. */
    int fort_count;
    uint8_t terrain[Z_MAP_STRIDE * Z_MAP_STRIDE]; /* Attribute plane. */
    blob_t rsc;                      /* SPRITES.RSC */
} zmap_t;

bool Z_LoadLevel(const char *map_path, level_t *out);
int Z_LoadThings(void);
int Z_TeamAt(const zmap_t *map, ivec2_t pixel);
/* Size of a SPRITES.RSC picture; false for a bad index or file. */
bool Z_SpriteSize(const zmap_t *map, int index, isize2_t *size);
bool Z_LoadAssets(const char *root, const level_t *map, tileset_t *tileset,
                  spritesheet_t *unit_sprite);
bool Z_InitSprites(const level_t *map, spritecache_t *cache);

/* Build the destructible cliffs into the planes, as a level start does. */
void Z_StampCliffs(uint8_t *tiles, uint8_t *attrs, const uint8_t *blocks, int world);
/* World sprite of a building type, or -1. */
int Z_BuildingSprite(int native, int world);

#endif
