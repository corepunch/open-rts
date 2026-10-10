#include "sc_local.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* The sparks a Terran worker throws off a mineral field. Every swing of the
 * cutter (SC_WORK_PERIOD tics, started in sc_unit_ticker) plays scvspark.grp
 * once, one picture per 24 Hz frame, where the beam meets the field. The
 * sparks are drawn from the swing's phase, so they cost the simulation and
 * saves nothing. */

enum { REACH_PX = 13, LIFT_PX = 5 };

static spritesheet_t spark;
static bool loaded;

/* The images.dat row that names bullet\scvspark.grp. */
void sc_load_work_spark(const char *root, const blob_t *images, const blob_t *names) {
    R_FreeSprite(&spark);
    loaded = false;
    unsigned ni = (unsigned)images->size / 38;
    for (unsigned image = 0; image < ni; image++) {
        const char *grp = sc_tbl_string(names, read_u32_le(images->bytes + image * 4));
        char path[512];
        blob_t file = {0};
        if (!grp || strcasecmp(grp, "bullet\\scvspark.grp")) continue;
        snprintf(path, sizeof(path), "unit/%s", grp);
        loaded = sc_read(root, path, &file) && sc_decode_grp(&file, sc_palette, false, &spark);
        W_FreeFile(&file);
        if (!loaded) R_FreeSprite(&spark);
        return;
    }
}

static void draw_spark(const unitoverlaycontext_t *ctx) {
    const mobj_t *unit = ctx->unit;
    if (!loaded || !sc_unit(unit) || !sc_mining(unit)) return;
    /* The strike lands a frame into the swing. */
    int age = (leveltime + (int)unit->id) % SC_WORK_PERIOD - 1;
    int frame = age * 24 / RTS_TICRATE;
    if (age < 0 || frame >= spark.spritedef.numframes) return;
    const resourcevent_t *vent = &level.resource_vents[unit->harvest.target];
    fixed2_t to = fixed2_sub(vent->attachment, fixed3_xy(unit->core.position));
    float dx = (float)to.x / FIXED_ONE, dy = (float)to.y / FIXED_ONE, length = hypotf(dx, dy);
    if (length < 0.01f) return;
    const spritecell_t *cell = &spark.cells[frame];
    int x = (int)lroundf(ctx->anchor.x) + unit->core.render_offset.x + (int)lroundf(dx / length * REACH_PX);
    int y = (int)lroundf(ctx->anchor.y) + L_ScreenDY(unit->core.render_offset.y) +
            (int)lroundf(L_ScreenDY((int)lroundf(dy / length * REACH_PX))) - LIFT_PX;
    irect_t dst = {x - cell->ground_point.x, y - cell->ground_point.y, cell->rect.w, cell->rect.h};
    R_DrawSprite(&spark, frame, 0, NULL, &dst, 0, 16);
}

void sc_draw_overlays(const unitoverlaycontext_t *ctx) {
    sc_draw_status_bars(ctx);
    draw_spark(ctx);
}
