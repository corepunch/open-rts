#include "sc_local.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Selection circles and status bars as StarCraft draws them (OpenBW ui.h
 * draw_selection_circle / draw_health_bars), from sprites.dat geometry. */

enum { CIRCLES = 10, CIRCLE_IMAGE = 561, BAR_BORDER = 18 };
enum { CIRCLE_OWN, CIRCLE_NEUTRAL, CIRCLE_ENEMY, CIRCLE_TINTS };

typedef struct { uint8_t circle, vpos, bar; bool valid; } sc_selection_t;

static sc_selection_t selection[SC_TYPES];
static spritesheet_t circles[CIRCLES];
static uint8_t bar_colors[19];
static bool loaded;

static bool load_pcx_row(const char *root, const char *name, uint8_t *out, int count) {
    char path[2048];
    spritesheet_t sheet = {0};
    sc_asset_path(path, sizeof(path), root, name);
    if (!W_LoadIndexedSheet(path, &sheet)) return false;
    bool ok = sheet.numlumps > 0 && sheet.frame_size.w * sheet.frame_size.h >= count;
    if (ok) memcpy(out, sheet.lumps[0].indices, (size_t)count);
    R_FreeSprite(&sheet);
    return ok;
}

void sc_free_selection(void) {
    for (int i = 0; i < CIRCLES; i++) R_FreeSprite(&circles[i]);
    memset(selection, 0, sizeof(selection));
    loaded = false;
}

bool sc_load_selection(const char *root, const blob_t *units, const blob_t *flingy,
                       const blob_t *sprites, const blob_t *images, const blob_t *names) {
    sc_free_selection();
    unsigned nf = (unsigned)flingy->size / 15, ni = (unsigned)images->size / 38;
    /* Image ids, then health bar, circle and offset bytes for sprites 130 on
     * (unused and visible bytes cover every sprite). The original disc has
     * 386 sprites with 179 such rows; Brood War has 517 and 387. */
    unsigned ns = sprites->size == 2081 ? 386 : 130 + ((unsigned)sprites->size - 520) / 7;
    unsigned rows = ((unsigned)sprites->size - 4 * ns) / 3;
    const uint8_t *bar = sprites->bytes + 2 * ns, *circle = bar + rows + 2 * ns, *vpos = circle + rows;
    uint8_t tints[CIRCLE_TINTS * 8];
    if (!load_pcx_row(root, "game/tselect.pcx", tints, sizeof(tints)) ||
        !load_pcx_row(root, "game/thpbar.pcx", bar_colors, sizeof(bar_colors))) return false;
    for (int i = 0; i < CIRCLES; i++) {
        unsigned image = CIRCLE_IMAGE + i;
        const char *grp = image < ni ? sc_tbl_string(names, read_u32_le(images->bytes + image * 4)) : NULL;
        char path[512];
        blob_t file = {0};
        if (!grp) return false;
        snprintf(path, sizeof(path), "unit/%s", grp);
        bool ok = sc_read(root, path, &file) && sc_decode_grp(&file, sc_palette, false, &circles[i]);
        W_FreeFile(&file);
        if (!ok) return false;
        /* Image remap 13: shades 1..8 through the owner's tselect ramp. */
        circles[i].palette_maps = calloc(CIRCLE_TINTS, sizeof(*circles[i].palette_maps));
        if (!circles[i].palette_maps) return false;
        circles[i].palette_map_count = CIRCLE_TINTS;
        for (int t = 0; t < CIRCLE_TINTS; t++) {
            spritepalettemap_t *map = &circles[i].palette_maps[t];
            map->id = t;
            for (int k = 0; k < 256; k++) map->indices[k] = (uint8_t)k;
            for (int k = 1; k <= 8; k++) map->indices[k] = tints[t * 8 + k - 1];
        }
    }
    for (int i = 0; i < SC_TYPES; i++) {
        unsigned f = units->bytes[i];
        if (f >= nf) continue;
        unsigned s = read_u16_le(flingy->bytes + f * 2);
        if (s < 130 || s - 130 >= rows || circle[s - 130] >= CIRCLES) continue;
        selection[i] = (sc_selection_t){circle[s - 130], vpos[s - 130], bar[s - 130], true};
    }
    loaded = true;
    return true;
}

static const sc_selection_t *unit_selection(const mobj_t *unit) {
    if (!loaded || !sc_unit(unit) || !(unit->traits & MF_SELECTABLE) || !P_MobjIsSelected(unit) ||
        unit->hp <= 0) return NULL;
    const sc_selection_t *s = &selection[unit->type_id - 1];
    return s->valid ? s : NULL;
}

static ivec2_t sprite_origin(const unitoverlaycontext_t *ctx) {
    return (ivec2_t){(int)lroundf(ctx->anchor.x) + ctx->unit->core.render_offset.x,
                     (int)lroundf(ctx->anchor.y) + L_ScreenDY(ctx->unit->core.render_offset.y)};
}

void sc_draw_selection_circle(const unitoverlaycontext_t *ctx) {
    const sc_selection_t *s = unit_selection(ctx->unit);
    if (!s) return;
    const spritesheet_t *sheet = &circles[s->circle];
    const spritecell_t *cell = &sheet->cells[0];
    ivec2_t at = sprite_origin(ctx);
    irect_t dst = {at.x - cell->ground_point.x, at.y + s->vpos - cell->ground_point.y,
                   cell->rect.w, cell->rect.h};
    int tint = ctx->unit->owner == consoleplayer ? CIRCLE_OWN :
               ctx->unit->allegiance == ALLEGIANCE_ENEMY ? CIRCLE_ENEMY : CIRCLE_NEUTRAL;
    R_DrawSprite(sheet, 0, tint, NULL, &dst, 0, 16);
}

/* Fill in 3-pixel boxes; an empty bar still shows one. */
static int filled_width(int percent, int width) {
    int r = percent * width / 100;
    if (r < 3) r = 3;
    else if (r % 3 == 1) r--;
    else if (r % 3 == 2) r++;
    return r < width ? r : width;
}

static void bar_rows(int x, int y, int width, int fill, const uint8_t *on, const uint8_t *off,
                     int rows) {
    for (int row = 0; row < rows; row++) {
        V_FillRect((irect_t){x, y + row, fill, 1}, bar_colors[on[row]]);
        V_FillRect((irect_t){x + fill, y + row, width - fill, 1}, bar_colors[off[row]]);
        for (int dx = 0; dx < width; dx += 3)
            V_FillRect((irect_t){x + dx, y + row, 1, 1}, bar_colors[BAR_BORDER]);
    }
}

void sc_draw_status_bars(const unitoverlaycontext_t *ctx) {
    const mobj_t *unit = ctx->unit;
    const sc_selection_t *s = unit_selection(unit);
    if (!s || unit->max_hp <= 0) return;
    const sc_unit_t *type = sc_unit(unit);
    bool shield = type->shields > 0;
    int max_energy = (type->flags & SC_UNIT_SPELLCASTER) ? sc_max_energy(unit) >> 8 : 0;
    int height = 5 + (shield ? 2 : 0) + (max_energy > 0 ? 6 : 0);
    int width = s->bar;
    width -= (width - 1) % 3;
    if (width < 19) width = 19;
    ivec2_t at = sprite_origin(ctx);
    int offset = s->vpos + circles[s->circle].cells[0].rect.h / 2 + 8;
    int x = at.x - width / 2, y = at.y + offset - height / 2;
    int hp = unit->hp < unit->max_hp ? unit->hp : unit->max_hp;
    int percent = (int)((int64_t)hp * 100 / unit->max_hp);
    static const uint8_t hp5[3][5] = {{18, 0, 1, 2, 18}, {18, 3, 4, 5, 18}, {18, 6, 7, 8, 18}};
    static const uint8_t hp7[3][7] = {{18, 0, 0, 1, 1, 2, 18}, {18, 3, 3, 4, 4, 5, 18},
                                      {18, 6, 6, 7, 7, 8, 18}};
    static const uint8_t bg5[5] = {18, 15, 16, 17, 18}, bg7[7] = {18, 15, 15, 16, 16, 17, 18};
    int tier = percent >= 66 ? 0 : percent >= 33 ? 1 : 2;
    int rows = shield ? 7 : 5;
    bar_rows(x, y, width, filled_width(percent, width), shield ? hp7[tier] : hp5[tier],
             shield ? bg7 : bg5, rows);
    if (shield) {
        static const uint8_t on[4] = {18, 10, 11, 18}, off[4] = {18, 16, 17, 18};
        int fill = filled_width(sc_shields(unit) * 100 / type->shields, width);
        bar_rows(x, y, width, fill, on, off, 4);
    }
    if (max_energy > 0) {
        static const uint8_t on[5] = {18, 12, 13, 14, 18};
        int fill = filled_width(sc_energy(unit) * 100 / max_energy, width);
        bar_rows(x, y + rows + 1, width, fill, on, bg5, 5);
    }
}
