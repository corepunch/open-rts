#ifndef RUSTFRONT_H
#define RUSTFRONT_H
/* Rustfront: an original RTS whose art is rendered with Scener (art/, docs/SCENER.md) and
 * packed into 256-colour BMPs (assets/) by art/pack8.py, which also writes sprites.inc. */

#include "engine.h"

/* One packed sheet: frames of w x h in `columns` columns, every frame grounded at pivot. */
typedef struct {
    const char *unit, *layer, *file;
    int w, h, columns, count, pivot_x, pivot_y;
    const short (*mount)[2]; /* Turret layers: offset of the mount from the hull pivot, per hull facing. */
} rf_sheetdef_t;

/* Infantry animation: lump = start + facing * frames + frame. */
typedef struct {
    const char *unit, *name;
    int start, frames, facings;
} rf_animdef_t;

bool RF_LoadLevel(const char *path, level_t *out);
bool RF_LoadAssets(const char *root, tileset_t *tileset);
int  RF_LoadThings(const char *path);
bool RF_LoadSprites(const char *root, spritecache_t *cache);

/* g_mission.c: building footprints block, idle harvesters find ore, a refinery comes with a
 * harvester. */
void RF_MissionTicker(level_t *map, mobj_t *const *mobjs, int count);
void RF_SyncBlocking(level_t *map);
void RF_ClearMinedOre(level_t *map);

/* hu_rustfront.c: the sidebar. */
#define RF_SIDEBAR_X 480
menu_t *RF_InitHUD(app_t *app, const char *root);
void RF_ShutdownHUD(void);

#endif
