#ifndef OPEN_RTS_CLIENT_GAME_UI_H
#define OPEN_RTS_CLIENT_GAME_UI_H

#include "engine.h"
#include "ui_definition.h"

typedef struct {
    const GameUiDefinition *definition;
    SDL_Texture *textures[RTS_UI_MAX_LAYERS];
    bool ready;
} GameUi;

bool game_ui_load(GameUi *ui, SDL_Renderer *renderer, const char *data_root,
                  const GameUiDefinition *definition);
void game_ui_render(const GameUi *ui, App *app, const GameMap *map,
                    const Unit *units, int unit_count, const SpriteCache *sprites);
void game_ui_destroy(GameUi *ui);

#endif
