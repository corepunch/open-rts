#ifndef __W_LVL__
#define __W_LVL__

#include "kknd.h"

enum { MAX_LAYERS = 3 };

typedef struct {
    isize2_t atlas;
    int tile_count;
    uint32_t palette[256];
    uint32_t *pixels;
} KkndMapData;

typedef struct {
    char name[32];
    uint16_t native_team;
    fvec2_t position;
} KkndMapUnit;

bool range_ok(size_t size, uint32_t offset, size_t length);
void map_data_destroy(void *opaque);
bool open_lvl(const char *path, blob_t *blob, const uint8_t **segment,
                   size_t *segment_size);
bool lvl_asset(const uint8_t *segment, size_t size, const char type[4],
                    int index, uint32_t *asset_offset);
int load_kknd_map_units(const char *path, KkndMapUnit *out, int max_units);

#endif
