#ifndef __W_SPR__
#define __W_SPR__
#include "m_vec.h"
#include "game.h"
#include <stdbool.h>

bool load_render_tables(const char *data_root, const char *tileset_name);
bool load_dark_colony_sprite(SDL_Renderer *renderer, const char *path,
                             spritesheet_t *out, uint32_t palette_out[256]);
bool load_dark_colony_unit_sprites(SDL_Renderer *renderer, const char *data_root,
                                   const level_t *map, const mobj_t *units, int unit_count,
                                   spritecache_t *cache);
#endif
