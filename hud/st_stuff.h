#ifndef __ST_STUFF__
#define __ST_STUFF__

#include "engine.h"
#include "ui_definition.h"

typedef struct {
    const uidefinition_t *definition;
    SDL_Texture *textures[RTS_UI_MAX_LAYERS];
    bool ready;
    bool first_draw;
    int pressed_button;
    uint64_t clock;
} st_state_t;

/* Doom-style status-bar lifecycle.  The explicit state argument replaces the
   original globals while keeping call sites directly comparable to st_stuff.c. */
bool ST_Init(st_state_t *st, SDL_Renderer *renderer, const char *data_root,
             const uidefinition_t *definition);
void ST_Start(st_state_t *st);
bool ST_Responder(st_state_t *st, const app_t *app, const SDL_Event *event);
void ST_Ticker(st_state_t *st);
void ST_Drawer(st_state_t *st, app_t *app, const level_t *map,
               const mobj_t *units, int unit_count, const spritecache_t *sprites,
               bool fullscreen, bool refresh);
void ST_Shutdown(st_state_t *st);

#endif
