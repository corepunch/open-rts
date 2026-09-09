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
                                   const level_t *map, const mobj_t *units, int unit_count,
                                   spritecache_t *cache);

/* FIN decoding writes directly into the common sprite structures.
 * Accessors take a buffer validated by DC_LoadFIN; release it with W_FreeFile.
 * DC_FINLabel returns a borrowed 20-byte file record (name[16], start, end).
 * DC_FINFrame replaces an initialized direction on success; its layers remain
 * valid after the FIN buffer is freed and belong to the caller. */
bool DC_LoadFIN(const char *path, blob_t *out);
const uint8_t *DC_FINLabel(const blob_t *fin, const char *name);
int DC_FINCommandCount(const blob_t *fin);
bool DC_FINLayer(const blob_t *fin, int index, spritelayer_t *out);
bool DC_FINFrame(const blob_t *fin, int index, spritedirection_t *out);
#endif
