#ifndef __W_SPR__
#define __W_SPR__
#include "engine.h"
#include "m_vec.h"
#include "game.h"
#include <stdbool.h>

bool load_render_tables(const char *data_root, const char *tileset_name);
bool load_dark_colony_sprite(const char *path,
                             spritesheet_t *out, uint32_t palette_out[256]);
bool load_dark_colony_unit_sprites(const char *data_root,
                                   const level_t *map, mobj_t *const *units, int unit_count,
                                   spritecache_t *cache);

/* Native little-endian file records. These are views, not decoded copies. */
typedef struct {
    uint16_t magic, frame_count, label_count, dependency_count;
} dc_fin_header_t;

typedef struct {
    char name[8];
} dc_fin_dependency_t;

typedef struct {
    char name[16];
    uint16_t start, end;
} dc_fin_label_t;

typedef struct {
    uint16_t part_count, ticks;
    uint8_t unknown_04[160];
} dc_fin_frame_t;

typedef struct {
    int16_t x, y;
} dc_file_point_t;

typedef struct {
    char sprite[8];
    int16_t cell;
    dc_file_point_t offset;
    int16_t remap; /* Native draw mode; 2 selects the alternate clipping path. */
    int16_t intensity, layer, flags;
} dc_fin_command_t;

typedef struct {
    uint16_t w, h;
} dc_file_size_t;

typedef struct {
    uint16_t x, y;
} dc_file_displacement_t;

typedef struct {
    uint16_t flags, cell_count;
    uint32_t payload_size;
    uint8_t palette[256][3];
} dc_spr_header_t;

typedef struct {
    dc_file_size_t size;
    dc_file_displacement_t displacement;
} dc_spr_cell_t;

/* Owns only the file buffer and a relocation index for variable-length frames.
 * All record pointers borrow the file. Free with DC_FreeFIN. */
typedef struct {
    blob_t file;
    const dc_fin_header_t *header;
    const dc_fin_dependency_t *dependencies;
    const dc_fin_label_t *labels;
    const dc_fin_frame_t *frames;
    const dc_fin_command_t *commands;
    const dc_fin_command_t **frame_commands;
    int command_count;
} dc_fin_t;

/* Consume a checked span from a borrowed file cursor; do not free the cursor. */
const void *DC_TakeRecords(blob_t *cursor, size_t count, size_t record_size);
bool DC_LoadFIN(const char *path, dc_fin_t *out);
void DC_FreeFIN(dc_fin_t *fin);
const dc_fin_label_t *DC_FINLabel(const dc_fin_t *fin, const char *name);
bool DC_FINLayer(const dc_fin_t *fin, int index, spritelayer_t *out);
/* Replaces an initialized direction on success. Layers belong to the caller. */
bool DC_FINFrame(const dc_fin_t *fin, int index, spritedirection_t *out);
#endif
