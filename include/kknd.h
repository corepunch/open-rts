#ifndef __KKND__
#define __KKND__

#include "engine.h"


enum { KKND_RESEARCH = 9000 };
/* Oil a drill rig holds and the rate a docked tanker draws per second. A
 * tanker (capacity 100) fills in ten seconds. Gameplay values, not retail. */
enum { KK_DRILLRIG_OIL = 20000, KK_DRILLRIG_RATE = 10 };
void KK_DrawUnitOverlays(const unitoverlaycontext_t *ctx);
bool KK_Research(mobj_t *target);
int KK_NextTechLevel(const mobj_t *actor);
/* The catalog faction (1 Survivor, 2 Mutant) of what `owner` fields, or -1. */
int KK_OwnerFaction(int owner);

bool load_kknd_map(const char *map_path, level_t *out);
bool load_assets(const char *data_root, const level_t *map,
                 const char *sprite_name, tileset_t *tileset,
                 spritesheet_t *unit_sprite);


enum { MAX_LAYERS = 3 };

typedef struct {
    int tile_count;
    uint32_t palette[256];
    uint8_t *indices; /* tile_count tiles, 32x32, contiguous */
} KkndMapData;

typedef struct {
    char name[32];
    uint16_t native_team;
    fixed2_t position;
} KkndMapUnit;

bool range_ok(size_t size, uint32_t offset, size_t length);
void map_data_destroy(void *opaque);
bool open_lvl(const char *path, blob_t *blob, const uint8_t **segment,
                   size_t *segment_size);
bool lvl_asset(const uint8_t *segment, size_t size, const char type[4],
                    int index, uint32_t *asset_offset);
int load_kknd_map_units(const char *path, KkndMapUnit *out, int max_units);


#endif
