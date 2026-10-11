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
static spritesheet_t glow;
static uint8_t fire_map[256 * 256];
static bool spark_loaded, glow_loaded;

/* images.dat names both the cutter sparks and SCVWalking's imgol 249.
 * The latter uses images.dat draw function 9 with the active tileset's
 * ofire.pcx as a source-over-destination indexed lookup. */
bool sc_load_worker_effects(const char *root, const char *tileset,
                            const blob_t *images, const blob_t *names) {
    R_FreeSprite(&spark);
    R_FreeSprite(&glow);
    spark_loaded = glow_loaded = false;
    unsigned ni = (unsigned)images->size / 38, glow_image = 249;
    if (ni <= glow_image || images->bytes[ni * 4 + glow_image] != 1 ||
        images->bytes[ni * 8 + glow_image] != 9 || images->bytes[ni * 9 + glow_image] != 1)
        return false;
    const char *name = sc_tbl_string(names, read_u32_le(images->bytes + glow_image * 4));
    if (!name || strcasecmp(name, "thingy\\tscGlow.grp")) return false;
    char path[512];
    blob_t file = {0};
    snprintf(path, sizeof(path), "unit/%s", name);
    glow_loaded = sc_read(root, path, &file) && sc_decode_grp(&file, sc_palette, true, &glow) &&
                  glow.numlumps == 68;
    W_FreeFile(&file);
    if (!glow_loaded) return false;
    snprintf(path, sizeof(path), "%s/native/tileset/%s/ofire.pcx", root, tileset);
    spritesheet_t fire = {0};
    if (!W_LoadIndexedSheet(path, &fire) || fire.frame_size.w != 256 ||
        fire.frame_size.h != 63 || !fire.lumps || !fire.lumps[0].indices) {
        R_FreeSprite(&fire);
        return false;
    }
    for (int source = 0; source < 256; ++source) {
        uint8_t *row = fire_map + source * 256;
        if (source > 0 && source <= 63)
            memcpy(row, fire.lumps[0].indices + (source - 1) * 256, 256);
        else for (int destination = 0; destination < 256; ++destination) row[destination] = destination;
    }
    R_FreeSprite(&fire);
    for (unsigned image = 0; image < ni; ++image) {
        name = sc_tbl_string(names, read_u32_le(images->bytes + image * 4));
        if (!name || strcasecmp(name, "bullet\\scvspark.grp")) continue;
        snprintf(path, sizeof(path), "unit/%s", name);
        spark_loaded = sc_read(root, path, &file) && sc_decode_grp(&file, sc_palette, false, &spark);
        W_FreeFile(&file);
        break;
    }
    return spark_loaded;
}

static void draw_spark(const unitoverlaycontext_t *ctx) {
    const mobj_t *unit = ctx->unit;
    if (!spark_loaded || !sc_unit(unit) || !sc_mining(unit)) return;
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

static void draw_glow(const unitoverlaycontext_t *ctx) {
    const mobj_t *unit = ctx->unit;
    if (!glow_loaded || unit->type_id != MT_SCV || !ctx->game_info ||
        unit->core.state_id <= 0 || unit->core.state_id >= ctx->game_info->state_count ||
        ctx->game_info->states[unit->core.state_id].group != 2) return;
    /* engset 0/1 selects the parent's current frame or that frame plus the
     * parent's entire 51-picture GRP. SCVWalking holds parent frame set 0. */
    int frame = unit->sc.glow.phase ? 3 : 0;
    int facing = angle_to_direction(unit->core.angle, 32, ANG90, false);
    const spritelayer_t *part = glow.spritedef.spriteframes[frame].directions[facing].layers;
    if (!part || part->lump >= glow.numlumps) return;
    const spritecell_t *cell = &glow.cells[part->lump];
    ivec2_t pivot = cell->ground_point;
    if (part->flags & RTS_FRAME_FLIP_X) pivot.x = cell->rect.w - pivot.x;
    ivec2_t at = {(int)lroundf(ctx->anchor.x) + unit->core.render_offset.x - pivot.x,
                  (int)lroundf(ctx->anchor.y) + L_ScreenDY(unit->core.render_offset.y) - pivot.y};
    V_DrawBlockTranslucent(at, glow.lumps[part->lump].indices,
                           (isize2_t){cell->rect.w, cell->rect.h}, cell->rect.w,
                           fire_map, part->flags & RTS_FRAME_FLIP_X ? V_FLIP_X : 0);
}

void sc_draw_overlays(const unitoverlaycontext_t *ctx) {
    draw_glow(ctx);
    sc_draw_status_bars(ctx);
    draw_spark(ctx);
}
