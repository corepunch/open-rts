#ifndef OPEN_RTS_KKND_H
#define OPEN_RTS_KKND_H

#include "plugin.h"

bool load_kknd_map(const char *map_path, GameMap *out);
bool kknd_load_assets(SDL_Renderer *renderer, const char *data_root,
                      const GameMap *map, const char *sprite_name,
                      Tileset *tileset, SpriteSheet *unit_sprite);

#endif
