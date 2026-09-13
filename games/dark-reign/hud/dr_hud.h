#ifndef __DR_HUD__
#define __DR_HUD__

#include "sb_bar.h"

bool DR_PaletteResponder(sb_state_t *st, app_t *app, const SDL_Event *event);
void DR_PaletteDrawer(sb_state_t *st, const app_t *app);
irect_t DR_MinimapRect(const level_t *map);
void DR_DrawText(const app_t *app, ivec2_t point, const char *text, int width);

#endif
