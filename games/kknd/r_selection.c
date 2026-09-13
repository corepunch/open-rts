#include "game.h"
#include "kknd.h"
#include "info.h"

/* OpenKrush core.yaml: infantry 16, vehicles/towers 32, buildings 64. */
static const uint8_t bar_widths[NUMMOBJTYPES] = {
#define KK_PRODUCT(id,faction,category,kind,type,maker,cost,ticks,tech,limit,label) \
    [type] = category == 0 ? 16 : category == 2 ? 64 : 32,
#include "products.inc"
#undef KK_PRODUCT
};

static void fill(SDL_Renderer *renderer, irect_t rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void KK_DrawUnitOverlays(const unitoverlaycontext_t *ctx) {
    const mobj_t *unit = ctx->unit;
    if (!(unit->traits & MF_SELECTABLE) || !P_MobjIsSelected(unit) ||
        unit->hp <= 0 || unit->max_hp <= 0 || ctx->bounds.w <= 0) return;
    int width = unit->type_id < NUMMOBJTYPES ? bar_widths[unit->type_id] : 0;
    if (!width) width = (unit->traits & MF_MOBILE) ? 32 : 64;
    int height = width == 64 ? 3 : 2;
    int hp = unit->hp < unit->max_hp ? unit->hp : unit->max_hp;
    int progress = (int)((int64_t)(width - 4) * hp / unit->max_hp);
    ivec2_t origin = {ctx->bounds.x + (ctx->bounds.w - width) / 2,
                      ctx->bounds.y - height - 4};
    SDL_Renderer *renderer = ctx->app->renderer;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    /* OpenKrush StatusBar.Render: gray frame, dark inset, two-tone fill.
     * Position above our current sprite bounds; no OpenRA mouse-bound offsets. */
    fill(renderer, (irect_t){origin.x, origin.y, width, height + 4},
         (SDL_Color){206,206,206,255});
    fill(renderer, (irect_t){origin.x + 1, origin.y + 1, width - 2, height + 2},
         (SDL_Color){16,16,16,255});
    fill(renderer, (irect_t){origin.x + 2, origin.y + 2, width - 4, 1},
         (SDL_Color){206,206,206,255});
    fill(renderer, (irect_t){origin.x + 2, origin.y + 3, width - 4, height - 1},
         (SDL_Color){49,49,49,255});
    fill(renderer, (irect_t){origin.x + 2, origin.y + 2, progress, 1},
         (SDL_Color){0,255,0,255});
    fill(renderer, (irect_t){origin.x + 2, origin.y + 3, progress, height - 1},
         (SDL_Color){0,181,0,255});
}
