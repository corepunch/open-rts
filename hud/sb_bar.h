#ifndef __SB_STUFF__
#define __SB_STUFF__

#include "engine.h"
#include "ui_definition.h"

typedef struct {
    const uidefinition_t *definition;
    SDL_Texture *textures[RTS_UI_MAX_LAYERS];
    bool ready;
    bool first_draw;
    int pressed_button;
    uint64_t clock;
    uint32_t production_selection;
    int production_page;
    int production_category;
    spritesheet_t *product_icons;
    const spritecache_t *sprites; /* Borrowed from the world renderer for picking. */
    bool radar_visible;
    bool options_visible;
    uiactionkind_t order;
    int order_product;
} sb_state_t;

/* Doom-style status-bar lifecycle.  The explicit state argument replaces the
   original globals while keeping call sites directly comparable to sb_bar.c. */
bool SB_Init(sb_state_t *st, SDL_Renderer *renderer, const char *data_root,
             const uidefinition_t *definition);
void SB_Start(sb_state_t *st);
bool SB_Responder(sb_state_t *st, const app_t *app, const SDL_Event *event);
void SB_Ticker(sb_state_t *st);
void SB_Drawer(sb_state_t *st, app_t *app, const level_t *map,
               mobj_t *const *units, int unit_count, const spritecache_t *sprites,
               bool fullscreen, bool refresh);
void SB_Shutdown(sb_state_t *st);
bool SB_ProductionResponder(sb_state_t *st, app_t *app, const SDL_Event *event);
void SB_ProductionDrawer(sb_state_t *st, const app_t *app);
bool SB_PaletteResponder(sb_state_t *st, app_t *app, const SDL_Event *event);
void SB_PaletteDrawer(sb_state_t *st, const app_t *app);
irect_t SB_MinimapRect(const level_t *map);
void SB_DrawText(const app_t *app, ivec2_t point, const char *text, int width);
bool G_LoadMenuSprite(SDL_Renderer *renderer, const char *root,
                      const char *name, spritesheet_t *out);

#endif
