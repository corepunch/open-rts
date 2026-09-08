#ifndef __W_SPR__
#define __W_SPR__
#include "actor.h"
#include "map.h"
#include "m_vec.h"
#include "sprites.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t default_ticks;
    uint16_t frame_count;
    uint16_t label_count;
    uint16_t dependency_count;
} dc_fin_header_t;

typedef struct __attribute__((packed)) {
    char name[SPRITE_LAYER_NAME_SIZE];
} dc_fin_dependency_t;

typedef struct __attribute__((packed)) {
    char name[SPRITE_FRAME_NAME_SIZE];
    uint16_t start;
    uint16_t end;
} dc_fin_label_t;

typedef struct __attribute__((packed)) {
    uint16_t part_count;
    uint16_t ticks;
    uint8_t unknown[160];
} dc_fin_frame_t;

typedef struct {
    void *data;
    size_t size;
    const dc_fin_header_t *header;
    const dc_fin_dependency_t *dependencies;
    const dc_fin_label_t *labels;
    const dc_fin_frame_t *frames;
    const spritelayer_t *layers;
    int layer_count;
} dc_fin_t;

_Static_assert(sizeof(dc_fin_header_t) == 8, "Dark Colony FIN header layout");
_Static_assert(sizeof(dc_fin_dependency_t) == 8, "Dark Colony FIN dependency layout");
_Static_assert(sizeof(dc_fin_label_t) == 20, "Dark Colony FIN label layout");
_Static_assert(sizeof(dc_fin_frame_t) == 164, "Dark Colony FIN frame layout");

bool W_LoadFin(const char *path, dc_fin_t *fin);
bool W_LoadFinForMap(const char *map_path, const char *name, dc_fin_t *fin);
void W_FreeFin(dc_fin_t *fin);
const dc_fin_label_t *W_FinLabel(const dc_fin_t *fin, const char *name);
const spritelayer_t *W_FinFrameLayers(const dc_fin_t *fin, int frame, int *count);
int W_FinFrameDuration(const dc_fin_t *fin, int frame);
bool load_render_tables(const char *data_root, const char *tileset_name);
bool load_dark_colony_sprite(const char *path,
                             spritesheet_t *out, uint32_t palette_out[256]);
bool R_PrecacheLevel(SDL_Renderer *renderer, const char *root, const level_t *map,
                     const mobj_t *units, int unit_count, spritecache_t *cache);
#endif
