#ifndef __DC_SB_BAR__
#define __DC_SB_BAR__

#include "engine.h"
#include "game.h"

void *DC_SB_Init(app_t *app, const char *data_root);
bool  DC_SB_Responder(void *sb, const app_t *app, level_t *map,
                   mobj_t *units, int unit_count, const SDL_Event *event);
void  DC_SB_Ticker(void *sb);
void  DC_SB_Drawer(void *sb, app_t *app, const level_t *map,
                const mobj_t *units, int unit_count,
                const spritecache_t *sprites, const hudtext_t *hud);
void  DC_SB_Shutdown(void *sb);
int   DC_SB_WorldViewportWidth(const app_t *app);

#endif
