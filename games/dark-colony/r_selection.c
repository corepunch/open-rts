#include "r_selection.h"
#include "info.h"
#include "gamestat.h"

#include <math.h>
#include <limits.h>
#include <string.h>
#include <strings.h>

/* Type-wide origins belong to DC presentation, not renderer image storage. */
static ivec2_t selection_origins[NUMSPRITES];

void DC_LoadSelectionOrigin(const char *stem, const dc_fin_t *fin, const spritecache_t *cache) {
    int sprite = 0;
    while (sprite < NUMSPRITES && strcasecmp(stem, sprnames[sprite])) ++sprite;
    if (sprite == NUMSPRITES || !fin->header) return;
    selection_origins[sprite] = (ivec2_t){0};
    const dc_fin_label_t *standing = DC_FINLabel(fin, M_va("%sSTAND0", stem));
    if (!standing) return;
    int first = SDL_SwapLE16(standing->start);
    if (first >= SDL_SwapLE16(fin->header->frame_count)) return;
    const dc_fin_point_t *point = &fin->frames[first].points[6];
    /* DC.EXE 0x4389af / 0x423ccc: STAND0 attachment slot 6. */
    if (strncmp(point->name, "NONAME", sizeof(point->name))) {
        selection_origins[sprite] = (ivec2_t){
            (int16_t)SDL_SwapLE16(point->x), (int16_t)SDL_SwapLE16(point->y),
        };
        return;
    }
    /* 0x4239f0 unions all standing directions; 0x4389db uses its top.
     * Include every authored layer, with native cell extents, not pixel bounds. */
    int top = INT_MAX;
    for (int d = 0; d < 32; ++d) {
        const dc_fin_label_t *label = DC_FINLabel(fin, M_va("%sSTAND%d", stem, d));
        if (!label) continue;
        int start = SDL_SwapLE16(label->start), end = SDL_SwapLE16(label->end);
        if (end >= SDL_SwapLE16(fin->header->frame_count)) continue;
        for (int f = start; f <= end; ++f) {
            for (int p = 0; p < SDL_SwapLE16(fin->frames[f].part_count); ++p) {
                const dc_fin_command_t *part = &fin->frame_commands[f][p];
                const spritesheet_t *source = R_CacheLookup(cache, M_va("%.8s", part->sprite));
                if (!source)
                    source = R_CacheLookup(cache, M_va("SPRITES/%.8s.SPR", part->sprite));
                int cell = (int16_t)SDL_SwapLE16(part->cell);
                if (!source || cell < 0 || cell >= source->numlumps) continue;
                int y = (int16_t)SDL_SwapLE16(part->offset.y) - source->cells[cell].rect.h;
                if (y < top) top = y;
            }
        }
    }
    if (top != INT_MAX) selection_origins[sprite] = (ivec2_t){0, top};
}

static void draw_marker(const unitoverlaycontext_t *ctx, const spritesheet_t *client,
                        int frame, ivec2_t position) {
    if (frame < 0 || frame >= client->numlumps) return;
    irect_t cell = client->cells[frame].rect;
    irect_t dst = {position.x, position.y, cell.w, cell.h};
    R_DrawSprite(ctx->app->renderer, client, frame, -1, NULL, &dst,
                 SDL_FLIP_NONE, (SDL_Color){255,255,255,255}, SDL_BLENDMODE_BLEND);
}

void DC_DrawUnitOverlays(const unitoverlaycontext_t *ctx) {
    const mobj_t *unit = ctx->unit;
    if (!(unit->traits & MF_SELECTABLE) || unit->hp <= 0 || unit->max_hp <= 0 ||
        unit->native_type_id >= GAMESTAT_UNIT_COUNT || !ctx->cache || !ctx->cache->ui) return;
    const DcGamestatUnit *type = &dc_gamestat_units[unit->native_type_id];
    unsigned flags = type->values[23]; /* Native type +0xf4, presentation flags. */
    bool selected = P_MobjIsSelected(unit);
    if (!selected && !(flags & 4)) return;
    const spritesheet_t *client = R_CacheLookup(ctx->cache->ui, ctx->game_info->selection_marker.image);
    if (!client || !client->numlumps) return;
    int sprite = 0;
    while (sprite < NUMSPRITES && strcmp(type->sprite, sprnames[sprite])) ++sprite;
    if (sprite == NUMSPRITES) return;
    ivec2_t origin = ivec2_add(selection_origins[sprite], unit->core.render_offset);
    origin = ivec2_add(origin, (ivec2_t){(int)lroundf(ctx->anchor.x), (int)lroundf(ctx->anchor.y)});
    irect_t first = client->cells[0].rect;
    origin = ivec2_sub(origin, (ivec2_t){first.w / 2, first.h});
    int rank = (flags & 0x70) >> 4;
    /* 0x43349e / 0x433753: the rank badge persists independently of selection. */
    if (flags & 4)
        draw_marker(ctx, client, (type->values[GAMESTAT_UNIT_RACE] == 0 ? 35 : 47) + rank,
                    ivec2_add(origin, (ivec2_t){0, -3 * rank}));
    if (!selected) return;
    int health = (int)((int64_t)unit->hp * 100 / unit->max_hp);
    int color = health > 80 ? 0 : health > 60 ? 1 : health > 40 ? 2 : health > 20 ? 3 : 4;
    int frame = color + ((flags & 2) ? 5 : (flags & 4) ? 10 : 0);
    ivec2_t offset = {0, (flags & 4) ? -8 - 3 * rank : -2};
    draw_marker(ctx, client, frame, ivec2_add(origin, offset));
    /* 0x433857: commander ability meter above the star and rank badge. */
    if (flags & 8) {
        int meter = unit->ability_charge * 9 / 32;
        if (meter > 9) meter = 9;
        draw_marker(ctx, client, 15 + meter, ivec2_add(origin, (ivec2_t){0, -12 - 3 * rank}));
    }
}
