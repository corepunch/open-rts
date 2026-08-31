#define _DEFAULT_SOURCE
#include "p_local.h"

static irect_t sprite_visible_bounds(const spritesheet_t *sprite, int frame);
static irect_t sprite_frame_rect(const spritesheet_t *sprite, int frame);

static int app_cell_w(const app_t *app) {
    return app->cell.w > 0 ? app->cell.w : CELL_W;
}

static int app_cell_h(const app_t *app) {
    return app->cell.h > 0 ? app->cell.h : CELL_H;
}

static float viewport_scale_x(const app_t *app) {
    int window_w = 0, window_h = 0;
    int render_w = 0, render_h = 0;
    if (!app || !app->window || !app->renderer) return 1.0f;
    SDL_GetWindowSize(app->window, &window_w, &window_h);
    if (SDL_GetRendererOutputSize(app->renderer, &render_w, &render_h) != 0 ||
        window_w <= 0 || window_h <= 0 || render_w <= 0 || render_h <= 0) {
        return 1.0f;
    }
    return (float)render_w / (float)window_w;
}

static float viewport_scale_y(const app_t *app) {
    int window_w = 0, window_h = 0;
    int render_w = 0, render_h = 0;
    if (!app || !app->window || !app->renderer) return 1.0f;
    SDL_GetWindowSize(app->window, &window_w, &window_h);
    if (SDL_GetRendererOutputSize(app->renderer, &render_w, &render_h) != 0 ||
        window_w <= 0 || window_h <= 0 || render_w <= 0 || render_h <= 0) {
        return 1.0f;
    }
    return (float)render_h / (float)window_h;
}

static int app_tile_w(const app_t *app, const tileset_t *tileset) {
    (void)app;
    return tileset->tile_w;
}

static int app_tile_h(const app_t *app, const tileset_t *tileset) {
    (void)app;
    return tileset->tile_h;
}

static int tileset_animate_value(const tileset_t *tileset, int value, uint32_t ticks_ms) {
    if (!tileset->animations || tileset->animation_count <= 0) return value;
    for (int i = 0; i < tileset->animation_count; ++i) {
        const TileAnimation *anim = &tileset->animations[i];
        if (anim->value != value) continue;
        int frame = (int)((ticks_ms / anim->frame_ms) % (uint32_t)anim->frame_count);
        return anim->frames[frame];
    }
    return value;
}

static int tileset_resolve_tile(const tileset_t *tileset, int value, uint32_t ticks_ms) {
    value = tileset_animate_value(tileset, value, ticks_ms);
    if (value < 0 || tileset->count <= 0) return -1;
    if (tileset->tile_lookup) {
        if (value < tileset->tile_lookup_count) {
            int tile = tileset->tile_lookup[value];
            if (tile >= 0 && tile < tileset->count) return tile;
        }
        return -1;
    }
    if (value < tileset->count) return value;
    return value % tileset->count;
}


void R_GridToScreen(const app_t *app, float gx, float gy, float *sx, float *sy) {
    *sx = gx * (float)app_cell_w(app) + app->cam.x;
    *sy = gy * (float)app_cell_h(app) + app->cam.y;
}

void R_MapToScreen(const app_t *app, const level_t *map, float gx, float gy,
                        float *sx, float *sy) {
    float screen_y = L_ScreenYF(map, gy);
    R_GridToScreen(app, gx, screen_y, sx, sy);
}

static void screen_to_grid_point(const app_t *app, int sx, int sy, float *gx, float *gy) {
    if (gx) *gx = ((float)sx - app->cam.x) / (float)app_cell_w(app);
    if (gy) *gy = ((float)sy - app->cam.y) / (float)app_cell_h(app);
}

cell_t R_ScreenToGrid(const app_t *app, int sx, int sy) {
    return (cell_t){ (int)floorf(((float)sx - app->cam.x) / (float)app_cell_w(app)),
                   (int)floorf(((float)sy - app->cam.y) / (float)app_cell_h(app)) };
}

cell_t R_ScreenToMapGrid(const app_t *app, const level_t *map, int sx, int sy) {
    float gx = 0.0f, gy = 0.0f;
    screen_to_grid_point(app, sx, sy, &gx, &gy);
    gy = L_WorldYF(map, gy);
    return (cell_t){ (int)floorf(gx), (int)floorf(gy) };
}

static void screen_to_map_grid_point(const app_t *app, const level_t *map, int sx, int sy,
                                     float *gx, float *gy) {
    screen_to_grid_point(app, sx, sy, gx, gy);
    if (gy) *gy = L_WorldYF(map, *gy);
}

void R_RefreshViewport(app_t *app) {
    if (!app || !app->window || !app->renderer) return;
    int render_w = 0, render_h = 0;
    if (SDL_GetRendererOutputSize(app->renderer, &render_w, &render_h) != 0 ||
        render_w <= 0 || render_h <= 0) {
        SDL_GetWindowSize(app->window, &render_w, &render_h);
    }
    if (render_w > 0) app->win.w = render_w;
    if (render_h > 0) app->win.h = render_h;
}

void R_WindowToRenderPt(const app_t *app, int wx, int wy, int *rx, int *ry) {
    float sx = viewport_scale_x(app);
    float sy = viewport_scale_y(app);
    if (rx) *rx = (int)lroundf((float)wx * sx);
    if (ry) *ry = (int)lroundf((float)wy * sy);
}

void R_WindowToRenderDelta(const app_t *app, int wx, int wy, float *rx, float *ry) {
    float sx = viewport_scale_x(app);
    float sy = viewport_scale_y(app);
    if (rx) *rx = (float)wx * sx;
    if (ry) *ry = (float)wy * sy;
}

void R_DrawCell(app_t *app, int gx, int gy, SDL_Color color) {
    float sx, sy;
    R_GridToScreen(app, (float)gx, (float)gy, &sx, &sy);
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, color.a);
    irect_t r = { (int)sx, (int)sy, app_cell_w(app), app_cell_h(app) };
    SDL_RenderDrawRect(app->renderer, &r);
}

static void render_map_grid_cell(app_t *app, const level_t *map, int gx, int gy, SDL_Color color) {
    float sx, sy;
    R_MapToScreen(app, map, (float)gx, (float)gy, &sx, &sy);
    irect_t r = { (int)sx, (int)sy, app_cell_w(app), app_cell_h(app) };
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(app->renderer, &r);
}

static void render_blocked_overlay(app_t *app, const level_t *map) {
    if (!app || !map || !map->blocked) return;
    int cell_w = app_cell_w(app);
    int cell_h = app_cell_h(app);
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            if (!map->blocked[L_Index(map, x, y)]) continue;
            float sx, sy;
            R_MapToScreen(app, map, (float)x, (float)y, &sx, &sy);
            if (sx < -cell_w || sy < -cell_h ||
                sx > app->win.w + cell_w || sy > app->win.h + cell_h) {
                continue;
            }
            irect_t r = { (int)sx, (int)sy, cell_w, cell_h };
            SDL_SetRenderDrawColor(app->renderer, 230, 45, 40, 92);
            SDL_RenderFillRect(app->renderer, &r);
            SDL_SetRenderDrawColor(app->renderer, 255, 205, 64, 180);
            SDL_RenderDrawRect(app->renderer, &r);
        }
    }
    SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
}

void R_DrawTile(app_t *app, const tileset_t *tileset, int tile, irect_t src_part, irect_t dst_part) {
    tile = tileset_resolve_tile(tileset, tile, app->ticks_ms);
    if (!tileset->texture || tile < 0 || tile >= tileset->count) return;
    irect_t src = {
        (tile % tileset->atlas_cols) * tileset->tile_w + src_part.x,
        (tile / tileset->atlas_cols) * tileset->tile_h + src_part.y,
        src_part.w,
        src_part.h,
    };
    SDL_RenderCopy(app->renderer, tileset->texture, &src, &dst_part);
}

static void render_tile_at_flipped(app_t *app, const tileset_t *tileset, int tile,
                                   irect_t src_part, irect_t dst_part, uint8_t transforms) {
    tile = tileset_resolve_tile(tileset, tile, app->ticks_ms);
    if (!tileset->texture || tile < 0 || tile >= tileset->count) return;
    irect_t src = {
        (tile % tileset->atlas_cols) * tileset->tile_w + src_part.x,
        (tile / tileset->atlas_cols) * tileset->tile_h + src_part.y,
        src_part.w,
        src_part.h,
    };
    SDL_RendererFlip flip = SDL_FLIP_NONE;
    if (transforms & MAP_TILE_TRANSFORM_FLIP_X)
        flip = (SDL_RendererFlip)(flip | SDL_FLIP_HORIZONTAL);
    if (transforms & MAP_TILE_TRANSFORM_FLIP_Y)
        flip = (SDL_RendererFlip)(flip | SDL_FLIP_VERTICAL);
    if (flip == SDL_FLIP_NONE) {
        SDL_RenderCopy(app->renderer, tileset->texture, &src, &dst_part);
    } else {
        SDL_RenderCopyEx(app->renderer, tileset->texture, &src, &dst_part, 0.0, NULL, flip);
    }
}

void R_DrawLevel(app_t *app, const level_t *map, const tileset_t *tileset) {
    int cell_w = app_cell_w(app);
    int cell_h = app_cell_h(app);
    int tile_w = app_tile_w(app, tileset);
    int tile_h = app_tile_h(app, tileset);
    int draw_y_offset = tileset->draw_y_offset;
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            float sx, sy;
            R_MapToScreen(app, map, (float)x, (float)y, &sx, &sy);
            if ((map->render_capabilities & MAP_RENDER_CAP_CELL_COLORS) && map->cell_colors) {
                if (sx < -cell_w || sy < -cell_h ||
                    sx > app->win.w + cell_w || sy > app->win.h + cell_h) {
                    continue;
                }
                uint32_t color = map->cell_colors[L_Index(map, x, y)];
                SDL_SetRenderDrawColor(app->renderer,
                                       (uint8_t)(color >> 16),
                                       (uint8_t)(color >> 8),
                                       (uint8_t)color,
                                       255);
                irect_t dst = { (int)sx, (int)sy, cell_w, cell_h };
                SDL_RenderFillRect(app->renderer, &dst);
                continue;
            }
            if (sx < -tile_w || sy < -tile_h ||
                sx > app->win.w + tile_w || sy > app->win.h + tile_h) {
                continue;
            }
            int idx = L_Index(map, x, y);
            int tile = map->tile_ids[idx];
            if ((map->render_capabilities & MAP_RENDER_CAP_ZERO_TILE_EMPTY) && tile == 0) continue;
            irect_t src = { 0, 0, tileset->tile_w, tileset->tile_h };
            irect_t dst = {
                (int)sx,
                (int)(sy + draw_y_offset),
                tile_w,
                tile_h,
            };
            uint8_t base_flip =
                (map->render_capabilities & MAP_RENDER_CAP_TILE_TRANSFORMS) &&
                map->tile_transforms[0] ? map->tile_transforms[0][idx] : 0;
            render_tile_at_flipped(app, tileset, tile, src, dst, base_flip);
        }
    }

    for (int layer = 0;
         !(map->render_capabilities & MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS) &&
         layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS;
         ++layer) {
        if (!map->tile_overlays[layer]) continue;
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                R_MapToScreen(app, map, (float)x, (float)y, &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win.w + tile_w || sy > app->win.h + tile_h) {
                    continue;
                }
                int idx = L_Index(map, x, y);
                int overlay = map->tile_overlays[layer][idx];
                if (overlay <= 0) continue;
                irect_t src = { 0, 0, tileset->tile_w, tileset->tile_h };
                irect_t dst = {
                    (int)sx,
                    (int)(sy + draw_y_offset),
                    tile_w,
                    tile_h,
                };
                uint8_t overlay_flip =
                    (map->render_capabilities & MAP_RENDER_CAP_TILE_TRANSFORMS) &&
                    map->tile_transforms[layer + 1] ?
                    map->tile_transforms[layer + 1][idx] : 0;
                render_tile_at_flipped(app, tileset, overlay, src, dst, overlay_flip);
            }
        }
    }

    if ((map->render_capabilities & MAP_RENDER_CAP_TERRAIN_TRANSITIONS) &&
        !(map->render_capabilities & MAP_RENDER_CAP_CELL_COLORS) &&
        map->render_transitions) {
        SDL_SetTextureBlendMode(tileset->texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tileset->texture, 255);
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                R_MapToScreen(app, map, (float)x, (float)y, &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win.w + tile_w || sy > app->win.h + tile_h) {
                    continue;
                }
                int dx = (int)sx;
                int dy = (int)(sy + draw_y_offset);
                map->render_transitions(app, map, tileset, x, y, dx, dy);
            }
        }
        SDL_SetTextureAlphaMod(tileset->texture, 255);
        SDL_SetTextureBlendMode(tileset->texture, SDL_BLENDMODE_NONE);
    }

    if (app->show_blocked) {
        render_blocked_overlay(app, map);
    }

    if (app->show_grid) {
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                R_MapToScreen(app, map, (float)x, (float)y, &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win.w + tile_w || sy > app->win.h + tile_h) {
                    continue;
                }
                render_map_grid_cell(app, map, x, y, (SDL_Color){ 50, 78, 72, 80 });
            }
        }
    }
}

const spritesheet_t *R_CacheLookup(const spritecache_t *cache, const char *name) {
    if (!name || name[0] == '\0') return NULL;
    for (int i = 0; i < cache->count; ++i) {
        if (strcasecmp(cache->entries[i].name, name) == 0) return &cache->entries[i].sprite;
    }
    return NULL;
}

cachedsprite_t *R_CacheFind(spritecache_t *cache, const char *name) {
    if (!name || name[0] == '\0') return NULL;
    for (int i = 0; i < cache->count; ++i) {
        if (strcasecmp(cache->entries[i].name, name) == 0) return &cache->entries[i];
    }
    return NULL;
}

static const spritesequence_t *sprite_sequence_find(const spritesheet_t *sprite, const char *name) {
    if (!sprite || !name) return NULL;
    for (int i = 0; i < sprite->sequence_count; ++i) {
        if (strcmp(sprite->sequences[i].name, name) == 0) return &sprite->sequences[i];
    }
    return NULL;
}

static int sequence_facing_index(const spritesequence_t *seq, int direction_code) {
    if (!seq || seq->facings <= 0) return 0;
    int best = 0;
    int best_delta = 1000;
    for (int i = 0; i < seq->facings && i < MAX_SEQUENCE_FACINGS; ++i) {
        int code = seq->direction_codes[i];
        int delta = abs(code - direction_code);
        if (delta > 8) delta = 16 - delta;
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static int sprite_sequence_frame(const spritesheet_t *sprite, const char *sequence_name,
                                 int facing_code, int sequence_frame) {
    const spritesequence_t *seq = sprite_sequence_find(sprite, sequence_name);
    if (!seq || seq->facings <= 0 || seq->length <= 0) {
        debug_effects_log("sequence miss sprite_frames=%d sequence=%s facing=%d anim=%d",
                          sprite ? sprite->frame_count : 0,
                          sequence_name ? sequence_name : "(null)",
                          facing_code, sequence_frame);
        return -1;
    }
    int facing = sequence_facing_index(seq, facing_code);
    int anim = sequence_frame < 0 ? seq->length - 1 : sequence_frame;
    if (anim >= seq->length) anim = seq->length - 1;
    int frame_stride = seq->frame_stride > 0 ? seq->frame_stride : 1;
    int start = seq->frame_starts[facing];
    if (start < 0 || start >= sprite->frame_count) {
        debug_effects_log("sequence bad start sequence=%s facing=%d/%d code=%d start=%d frame_count=%d",
                          sequence_name, facing, seq->facings, facing_code, start,
                          sprite ? sprite->frame_count : 0);
        return -1;
    }
    int frame = start + anim * frame_stride;
    if (frame < 0) return start;
    if (frame >= sprite->frame_count) return sprite->frame_count - 1;
    return frame;
}


static int decoration_sprite_frame(app_t *app, const mapdecoration_t *dec, const spritesheet_t *sprite,
                                   int frame_index, const char *sequence_name) {
    int frame = -1;
    if (sequence_name && sequence_name[0] != '\0') {
        frame = sprite_sequence_frame(sprite, sequence_name, dec->facing_code, frame_index);
    }
    if (frame >= 0) return frame;
    if (frame_index >= 0 && frame_index < sprite->frame_count) return frame_index;
    if (frame_index < 0) {
        return (int)((app->ticks_ms / 250u) % (uint32_t)sprite->frame_count);
    }
    return 0;
}

static uint8_t fin_intensity_color_mod(int intensity) {
    if (intensity <= 0) intensity = 16;
    return (uint8_t)clamp255((intensity * 255 + 8) / 16);
}

static SDL_Texture *sprite_texture_for_remap(const spritesheet_t *sprite, int render_remap) {
    if (!sprite) return NULL;
    if (render_remap > 0 && render_remap < 8 && sprite->remap_textures[render_remap])
        return sprite->remap_textures[render_remap];
    return sprite->texture;
}

static SDL_Texture *begin_sprite_command(const spritesheet_t *sprite, uint32_t render_flags,
                                         int render_remap, int render_intensity) {
    SDL_Texture *texture = sprite_texture_for_remap(sprite, render_remap);
    if (!texture) return NULL;
    if ((render_flags & RTS_FRAME_ADDITIVE) != 0)
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);

    uint8_t intensity = fin_intensity_color_mod(render_intensity);
    uint8_t r = intensity;
    uint8_t g = intensity;
    uint8_t b = intensity;
    uint8_t a = 255;
    if ((render_flags & RTS_FRAME_TINT_YELLOW) != 0) {
        r = intensity;
        g = (uint8_t)((intensity * 236 + 127) / 255);
        b = (uint8_t)((intensity * 72 + 127) / 255);
        a = 230;
    }
    SDL_SetTextureColorMod(texture, r, g, b);
    SDL_SetTextureAlphaMod(texture, a);
    return texture;
}

static void end_sprite_command(SDL_Texture *texture, uint32_t render_flags) {
    if (!texture) return;
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
    if ((render_flags & RTS_FRAME_ADDITIVE) != 0)
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
}

static SDL_Point sprite_frame_raw_displacement(const spritesheet_t *sprite, int frame);
static SDL_Point sprite_ground_point(const spritesheet_t *sprite, int frame);

static void render_decoration_sprite(app_t *app, const level_t *map,
                                     const mapdecoration_t *dec, const spritesheet_t *sprite,
                                     int frame_index, uint32_t render_flags,
                                     const char *sequence_name, int anchor_frame_index,
                                     const char *anchor_sequence_name) {
    if (!sprite || !sprite->texture || sprite->frame_count <= 0) return;

    float sx, sy;
    R_MapToScreen(app, map, (float)dec->gx, (float)dec->gy, &sx, &sy);
    int footprint_w = dec->footprint_w > 0 ? dec->footprint_w : 1;
    int footprint_h = dec->footprint_h > 0 ? dec->footprint_h : 1;

    int frame = decoration_sprite_frame(app, dec, sprite, frame_index, sequence_name);
    int anchor_frame = decoration_sprite_frame(app, dec, sprite, anchor_frame_index,
                                               anchor_sequence_name);
    irect_t frame_rect = sprite_frame_rect(sprite, frame);
    int sprite_w = frame_rect.w;
    int sprite_h = frame_rect.h;

    irect_t dst;
    if (dec->has_sprite_pivot) {
        /* The plugin-authored point is in the full frame canvas, not in the
           visible-pixel bounds.  All layers of a composite therefore attach
           to exactly the same world point. */
        if (dec->center_anchor) {
            R_MapToScreen(app, map,
                               (float)dec->gx + (float)footprint_w * 0.5f,
                               (float)dec->gy + (float)footprint_h * 0.5f,
                               &sx, &sy);
        }
        dst = (irect_t){
            (int)lroundf(sx) - dec->sprite_pivot_x,
            (int)lroundf(sy) - dec->sprite_pivot_y,
            sprite_w,
            sprite_h,
        };
    } else if (dec->center_anchor) {
        R_MapToScreen(app, map,
                           (float)dec->gx + (float)footprint_w * 0.5f,
                           (float)dec->gy + (float)footprint_h * 0.5f,
                           &sx, &sy);
        SDL_Point ground;
        if (sprite->frame_ground_points) {
            ground = sprite_ground_point(sprite, anchor_frame);
        } else {
            /* Preserve the legacy decoration anchor for formats without an
               authored ground point (notably Dark Colony FIN sprites). */
            irect_t anchor_rect = sprite_frame_rect(sprite, anchor_frame);
            irect_t bounds = sprite_visible_bounds(sprite, anchor_frame);
            ground = (SDL_Point){ anchor_rect.w / 2, bounds.y + bounds.h };
        }
        dst = (irect_t){
            (int)(sx - ground.x),
            (int)(sy - ground.y),
            sprite_w,
            sprite_h,
        };
        if (frame != anchor_frame) {
            SDL_Point anchor_dis = sprite_frame_raw_displacement(sprite, anchor_frame);
            SDL_Point frame_dis = sprite_frame_raw_displacement(sprite, frame);
            dst.x += frame_dis.x - anchor_dis.x;
            dst.y += frame_dis.y - anchor_dis.y;
        }
    } else {
        dst = (irect_t){
            (int)(sx + (float)(footprint_w * app_cell_w(app) - sprite_w) * 0.5f),
            (int)(sy + (float)(footprint_h * app_cell_h(app) - sprite_h)),
            sprite_w,
            sprite_h,
        };
    }
    if (dst.x > app->win.w || dst.y > app->win.h ||
        dst.x + dst.w < 0 || dst.y + dst.h < 0) {
        return;
    }
    if ((render_flags & RTS_FRAME_BLINK) != 0 && ((app->ticks_ms / 250u) % 2u) == 0u) {
        return;
    }
    SDL_RendererFlip flip = (render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_Texture *texture = begin_sprite_command(sprite, render_flags, 0, 16);
    SDL_RenderCopyEx(app->renderer, texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
    end_sprite_command(texture, render_flags);
}

static void render_decoration(app_t *app, const level_t *map,
                              const mapdecoration_t *dec, const spritecache_t *cache) {
    render_decoration_sprite(app, map, dec, R_CacheLookup(cache, dec->shadow_name),
                             dec->frame_index, dec->render_flags, dec->sequence_name,
                             dec->frame_index, dec->sequence_name);
    render_decoration_sprite(app, map, dec, R_CacheLookup(cache, dec->sprite_name),
                             dec->frame_index, dec->render_flags, dec->sequence_name,
                             dec->frame_index, dec->sequence_name);
    render_decoration_sprite(app, map, dec, R_CacheLookup(cache, dec->sprite2_name),
                             dec->frame2_index, dec->render2_flags, NULL,
                             dec->frame_index, dec->sequence_name);
    render_decoration_sprite(app, map, dec, R_CacheLookup(cache, dec->sprite3_name),
                             dec->frame3_index, dec->render3_flags, NULL,
                             dec->frame_index, dec->sequence_name);
}

void R_DrawDecorations(app_t *app, const level_t *map, const spritecache_t *cache) {
    for (int i = 0; i < map->decoration_count; ++i) {
        render_decoration(app, map, &map->decorations[i], cache);
    }
}

static float unit_pick_radius_px(const app_t *app, const mobj_t *unit) {
    float cell = ((float)app_cell_w(app) + (float)app_cell_h(app)) * 0.5f;
    float radius = P_MobjRadius(unit) * cell;
    float min_radius = 12.0f;
    return radius < min_radius ? min_radius : radius;
}

static bool circle_intersects_rect(fvec2_t center, float radius, irect_t r) {
    float nearest_x = center.x;
    float nearest_y = center.y;
    if (nearest_x < (float)r.x) nearest_x = (float)r.x;
    if (nearest_x > (float)(r.x + r.w)) nearest_x = (float)(r.x + r.w);
    if (nearest_y < (float)r.y) nearest_y = (float)r.y;
    if (nearest_y > (float)(r.y + r.h)) nearest_y = (float)(r.y + r.h);
    float dx = center.x - nearest_x;
    float dy = center.y - nearest_y;
    return dx * dx + dy * dy <= radius * radius;
}

static int sprite_frame_for_unit(const spritesheet_t *sprite, const mobj_t *unit, uint32_t ticks) {
    bool moving = unit->movement.path_index > 0 && unit->movement.path_index < unit->movement.path_len;
    bool attacking = unit->attack.anim_left_ms > 0;
    bool dead = unit->hp <= 0;
    const spritesequence_t *seq = NULL;
    bool using_death_sequence = false;
    if (dead) {
        seq = sprite_sequence_find(sprite, "die");
        if (seq) using_death_sequence = true;
        if (!seq) {
            seq = sprite_sequence_find(sprite, "death");
            if (seq) using_death_sequence = true;
        }
        if (!seq) seq = sprite_sequence_find(sprite, "shoot");
    } else {
        seq = sprite_sequence_find(sprite, attacking ? "shoot" : (moving ? "run" : "stand"));
        if (!seq && attacking) seq = sprite_sequence_find(sprite, "attack");
        if (!seq && moving) seq = sprite_sequence_find(sprite, "walk");
    }
    if (!seq) seq = sprite_sequence_find(sprite, "idle");
    if (!seq && dead) seq = sprite_sequence_find(sprite, "stand");
    if (seq && seq->facings > 0 && seq->length > 0) {
        int direction_code = unit->core.facing_code;
        int facing = sequence_facing_index(seq, direction_code);
        int tick_ms = seq->tick_ms > 0 ? seq->tick_ms : 120;
        int anim = 0;
        if (dead && using_death_sequence && unit->death_started && seq->length > 1) {
            int elapsed_ms = unit->death.anim_ms - unit->death.anim_left_ms;
            if (elapsed_ms < 0) elapsed_ms = 0;
            anim = elapsed_ms / tick_ms;
            if (anim >= seq->length) anim = seq->length - 1;
        } else if (dead && seq->length > 1) {
            anim = seq->length - 1;
        } else if (attacking && seq->length > 1) {
            int elapsed_ms = unit->attack.anim_ms - unit->attack.anim_left_ms;
            if (elapsed_ms < 0) elapsed_ms = 0;
            anim = elapsed_ms / tick_ms;
            if (anim >= seq->length) anim = seq->length - 1;
        } else if (moving && seq->length > 1) {
            anim = (int)((ticks / (uint32_t)tick_ms) % (uint32_t)seq->length);
        }
        int frame_stride = seq->frame_stride > 0 ? seq->frame_stride : 1;
        int frame = seq->frame_starts[facing] + anim * frame_stride;
        if (frame >= 0 && frame < sprite->frame_count) return frame;
    }

    int anim_frames = sprite->primary_frames_per_rotation > 0 ?
        sprite->primary_frames_per_rotation : sprite->frame_count;
    if (moving && anim_frames > 0 && sprite->sequence_count == 0) {
        return (int)((ticks / 120) % (uint32_t)anim_frames);
    }
    return 0;
}

static irect_t sprite_visible_bounds(const spritesheet_t *sprite, int frame) {
    if (sprite && sprite->frame_bounds && frame >= 0 && frame < sprite->frame_count) {
        irect_t r = sprite->frame_bounds[frame];
        if (r.w > 0 && r.h > 0) return r;
    }
    return (irect_t){ 0, 0, sprite ? sprite->frame_w : 1, sprite ? sprite->frame_h : 1 };
}

static irect_t sprite_frame_rect(const spritesheet_t *sprite, int frame) {
    if (sprite && sprite->frames && frame >= 0 && frame < sprite->frame_count &&
        sprite->frames[frame].w > 0 && sprite->frames[frame].h > 0) {
        return sprite->frames[frame];
    }
    return (irect_t){ 0, 0, sprite ? sprite->frame_w : 1, sprite ? sprite->frame_h : 1 };
}

static SDL_Point sprite_frame_raw_displacement(const spritesheet_t *sprite, int frame) {
    if (sprite && sprite->frame_displacements && frame >= 0 && frame < sprite->frame_count) {
        return sprite->frame_displacements[frame];
    }
    return (SDL_Point){ 0, 0 };
}

static SDL_Point sprite_frame_displacement(const spritesheet_t *sprite, int frame,
                                           uint32_t render_flags) {
    SDL_Point p = { 0, 0 };
    if (sprite && sprite->frame_displacements && frame >= 0 && frame < sprite->frame_count) {
        p = sprite->frame_displacements[frame];
    }
    if ((render_flags & RTS_FRAME_FLIP_X) != 0) p.x = 0;
    return p;
}

static SDL_Point sprite_ground_point(const spritesheet_t *sprite, int frame) {
    if (sprite && sprite->frame_ground_points && frame >= 0 && frame < sprite->frame_count) {
        return sprite->frame_ground_points[frame];
    }
    irect_t bounds = sprite_visible_bounds(sprite, frame);
    return (SDL_Point){ bounds.x + bounds.w / 2, bounds.y + bounds.h };
}

static const spritesheet_t *unit_sprite_sheet_for_view(const mobj_t *unit,
                                                     const spritesheet_t *fallback_sprite,
                                                     const spritecache_t *cache,
                                                     const gameinfo_t *game_info) {
    if (!unit) return NULL;
    const char *sprite_name = unit->core.sprite_name;
    if (game_info && unit->core.sprite_id >= 0 && unit->core.sprite_id < game_info->sprite_count &&
        game_info->sprnames && game_info->sprnames[unit->core.sprite_id]) {
        sprite_name = game_info->sprnames[unit->core.sprite_id];
    }
    const spritesheet_t *sprite = R_CacheLookup(cache, sprite_name);
    return sprite ? sprite : fallback_sprite;
}

static int unit_frame_for_view(const spritesheet_t *sprite, const mobj_t *unit,
                               const gameinfo_t *game_info, uint32_t ticks) {
    /* FIN/state-driven games author the resolved frame on the unit.  Dark Reign
       has a gameinfo_t for shared simulation metadata, but its RSPR facings are
       selected by the renderer from the unit heading. */
    bool has_state_frames = game_info && game_info->states && game_info->state_count > 0;
    int frame = has_state_frames ? unit->core.frame : sprite_frame_for_unit(sprite, unit, ticks);
    if (!sprite || sprite->frame_count <= 0) return 0;
    if (frame >= sprite->frame_count) frame = 0;
    if (frame < 0) frame = 0;
    return frame;
}

static int direction_slot_for_view(int facings, const int *direction_codes, int facing_code) {
    if (!direction_codes || facings <= 0) return -1;
    int wrap = 16;
    for (int i = 0; i < facings && i < RTS_MAX_STATE_FACINGS; ++i) {
        if (direction_codes[i] > 7 || facing_code > 7) {
            wrap = 16;
            break;
        }
        wrap = 8;
    }
    int best = 0;
    int best_delta = 1000;
    for (int i = 0; i < facings && i < RTS_MAX_STATE_FACINGS; ++i) {
        int code = direction_codes[i];
        int delta = abs(code - facing_code);
        if (delta > wrap / 2) delta = wrap - delta;
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static void unit_state_body_offset_for_view(const gameinfo_t *game_info, const mobj_t *unit,
                                            int *offset_x, int *offset_y) {
    int ox = 0;
    int oy = 0;
    if (game_info && unit && game_info->states &&
        unit->core.state_id >= 0 && unit->core.state_id < game_info->state_count) {
        const state_t *state = &game_info->states[unit->core.state_id];
        if (state->facings > 0) {
            int slot = direction_slot_for_view(state->facings, state->direction_codes,
                                               unit->core.facing_code);
            if (slot < 0) slot = 0;
            if (slot < RTS_MAX_STATE_FACINGS) {
                ox = state->offset_x[slot];
                oy = state->offset_y[slot];
            }
        }
    }
    if (offset_x) *offset_x = ox;
    if (offset_y) *offset_y = oy;
}

static bool unit_screen_rect_for_view(const app_t *app, const level_t *map, const mobj_t *unit,
                                      const spritesheet_t *fallback_sprite,
                                      const spritecache_t *cache,
                                      const gameinfo_t *game_info, uint32_t ticks,
                                      irect_t *dst_out, irect_t *visible_out,
                                      float *sx_out, float *sy_out,
                                      int *frame_out, const spritesheet_t **sprite_out) {
    if (!app || !unit) return false;
    float sx = 0.0f, sy = 0.0f;
    R_MapToScreen(app, map, unit->core.gx, unit->core.gy, &sx, &sy);
    const spritesheet_t *sprite = unit_sprite_sheet_for_view(unit, fallback_sprite, cache, game_info);
    if (!sprite || !sprite->texture || sprite->frame_count <= 0) {
        float radius = unit_pick_radius_px(app, unit);
        irect_t fallback = {
            (int)floorf(sx - radius),
            (int)floorf(sy - radius),
            (int)ceilf(radius * 2.0f),
            (int)ceilf(radius * 2.0f),
        };
        if (dst_out) *dst_out = fallback;
        if (visible_out) *visible_out = fallback;
        if (sx_out) *sx_out = sx;
        if (sy_out) *sy_out = sy;
        if (frame_out) *frame_out = 0;
        if (sprite_out) *sprite_out = sprite;
        return false;
    }

    int frame = unit_frame_for_view(sprite, unit, game_info, ticks);
    irect_t frame_rect = sprite_frame_rect(sprite, frame);
    irect_t bounds = sprite_visible_bounds(sprite, frame);
    uint32_t render_flags = game_info ? unit->core.render_flags : 0;
    if ((render_flags & RTS_FRAME_FLIP_X) != 0) {
        bounds.x = frame_rect.w - bounds.x - bounds.w;
    }
    int sprite_w = frame_rect.w;
    int sprite_h = frame_rect.h;
    int body_offset_x = 0;
    int body_offset_y = 0;
    unit_state_body_offset_for_view(game_info, unit, &body_offset_x, &body_offset_y);
    irect_t dst;
    if (game_info && game_info->state_coord_mode == RTS_STATE_COORDS_FIN_TOP_LEFT) {
        SDL_Point dis = sprite_frame_displacement(sprite, frame, render_flags);
        dst = (irect_t){
            (int)lroundf(sx) + body_offset_x + dis.x,
            (int)lroundf(sy) + body_offset_y + dis.y,
            sprite_w,
            sprite_h,
        };
    } else {
        SDL_Point ground = sprite_ground_point(sprite, frame);
        if ((render_flags & RTS_FRAME_FLIP_X) != 0) {
            ground.x = frame_rect.w - ground.x;
        }
        dst = (irect_t){
            (int)lroundf(sx - (float)ground.x) + body_offset_x,
            (int)lroundf(sy - (float)ground.y) + body_offset_y,
            sprite_w,
            sprite_h,
        };
    }
    dst.x += unit->core.render_offset_x;
    dst.y += unit->core.render_offset_y;
    irect_t visible = {
        dst.x + bounds.x,
        dst.y + bounds.y,
        bounds.w,
        bounds.h,
    };
    if (dst_out) *dst_out = dst;
    if (visible_out) *visible_out = visible;
    if (sx_out) *sx_out = sx;
    if (sy_out) *sy_out = sy;
    if (frame_out) *frame_out = frame;
    if (sprite_out) *sprite_out = sprite;
    return true;
}

static int pick_unit_at(const app_t *app, const level_t *map, const mobj_t *units, int unit_count,
                        const spritesheet_t *fallback_sprite, const spritecache_t *cache,
                        const gameinfo_t *game_info, int x, int y, int owner_filter) {
    int best = -1;
    float best_score = 1000000000.0f;
    for (int i = unit_count - 1; i >= 0; --i) {
        const mobj_t *unit = &units[i];
        if (unit->hp <= 0 || (unit->traits & MF_SELECTABLE) == 0) continue;
        if (owner_filter >= 0 && unit->owner != owner_filter) continue;
        irect_t visible;
        float sx = 0.0f, sy = 0.0f;
        unit_screen_rect_for_view(app, map, unit, fallback_sprite, cache, game_info, app->ticks_ms,
                                  NULL, &visible, &sx, &sy, NULL, NULL);
        if (!irect_contains(visible, (ivec2_t){ x, y })) continue;
        float dx = (float)x - sx;
        float dy = (float)y - sy;
        float score = dx * dx + dy * dy;
        if (score >= best_score) continue;
        best_score = score;
        best = i;
    }
    return best;
}

static int selection_health_bucket(const mobj_t *u) {
    if (!u || u->max_hp <= 0) return 0;
    if (u->hp * 3 <= u->max_hp) return 2;
    if (u->hp * 3 <= u->max_hp * 2) return 1;
    return 0;
}

static SDL_Color selection_health_tint(int bucket) {
    switch (bucket) {
    case 2: return (SDL_Color){ 255, 76, 54, 255 };
    case 1: return (SDL_Color){ 255, 218, 62, 255 };
    default: return (SDL_Color){ 83, 245, 92, 255 };
    }
}

static void draw_ellipse_outline(SDL_Renderer *renderer, int cx, int cy, int rx, int ry) {
    if (rx <= 0 || ry <= 0) return;
    int steps = (rx + ry) * 2;
    if (steps < 16) steps = 16;
    for (int i = 0; i < steps; ++i) {
        float a = (float)i / (float)steps * 6.28318530f;
        int x = (int)(rx * cosf(a) + 0.5f);
        int y = (int)(ry * sinf(a) + 0.5f);
        SDL_RenderDrawPoint(renderer, cx + x, cy + y);
    }
}

static void draw_selection_circle(app_t *app, const mobj_t *u, int cx, int cy, int radius) {
    if (!app || !app->renderer || radius < 2) return;
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_NONE);
    int rx = radius;
    int ry = radius / 2;
    if (ry < 2) ry = 2;
    SDL_Color tint = selection_health_tint(selection_health_bucket(u));
    SDL_SetRenderDrawColor(app->renderer, 8, 10, 8, 255);
    draw_ellipse_outline(app->renderer, cx, cy + 1, rx + 1, ry);
    draw_ellipse_outline(app->renderer, cx, cy + 1, rx,     ry);
    SDL_SetRenderDrawColor(app->renderer, tint.r, tint.g, tint.b, 255);
    draw_ellipse_outline(app->renderer, cx, cy, rx + 1, ry);
    draw_ellipse_outline(app->renderer, cx, cy, rx,     ry);
}

static void draw_selection_brackets(app_t *app, const mobj_t *u, const irect_t *visible) {
    if (!app || !app->renderer || !u || !visible || visible->w <= 0 || visible->h <= 0) return;
    irect_t box = {
        visible->x - 3,
        visible->y - 3,
        visible->w + 6,
        visible->h + 6,
    };
    int corner = box.w < box.h ? box.w / 4 : box.h / 4;
    if (corner < 4) corner = 4;
    if (corner > 9) corner = 9;
    int right = box.x + box.w;
    int bottom = box.y + box.h;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 236, 236, 220, 230);
    SDL_RenderDrawLine(app->renderer, box.x, box.y, box.x + corner, box.y);
    SDL_RenderDrawLine(app->renderer, box.x, box.y, box.x, box.y + corner);
    SDL_RenderDrawLine(app->renderer, right - corner, box.y, right, box.y);
    SDL_RenderDrawLine(app->renderer, right, box.y, right, box.y + corner);
    SDL_RenderDrawLine(app->renderer, box.x, bottom - corner, box.x, bottom);
    SDL_RenderDrawLine(app->renderer, box.x, bottom, box.x + corner, bottom);
    SDL_RenderDrawLine(app->renderer, right, bottom - corner, right, bottom);
    SDL_RenderDrawLine(app->renderer, right - corner, bottom, right, bottom);

    int bar_w = box.w - 8;
    if (bar_w < 16) bar_w = 16;
    int bar_x = box.x + (box.w - bar_w) / 2;
    int bar_y = box.y - 5;
    int fill_w = u->max_hp > 0 ? (bar_w * u->hp) / u->max_hp : bar_w;
    SDL_Color tint = selection_health_tint(selection_health_bucket(u));
    SDL_SetRenderDrawColor(app->renderer, 20, 20, 18, 235);
    SDL_RenderFillRect(app->renderer, &(irect_t){ bar_x - 1, bar_y - 1, bar_w + 2, 4 });
    SDL_SetRenderDrawColor(app->renderer, tint.r, tint.g, tint.b, 255);
    SDL_RenderFillRect(app->renderer, &(irect_t){ bar_x, bar_y, fill_w, 2 });
}

static void draw_selection_triangle(app_t *app, const mobj_t *u, const irect_t *visible) {
    if (!app || !app->renderer || !visible || visible->w <= 0 || visible->h <= 0) return;
    int cx = visible->x + visible->w / 2;
    int top_y = visible->y - 11;
    int tip_y = top_y + 7;
    int half_w = 8;
    SDL_Color tint = selection_health_tint(selection_health_bucket(u));

    SDL_SetRenderDrawColor(app->renderer, 8, 10, 8, 235);
    SDL_RenderDrawLine(app->renderer, cx - half_w - 1, top_y - 1, cx + half_w + 1, top_y - 1);
    SDL_RenderDrawLine(app->renderer, cx - half_w - 1, top_y - 1, cx, tip_y + 1);
    SDL_RenderDrawLine(app->renderer, cx + half_w + 1, top_y - 1, cx, tip_y + 1);

    SDL_SetRenderDrawColor(app->renderer, tint.r, tint.g, tint.b, tint.a);
    for (int y = top_y; y <= tip_y; ++y) {
        float t = (float)(y - top_y) / (float)(tip_y - top_y);
        int span = (int)lroundf((float)half_w * (1.0f - t));
        SDL_RenderDrawLine(app->renderer, cx - span, y, cx + span, y);
    }
}

static bool draw_selection_marker_sprite(app_t *app, const mobj_t *u, const spritecache_t *cache,
                                         const gameinfo_t *game_info, const irect_t *visible) {
    if (!app || !app->renderer || !u || !cache || !game_info || !visible ||
        !game_info->sprnames) {
        return false;
    }
    const selectionmarker_t *info = &game_info->selection_marker;
    if (info->sprite < 0 || info->sprite >= game_info->sprite_count) return false;
    const char *sprite_name = game_info->sprnames[info->sprite];
    const spritesheet_t *marker = R_CacheLookup(cache, sprite_name);
    if (!marker || !marker->texture || marker->frame_count <= 0) return false;

    int bucket = selection_health_bucket(u);
    int frame = bucket == 2 ? info->critical_frame :
                bucket == 1 ? info->wounded_frame : info->healthy_frame;
    if (frame < 0 || frame >= marker->frame_count) return false;
    irect_t frame_rect = sprite_frame_rect(marker, frame);
    if (frame_rect.w <= 0 || frame_rect.h <= 0) return false;

    int cx = visible->x + visible->w / 2;
    irect_t dst = {
        cx - frame_rect.w / 2,
        visible->y - frame_rect.h + info->top_offset_y,
        frame_rect.w,
        frame_rect.h,
    };
    SDL_Texture *texture = begin_sprite_command(marker, 0, 0, 16);
    if (!texture) return false;
    SDL_RenderCopy(app->renderer, texture, &marker->frames[frame], &dst);
    end_sprite_command(texture, 0);
    return true;
}

static void render_unit_state_overlay(app_t *app, const mobj_t *u, const spritesheet_t *body_sprite,
                                      int body_frame, const spritecache_t *cache,
                                      const gameinfo_t *game_info, const irect_t *body_dst,
                                      float origin_sx, float origin_sy) {
    (void)body_sprite;
    (void)body_frame;
    if (!app || !u || !cache || !game_info || !body_dst ||
        u->core.state_id < 0 || u->core.state_id >= game_info->state_count ||
        !game_info->states || !game_info->sprnames) {
        return;
    }
    const state_t *state = &game_info->states[u->core.state_id];
    if (state->overlay_facings <= 0 || state->overlay_sprite < 0 ||
        state->overlay_sprite >= game_info->sprite_count) {
        return;
    }
    int slot = direction_slot_for_view(state->overlay_facings,
                                       state->overlay_direction_codes,
                                       u->core.facing_code);
    if (slot < 0) slot = 0;
    if (slot >= RTS_MAX_STATE_FACINGS) return;
    int frame = state->overlay_facing_frames[slot];
    if (frame < 0) return;

    const char *sprite_name = game_info->sprnames[state->overlay_sprite];
    const spritesheet_t *overlay = R_CacheLookup(cache, sprite_name);
    if (!overlay || !overlay->texture || overlay->frame_count <= 0) return;
    if (frame >= overlay->frame_count) frame = 0;
    irect_t frame_rect = sprite_frame_rect(overlay, frame);

    uint32_t flags = state->overlay_facing_flags[slot];
    irect_t dst;
    if (game_info->state_coord_mode == RTS_STATE_COORDS_FIN_TOP_LEFT) {
        SDL_Point dis = sprite_frame_displacement(overlay, frame, flags);
        dst = (irect_t){
            (int)lroundf(origin_sx) + state->overlay_offset_x[slot] + dis.x,
            (int)lroundf(origin_sy) + state->overlay_offset_y[slot] + dis.y,
            frame_rect.w,
            frame_rect.h,
        };
    } else {
        dst = (irect_t){
            body_dst->x + state->overlay_offset_x[slot],
            body_dst->y + state->overlay_offset_y[slot],
            frame_rect.w,
            frame_rect.h,
        };
    }
    SDL_RendererFlip flip = (flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    int remap = state->overlay_remap[slot];
    int intensity = state->overlay_intensity[slot];
    SDL_Texture *texture = begin_sprite_command(overlay, flags, remap, intensity);
    SDL_RenderCopyEx(app->renderer, texture, &overlay->frames[frame],
                     &dst, 0.0, NULL, flip);
    end_sprite_command(texture, flags);
}

static void render_unit_sprite(app_t *app, const level_t *map,
                               const mobj_t *u, const spritesheet_t *fallback_sprite,
                               const spritecache_t *cache, const gameinfo_t *game_info,
                               uint32_t ticks) {
    if (!u || (u->traits & MF_RENDERABLE) == 0) return;
    const spritesheet_t *sprite = unit_sprite_sheet_for_view(u, fallback_sprite, cache, game_info);
    if (!sprite || !sprite->texture || sprite->frame_count <= 0) return;

    float sx = 0.0f, sy = 0.0f;
    int frame = 0;
    irect_t dst;
    irect_t visible;
    unit_screen_rect_for_view(app, map, u, fallback_sprite, cache, game_info, ticks,
                              &dst, &visible, &sx, &sy, &frame, &sprite);
    uint32_t render_flags = game_info ? u->core.render_flags : 0;
    const spritesheet_t *shadow = R_CacheLookup(cache, u->shadow_name);
    /* Some Dark Reign unit definitions repeat the body RSPR in
       SetShadowImage.  The original treats its shadow data specially; our
       cache resolves that name to the already-loaded colour body sheet, so
       drawing it here would create a second vehicle that mirrors every move. */
    if (shadow && shadow != sprite && shadow->texture && shadow->frame_count > 0) {
        int shadow_frame = frame < shadow->frame_count ? frame : 0;
        irect_t shadow_rect = sprite_frame_rect(shadow, shadow_frame);
        irect_t shadow_dst = { dst.x, dst.y, shadow_rect.w, shadow_rect.h };
        SDL_RenderCopy(app->renderer, shadow->texture, &shadow->frames[shadow_frame], &shadow_dst);
    }
    float content_y = (float)visible.y;
    SDL_RendererFlip flip = (render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_Texture *texture = begin_sprite_command(sprite, render_flags, u->core.render_remap,
                                                u->core.render_intensity);
    SDL_RenderCopyEx(app->renderer, texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
    end_sprite_command(texture, render_flags);
    render_unit_state_overlay(app, u, sprite, frame, cache, game_info, &dst, sx, sy);
    if (u->selected && (u->traits & MF_SELECTABLE) != 0) {
        SelectionStyle sel_style = game_info ? game_info->selection_marker.style
                                             : SELECTION_STYLE_SPRITE;
        if (sel_style == SELECTION_STYLE_CIRCLE) {
            int radius = (int)(unit_pick_radius_px(app, u) * 0.85f);
            draw_selection_circle(app, u, (int)sx, (int)sy, radius);
        }
        else if (sel_style == SELECTION_STYLE_BRACKETS)
            draw_selection_brackets(app, u, &visible);
        else if (!draw_selection_marker_sprite(app, u, cache, game_info, &visible))
            draw_selection_triangle(app, u, &visible);
    }
    if (u->max_hp > 0 && u->hp > 0 && u->hp < u->max_hp &&
        (!game_info || game_info->selection_marker.style != SELECTION_STYLE_BRACKETS ||
         !u->selected)) {
        int bar_w = dst.w / 2;
        int bar_h = 2;
        int bx = (int)(sx - bar_w / 2);
        int by = (int)content_y - bar_h - 4;
        irect_t back = { bx, by, bar_w, bar_h };
        irect_t fill = { bx, by, (bar_w * u->hp) / u->max_hp, bar_h };
        SDL_SetRenderDrawColor(app->renderer, 40, 20, 20, 220);
        SDL_RenderFillRect(app->renderer, &back);
        SDL_SetRenderDrawColor(app->renderer, 98, 224, 161, 230);
        SDL_RenderFillRect(app->renderer, &fill);
    }
}

void R_DrawThings(app_t *app, const mobj_t *units, int unit_count, const spritesheet_t *fallback_sprite,
                  const spritecache_t *cache, const gameinfo_t *game_info, uint32_t ticks) {
    for (int i = 0; i < unit_count; ++i) {
        render_unit_sprite(app, NULL, &units[i], fallback_sprite, cache, game_info, ticks);
    }
}

static int compare_draw_commands(const void *a, const void *b) {
    const drawcommand_t *ia = a;
    const drawcommand_t *ib = b;
    if (ia->sort_y < ib->sort_y) return -1;
    if (ia->sort_y > ib->sort_y) return 1;
    if (ia->layer != ib->layer) return (int)ia->layer - (int)ib->layer;
    if (ia->kind != ib->kind) return (int)ia->kind - (int)ib->kind;
    return ia->stable_index - ib->stable_index;
}

static void render_overlay_tile_item(app_t *app, const level_t *map, const tileset_t *tileset,
                                     int x, int y, int layer) {
    if (!app || !map || !tileset || layer < 0 || layer >= map->tile_overlay_count ||
        layer >= MAX_TILE_OVERLAYS || !map->tile_overlays[layer]) {
        return;
    }
    int idx = L_Index(map, x, y);
    int overlay = map->tile_overlays[layer][idx];
    if (overlay <= 0) return;

    int tile_w = app_tile_w(app, tileset);
    int tile_h = app_tile_h(app, tileset);
    int draw_y_offset = tileset->draw_y_offset;
    float sx, sy;
    R_MapToScreen(app, map, (float)x, (float)y, &sx, &sy);
    if (sx < -tile_w || sy < -tile_h ||
        sx > app->win.w + tile_w || sy > app->win.h + tile_h) {
        return;
    }
    irect_t src = { 0, 0, tileset->tile_w, tileset->tile_h };
    irect_t dst = {
        (int)sx,
        (int)(sy + draw_y_offset),
        tile_w,
        tile_h,
    };
    uint8_t overlay_flip =
        (map->render_capabilities & MAP_RENDER_CAP_TILE_TRANSFORMS) &&
        map->tile_transforms[layer + 1] ? map->tile_transforms[layer + 1][idx] : 0;
    render_tile_at_flipped(app, tileset, overlay, src, dst, overlay_flip);
}

void R_RenderPlayerView(app_t *app, const level_t *map, const tileset_t *tileset,
                          const mobj_t *units, int unit_count, const spritesheet_t *fallback_sprite,
                          const spritecache_t *cache, const gameinfo_t *game_info, uint32_t ticks) {
    if (!app || !map) return;
    int overlay_count = 0;
    if (map->render_capabilities & MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS) {
        for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
            if (!map->tile_overlays[layer]) continue;
            for (int y = 0; y < map->height; ++y) {
                for (int x = 0; x < map->width; ++x) {
                    if (map->tile_overlays[layer][L_Index(map, x, y)] > 0) overlay_count++;
                }
            }
        }
    }

    int decoration_count = map->decoration_count > 0 ? map->decoration_count : 0;
    int total = overlay_count + decoration_count + (unit_count > 0 ? unit_count : 0);
    if (total <= 0) return;
    drawcommand_t *commands = malloc((size_t)total * sizeof(*commands));
    if (!commands) {
        R_DrawDecorations(app, map, cache);
        for (int i = 0; i < unit_count; ++i) {
            render_unit_sprite(app, map, &units[i], fallback_sprite, cache, game_info, ticks);
        }
        return;
    }

    int count = 0;
    if (map->render_capabilities & MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS) {
        for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
            if (!map->tile_overlays[layer]) continue;
            for (int y = 0; y < map->height; ++y) {
                for (int x = 0; x < map->width; ++x) {
                    if (map->tile_overlays[layer][L_Index(map, x, y)] <= 0) continue;
                    commands[count++] = (drawcommand_t){
                        .kind = DRAW_COMMAND_TILE_OVERLAY,
                        .layer = RENDER_LAYER_TERRAIN_OVERLAY,
                        .sort_y = L_ScreenY(map, y) + 1.0f + (float)layer * 0.001f,
                        .stable_index = L_Index(map, x, y),
                        .ref.tile_overlay = { .x = x, .y = y, .layer = layer },
                    };
                }
            }
        }
    }
    for (int i = 0; i < map->decoration_count; ++i) {
        const mapdecoration_t *dec = &map->decorations[i];
        float sort_y = dec->has_sprite_pivot ?
            (float)dec->gy + (float)(dec->footprint_h > 0 ? dec->footprint_h : 1) :
            dec->center_anchor ?
            (float)dec->gy + 0.5f :
            (float)dec->gy + (float)(dec->footprint_h > 0 ? dec->footprint_h : 1);
        sort_y = L_ScreenYF(map, sort_y);
        commands[count++] = (drawcommand_t){
            .kind = DRAW_COMMAND_DECORATION,
            .layer = RENDER_LAYER_DECORATION,
            .sort_y = sort_y,
            .stable_index = i,
            .ref.decoration = dec,
        };
    }
    for (int i = 0; i < unit_count; ++i) {
        float sort_y = units[i].render_sort_y > 0.0f ?
            units[i].render_sort_y : units[i].core.gy;
        sort_y = L_ScreenYF(map, sort_y);
        commands[count++] = (drawcommand_t){
            .kind = DRAW_COMMAND_UNIT,
            .layer = RENDER_LAYER_UNIT,
            .sort_y = sort_y,
            .stable_index = i,
            .ref.unit = &units[i],
        };
    }

    qsort(commands, (size_t)count, sizeof(*commands), compare_draw_commands);
    for (int i = 0; i < count; ++i) {
        drawcommand_t *command = &commands[i];
        if (command->kind == DRAW_COMMAND_TILE_OVERLAY) {
            render_overlay_tile_item(app, map, tileset, command->ref.tile_overlay.x,
                                     command->ref.tile_overlay.y, command->ref.tile_overlay.layer);
        } else if (command->kind == DRAW_COMMAND_DECORATION) {
            render_decoration(app, map, command->ref.decoration, cache);
        } else {
            render_unit_sprite(app, map, command->ref.unit, fallback_sprite, cache, game_info, ticks);
        }
    }
    free(commands);
}

static int sprite_frame_for_effect(const spritesheet_t *sprite, const effect_t *effect) {
    if (!sprite || !effect) return 0;
    int frame_ms = effect->frame_ms > 0 ? effect->frame_ms : 90;
    int anim = effect->age_ms / frame_ms;
    if (effect->sequence_name[0] != '\0') {
        int frame = sprite_sequence_frame(sprite, effect->sequence_name,
                          effect->core.facing_code, anim);
        if (frame >= 0) return frame;
        if (strcmp(effect->sequence_name, "die") == 0) {
            frame = sprite_sequence_frame(sprite, "death", effect->core.facing_code, anim);
            if (frame >= 0) return frame;
        }
    }
    if (sprite->frame_count <= 0) return 0;
    if (anim >= sprite->frame_count) anim = sprite->frame_count - 1;
    return anim < 0 ? 0 : anim;
}

void R_DrawEffects(app_t *app, const level_t *map,
                           const effect_t *effects, int max_effects,
                           const spritecache_t *cache, const gameinfo_t *game_info) {
    if (!effects || max_effects <= 0) return;
    for (int i = 0; i < max_effects; ++i) {
        const effect_t *effect = &effects[i];
        if (!effect->active) continue;
        const char *sprite_name = effect->core.sprite_name;
        if (effect->use_state && game_info && effect->core.sprite_id >= 0 &&
            effect->core.sprite_id < game_info->sprite_count &&
            game_info->sprnames && game_info->sprnames[effect->core.sprite_id]) {
            sprite_name = game_info->sprnames[effect->core.sprite_id];
        }
        const spritesheet_t *sprite = R_CacheLookup(cache, sprite_name);
        if (!sprite || !sprite->texture || sprite->frame_count <= 0) {
            debug_effects_log("render skip slot=%d sprite=%s sequence=%s reason=missing-cache",
                              i, effect->core.sprite_name,
                              effect->sequence_name[0] ? effect->sequence_name : "(none)");
            continue;
        }

        float sx, sy;
        R_MapToScreen(app, map, effect->core.gx, effect->core.gy, &sx, &sy);
        int frame = effect->use_state ? effect->core.frame : sprite_frame_for_effect(sprite, effect);
        if (frame < 0 || frame >= sprite->frame_count) frame = 0;
        irect_t frame_rect = sprite_frame_rect(sprite, frame);
        int sprite_w = frame_rect.w;
        int sprite_h = frame_rect.h;
        irect_t dst;
        if (effect->use_state && game_info &&
            game_info->state_coord_mode == RTS_STATE_COORDS_FIN_TOP_LEFT) {
            SDL_Point dis = sprite_frame_displacement(sprite, frame, effect->core.render_flags);
            dst = (irect_t){
                (int)lroundf(sx) + effect->core.render_offset_x + dis.x,
                (int)lroundf(sy) + effect->core.render_offset_y + dis.y - sprite_h,
                sprite_w,
                sprite_h,
            };
        } else {
            dst = (irect_t){
                (int)(sx - sprite_w / 2),
                (int)(sy - sprite_h / 2),
                sprite_w,
                sprite_h,
            };
            if (effect->core.render_offset_x != 0 || effect->core.render_offset_y != 0) {
                dst.x += effect->core.render_offset_x;
                dst.y += effect->core.render_offset_y;
            }
        }
        if (dst.x > app->win.w || dst.y > app->win.h ||
            dst.x + dst.w < 0 || dst.y + dst.h < 0) {
            debug_effects_log("render skip slot=%d sprite=%s sequence=%s frame_count=%d pos=%.2f,%.2f dst=%d,%d,%d,%d reason=offscreen",
                              i, effect->core.sprite_name,
                              effect->sequence_name[0] ? effect->sequence_name : "(none)",
                              sprite->frame_count, effect->core.gx, effect->core.gy,
                              dst.x, dst.y, dst.w, dst.h);
            continue;
        }
        debug_effects_log("render slot=%d sprite=%s sequence=%s age=%d/%d facing=%d anim=%d frame=%d frame_count=%d offset=%d,%d dst=%d,%d,%d,%d",
                          i, effect->core.sprite_name,
                          effect->sequence_name[0] ? effect->sequence_name : "(none)",
                          effect->age_ms, effect->duration_ms, effect->core.facing_code,
                          effect->frame_ms > 0 ? effect->age_ms / effect->frame_ms : 0,
                          frame, sprite->frame_count,
                          effect->core.render_offset_x, effect->core.render_offset_y,
                          dst.x, dst.y, dst.w, dst.h);
        SDL_RendererFlip flip = (effect->core.render_flags & RTS_FRAME_FLIP_X) ?
            SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        SDL_Texture *texture = begin_sprite_command(sprite, effect->core.render_flags,
                                                    effect->core.render_remap,
                                                    effect->core.render_intensity);
        SDL_RenderCopyEx(app->renderer, texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
        end_sprite_command(texture, effect->core.render_flags);
    }
}

void G_Responder(app_t *app, const level_t *map, mobj_t *units, int unit_count,
                  const spritesheet_t *fallback_sprite, const spritecache_t *cache,
                  const gameinfo_t *game_info, const SDL_Event *e) {
    switch (e->type) {
        case SDL_QUIT:
            app->running = false;
            break;
        case SDL_WINDOWEVENT:
            if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                R_RefreshViewport(app);
            }
            break;
        case SDL_KEYDOWN:
            if (e->key.keysym.sym == SDLK_ESCAPE) app->running = false;
            if (e->key.keysym.sym == SDLK_g) app->show_grid = !app->show_grid;
            if (e->key.keysym.sym == SDLK_b) app->show_blocked = !app->show_blocked;
            if (e->key.keysym.sym == SDLK_a && (e->key.keysym.mod & KMOD_CTRL)) {
                for (int i = 0; i < unit_count; ++i) {
                    units[i].selected = units[i].owner == 0 &&
                        (units[i].traits & MF_SELECTABLE) != 0 &&
                        units[i].hp > 0;
                }
            }
            break;
        case SDL_MOUSEMOTION:
            R_WindowToRenderPt(app, e->motion.x, e->motion.y, &app->mouse.x, &app->mouse.y);
            if (app->panning) {
                float dx = 0.0f, dy = 0.0f;
                R_WindowToRenderDelta(app, e->motion.xrel, e->motion.yrel, &dx, &dy);
                app->cam.x += dx;
                app->cam.y += dy;
            }
            if (app->dragging_select) {
                int mx = 0, my = 0;
                R_WindowToRenderPt(app, e->motion.x, e->motion.y, &mx, &my);
                app->selection_rect = irect_from_points(app->mouse_down, (ivec2_t){ mx, my });
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            R_WindowToRenderPt(app, e->button.x, e->button.y, &app->mouse_down.x, &app->mouse_down.y);
            if (e->button.button == SDL_BUTTON_LEFT) {
                app->dragging_select = true;
                app->selection_rect = (irect_t){ app->mouse_down.x, app->mouse_down.y, 0, 0 };
            } else if (e->button.button == SDL_BUTTON_RIGHT) {
                int rx = 0, ry = 0;
                R_WindowToRenderPt(app, e->button.x, e->button.y, &rx, &ry);
                float gx = 0.0f, gy = 0.0f;
                screen_to_map_grid_point(app, map, rx, ry, &gx, &gy);
                int target = pick_unit_at(app, map, units, unit_count, fallback_sprite, cache,
                                          game_info, rx, ry, -1);
                if (target >= 0 && units[target].owner != 0 && units[target].hp > 0) {
                    for (int i = 0; i < unit_count; ++i) {
                        if (!units[i].selected || units[i].owner != 0 || units[i].hp <= 0) continue;
                        if ((units[i].traits & MF_ATTACK) == 0) continue;
                        units[i].attack.target = target;
                        units[i].harvest.target = -1;
                        units[i].harvest.timer_ms = 0;
                    }
                    gx = units[target].core.gx;
                    gy = units[target].core.gy;
                } else {
                    if (P_HarvestOrderAt(map, units, unit_count, gx, gy)) {
                        break;
                    }
                    for (int i = 0; i < unit_count; ++i) {
                        if (units[i].selected && units[i].owner == 0) {
                            units[i].attack.target = -1;
                        }
                    }
                }
                P_MoveOrderAt(map, units, unit_count, gx, gy);
            } else if (e->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e->button.button == SDL_BUTTON_LEFT && app->dragging_select) {
                int bx = 0, by = 0;
                R_WindowToRenderPt(app, e->button.x, e->button.y, &bx, &by);
                irect_t rect = irect_from_points(app->mouse_down, (ivec2_t){ bx, by });
                bool box = rect.w > 5 || rect.h > 5;
                bool additive = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (!additive) {
                    for (int i = 0; i < unit_count; ++i) units[i].selected = false;
                }
                if (box) {
                    for (int i = 0; i < unit_count; ++i) {
                        if (units[i].hp <= 0) continue;
                        if ((units[i].traits & MF_SELECTABLE) == 0) continue;
                        if (units[i].owner != 0) continue;
                        irect_t visible;
                        float sx = 0.0f, sy = 0.0f;
                        unit_screen_rect_for_view(app, map, &units[i], fallback_sprite, cache,
                                                  game_info, app->ticks_ms, NULL, &visible,
                                                  &sx, &sy, NULL, NULL);
                        float radius = unit_pick_radius_px(app, &units[i]);
                        if (irect_intersects(visible, rect) ||
                            circle_intersects_rect((fvec2_t){ sx, sy }, radius, rect)) {
                            units[i].selected = true;
                        }
                    }
                } else {
                    int picked = pick_unit_at(app, map, units, unit_count, fallback_sprite, cache,
                                              game_info, bx, by, 0);
                    if (picked >= 0) {
                        units[picked].selected = true;
                    }
                }
                app->dragging_select = false;
            } else if (e->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = false;
            }
            break;
        case SDL_MOUSEWHEEL:
            app->cam.y += (float)e->wheel.y * 48.0f;
            app->cam.x += (float)e->wheel.x * 48.0f;
            break;
        default:
            break;
    }
}

void G_CameraMove(app_t *app, float dt) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    float speed = 600.0f * dt;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) app->cam.x += speed;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) app->cam.x -= speed;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) app->cam.y += speed;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) app->cam.y -= speed;
}

void R_ClampCamera(app_t *app, const level_t *map, int viewport_w, int viewport_h) {
    if (!app || !map || map->width <= 0 || map->height <= 0) return;
    if (viewport_w <= 0) viewport_w = app->win.w;
    if (viewport_h <= 0) viewport_h = app->win.h;

    float map_w = (float)map->width * (float)app_cell_w(app);
    float map_h = (float)map->height * (float)app_cell_h(app);
    if (map_w <= (float)viewport_w) {
        app->cam.x = ((float)viewport_w - map_w) * 0.5f;
    } else {
        float min_x = (float)viewport_w - map_w;
        if (app->cam.x < min_x) app->cam.x = min_x;
        if (app->cam.x > 0.0f) app->cam.x = 0.0f;
    }
    if (map_h <= (float)viewport_h) {
        app->cam.y = ((float)viewport_h - map_h) * 0.5f;
    } else {
        float min_y = (float)viewport_h - map_h;
        if (app->cam.y < min_y) app->cam.y = min_y;
        if (app->cam.y > 0.0f) app->cam.y = 0.0f;
    }
}

void R_FreeTileset(tileset_t *tileset) {
    if (tileset->texture) SDL_DestroyTexture(tileset->texture);
    free(tileset->tile_lookup);
    free(tileset->animations);
    memset(tileset, 0, sizeof(*tileset));
}

void P_FreeLevel(level_t *map) {
    free(map->tile_ids);
    for (int i = 0; i < MAX_TILE_OVERLAYS; ++i) free(map->tile_overlays[i]);
    for (int i = 0; i < MAX_TILE_OVERLAYS + 1; ++i) free(map->tile_transforms[i]);
    free(map->blocked);
    free(map->cell_colors);
    free(map->decorations);
    free(map->resource_vents);
    free(map->extras);
    if (map->destroy_native_data) map->destroy_native_data(map->native_data);
    memset(map, 0, sizeof(*map));
}

void R_FreeSprite(spritesheet_t *sprite) {
    if (!sprite) return;
    if (sprite->texture) SDL_DestroyTexture(sprite->texture);
    for (int i = 0; i < 8; ++i)
        if (sprite->remap_textures[i]) SDL_DestroyTexture(sprite->remap_textures[i]);
    free(sprite->frames);
    free(sprite->frame_bounds);
    free(sprite->frame_ground_points);
    free(sprite->frame_displacements);
    if (sprite->destroy_native_data) sprite->destroy_native_data(sprite->native_data);
    memset(sprite, 0, sizeof(*sprite));
}

void HU_FreeFont(bitmapfont_t *font) {
    if (!font) return;
    R_FreeSprite(&font->sprite);
    memset(font, 0, sizeof(*font));
}

void R_FreeSpriteCache(spritecache_t *cache) {
    for (int i = 0; i < cache->count; ++i) {
        R_FreeSprite(&cache->entries[i].sprite);
    }
    memset(cache, 0, sizeof(*cache));
}
