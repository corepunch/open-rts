#ifndef __KKND__
#define __KKND__

#include "engine.h"

bool load_kknd_map(const char *map_path, level_t *out);
bool kknd_load_assets(SDL_Renderer *renderer, const char *data_root,
                      const level_t *map, const char *sprite_name,
                      tileset_t *tileset, spritesheet_t *unit_sprite);

#endif
