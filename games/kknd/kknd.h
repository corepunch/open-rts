#ifndef __KKND__
#define __KKND__

#include "engine.h"

enum { KKND_RESEARCH = 9000 };
bool KK_Research(mobj_t *target);
int KK_NextTechLevel(const mobj_t *actor);

bool load_kknd_map(const char *map_path, level_t *out);
bool W_LoadKkndPNG(SDL_Renderer *renderer, const char *name, spritesheet_t *out);
bool load_assets(SDL_Renderer *renderer, const char *data_root,
                      const level_t *map, const char *sprite_name,
                      tileset_t *tileset, spritesheet_t *unit_sprite);

#endif
