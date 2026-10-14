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

/* ZED.EXE turns a turret an eighth of a circle toward its target, then
 * waits three updates (0x58b7a); here it turns smoothly at that rate. */
#define Z_TURRET_TURN (ANG45 / 3u)

/* A group the computer starts with, CPUPLRnn.DAT +0x792. */
typedef struct {
    int group;
    ivec2_t at; /* Pixels, the first member's top-left. */
} zplaced_t;

typedef struct {
    ivec2_t min, max; /* Cells, max exclusive. */
    int team;
    ivec2_t flag;     /* Cell; whoever reaches it takes the territory. */
    uint8_t options;  /* +0x18: bits 0 and 1 leave it without a flag. */
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
/* Give a thing to team 0..3, or Z_NEUTRAL for nobody (owner 7). */
void Z_SetTeam(mobj_t *mo, int team);
/* Centre of a unit of weapon type `t` whose first picture's top-left is
 * at `pixel`; false for a bad type. */
bool Z_UnitCentre(const zmap_t *map, int t, ivec2_t pixel, fixed2_t *centre);
/* Weapon type of a unit's mobj type, or -1. */
int Z_UnitType(const mobj_t *mo);
bool Z_IsRobot(int t);
/* Size of a SPRITES.RSC picture; false for a bad index or file. */
bool Z_SpriteSize(const zmap_t *map, int index, isize2_t *size);
bool Z_LoadAssets(const char *root, const level_t *map, tileset_t *tileset,
                  spritesheet_t *unit_sprite);
bool Z_InitSprites(const level_t *map, spritecache_t *cache);

/* p_capture.c: flags, vehicles and grenades, run once a tic. */
void Z_Ticker(void);
bool Z_BoardOrder(mobj_t *const *units, int count, mobj_t *target);
bool Z_CanTarget(const mobj_t *attacker, const mobj_t *victim);
void Z_MobjTicker(mobj_t *mo);
bool Z_Attack(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target);
/* p_alert.c: an idle unit takes its squad's enemy or one close by. */
void Z_Alert(mobj_t *unit);

/* Build the destructible cliffs into the planes, as a level start does. */
void Z_StampCliffs(uint8_t *tiles, uint8_t *attrs, const uint8_t *blocks, int world);
/* World sprite of a building type, or -1. */
int Z_BuildingSprite(int native, int world);

#endif
