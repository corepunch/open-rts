#include "engine.h"
#include "kknd.h"
#include "info.h"

/* OpenKrush core.yaml: infantry 16, vehicles/towers 32, buildings 64. */

static void fill(irect_t rect, uint32_t argb) {
    V_FillRect(rect, V_NearestIndex(argb));
}

void KK_DrawUnitOverlays(const unitoverlaycontext_t *ctx) {
    const mobj_t *unit = ctx->unit;
    if (!(unit->traits & MF_SELECTABLE) || !P_MobjIsSelected(unit) ||
        unit->hp <= 0 || unit->max_hp <= 0 || ctx->bounds.w <= 0) return;
    int width = unit->type_id >= MT_SURV_RIFLEMAN && unit->type_id <= MT_MUTE_CRAZY_HARRY ? 16 :
                unit->traits & (MF_MOBILE | MF_ATTACK) ? 32 : 64;
    int height = width == 64 ? 3 : 2;
    int hp = unit->hp < unit->max_hp ? unit->hp : unit->max_hp;
    int progress = (int)((int64_t)(width - 4) * hp / unit->max_hp);
    ivec2_t origin = {ctx->bounds.x + (ctx->bounds.w - width) / 2,
                      ctx->bounds.y - height - 4};
    /* OpenKrush StatusBar.Render: gray frame, dark inset, two-tone fill.
     * Position above our current sprite bounds; no OpenRA mouse-bound offsets. */
    fill((irect_t){origin.x, origin.y, width, height + 4}, 0xffcececeu);
    fill((irect_t){origin.x + 1, origin.y + 1, width - 2, height + 2}, 0xff101010u);
    fill((irect_t){origin.x + 2, origin.y + 2, width - 4, 1}, 0xffcececeu);
    fill((irect_t){origin.x + 2, origin.y + 3, width - 4, height - 1}, 0xff313131u);
    fill((irect_t){origin.x + 2, origin.y + 2, progress, 1}, 0xff00ff00u);
    fill((irect_t){origin.x + 2, origin.y + 3, progress, height - 1}, 0xff00b500u);
}
