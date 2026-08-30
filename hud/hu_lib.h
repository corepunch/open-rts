#ifndef __HU_LIB__
#define __HU_LIB__

#include "engine.h"
#include "ui_definition.h"

typedef struct {
    const uidefinition_t *definition;
    SDL_Texture *textures[RTS_UI_MAX_LAYERS];
    bool ready;
} GameUi;

bool game_ui_load(GameUi *ui, SDL_Renderer *renderer, const char *data_root,
                  const uidefinition_t *definition);
void game_ui_render(const GameUi *ui, app_t *app, const level_t *map,
                    const mobj_t *units, int unit_count, const spritecache_t *sprites);
void game_ui_destroy(GameUi *ui);

#endif
